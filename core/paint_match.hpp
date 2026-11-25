// paint_match.hpp  -  attribute similarity search engine (v3)
//
// Finds the top-k catalog records most similar to a query of attribute targets.
// Intended for small catalogs (up to a few thousand records): every record that passes
// the hard filters is scored, then std::partial_sort picks the best k.
//
// Scoring is a weighted Gower similarity:
//   score = sum(w_i * sim_i) / sum(w_i)       over attributes that have a target
// where each sim_i is in 0..1.
//
// Attribute kinds
//   Numeric      value is a number or a range (Interval). A target inside the range is a
//                perfect hit; otherwise the gap to the nearest end is normalised by the
//                attribute's range and penalised asymmetrically per Direction
//                (falling short costs more than exceeding).
//   Categorical  one text value. Exact match = 1, otherwise directional partial credit
//                ("requested Interior, offered Both" = 1.0), otherwise ordinal distance
//                when `levels` is set, otherwise 0.
//   Set          several text values per record (e.g. substrates, sheens). Each requested
//                item is matched against the best offered item using the Categorical rules
//                plus `wildcards` (an offered "Any" satisfies everything). The attribute's
//                similarity is the mean over the requested items.
//   Keywords     free text. Target is a comma-separated list of terms; similarity is the
//                fraction of terms found (substring, case-insensitive).
//
// Hard filters (applied before scoring)
//   min / max    Numeric only. The record's value (or any part of its range) must lie
//                inside [min, max]. Records with no value fail.
//   required     the target must be fully met: Numeric per Direction (within tolerance),
//                text kinds need similarity 1 for every requested item / term.
//
// Text is compared case-insensitively; '-' and '_' count as spaces and runs of whitespace
// collapse, so "Semi-Gloss", "semi gloss" and "SEMI  GLOSS" are equal.
//
// Pure C++17, no Qt dependency.

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace paintmatch {

// ------------------------------------------------------------------ values

struct Interval {
    double lo = 0.0;
    double hi = 0.0;
};

// monostate = no value / no target
using Value = std::variant<std::monostate, Interval, std::string, std::vector<std::string>>;

inline Value number(double v) { return Interval{v, v}; }
inline Value range(double a, double b) { return Interval{std::min(a, b), std::max(a, b)}; }
inline Value text(std::string s) { return Value{std::move(s)}; }
inline Value items(std::vector<std::string> v) { return Value{std::move(v)}; }
inline bool isEmpty(const Value& v) { return std::holds_alternative<std::monostate>(v); }

inline std::string normalizeText(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    bool pendingSpace = false;
    for (char ch : in) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isspace(c) || c == '-' || c == '_') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// ------------------------------------------------------------------ schema

enum class Kind { Numeric, Categorical, Set, Keywords };

enum class Direction {
    HigherIsBetter,  // exceeding the target is fine, falling short is penalised hard
    LowerIsBetter,   // e.g. price, minimum application temperature, dry time
    Symmetric        // plain closeness
};

struct AttributeSpec {
    struct Related {
        std::string requested;  // what the query asks for
        std::string offered;    // what the record has
        double similarity;      // 0..1, one direction only
    };

    std::string name;
    Kind kind = Kind::Numeric;

    // Relative importance; a Criterion can override it per query. 0 = filter only.
    double weight = 1.0;

    // Dissimilarity charged when the query targets this attribute but the record has no value.
    double missingDissimilarity = 0.5;

    // --- Numeric
    Direction direction = Direction::HigherIsBetter;
    double range = 0.0;  // normalisation range; <= 0 means max - min observed in the catalog
    // Multipliers on the normalised gap:
    //   shortfallFactor: gap on the wrong side (either side for Symmetric)
    //   excessFactor:    gap on the good side (small, so over-spec is barely penalised)
    double shortfallFactor = 2.0;
    double excessFactor = 0.25;

    // --- Categorical / Set
    std::vector<std::string> levels;     // optional ordering, lowest to highest; uses shortfallFactor
    std::vector<std::string> wildcards;  // offered values that satisfy any request (e.g. "Any")
    std::vector<Related> related;        // directional partial credit
};

// ------------------------------------------------------------------ query / results

struct Criterion {
    Value target;                    // monostate = no preference (not scored)
    std::optional<double> min;       // Numeric: hard floor
    std::optional<double> max;       // Numeric: hard ceiling
    bool required = false;           // hard filter: target must be fully met
    double tolerance = 0.0;          // Numeric + required: allowed shortfall, in attribute units
    std::optional<double> weight;    // overrides AttributeSpec::weight

    bool isActive() const { return !isEmpty(target) || min || max; }
};

struct Query {
    std::map<std::string, Criterion> criteria;  // keyed by AttributeSpec::name
    std::size_t limit = 5;
};

struct AttributeScore {
    std::size_t attribute = 0;  // index into Matcher::specs()
    bool scored = false;        // false: filter-only criterion (no target or weight 0)
    bool hasValue = false;      // record has a value for this attribute
    double similarity = 0.0;    // 0..1, meaningful when scored
};

struct Match {
    std::size_t row = 0;  // index into the catalog passed to setCatalog()
    double score = 0.0;   // 0..1
    std::vector<AttributeScore> breakdown;  // active criteria, in spec order
};

struct SearchResult {
    std::vector<Match> matches;
    std::size_t candidates = 0;   // records that passed the hard filters
    std::size_t catalogSize = 0;
    std::vector<std::size_t> activeAttributes;  // spec indices of active criteria, in spec order
};

// ------------------------------------------------------------------ matcher

class Matcher {
public:
    explicit Matcher(std::vector<AttributeSpec> specs) : specs_(std::move(specs)) {
        const std::size_t n = specs_.size();
        levelIndex_.resize(n);
        wildcards_.resize(n);
        related_.resize(n);
        ranges_.assign(n, 1.0);

        for (std::size_t i = 0; i < n; ++i) {
            const AttributeSpec& s = specs_[i];
            if (s.name.empty()) throw std::invalid_argument("AttributeSpec with empty name");
            if (!index_.emplace(s.name, i).second)
                throw std::invalid_argument("Duplicate attribute name: " + s.name);

            for (std::size_t l = 0; l < s.levels.size(); ++l) {
                if (!levelIndex_[i].emplace(normalizeText(s.levels[l]), static_cast<int>(l)).second)
                    throw std::invalid_argument("Duplicate level '" + s.levels[l] + "' in '" + s.name + "'");
            }
            for (const auto& w : s.wildcards) wildcards_[i].push_back(normalizeText(w));
            for (const auto& r : s.related)
                related_[i][{normalizeText(r.requested), normalizeText(r.offered)}] =
                    std::clamp(r.similarity, 0.0, 1.0);
        }
    }

    // rows[r][a] is the value of attribute a for record r (monostate = missing).
    // Numeric needs Interval; Categorical/Keywords need string; Set takes a vector or a single string.
    void setCatalog(const std::vector<std::vector<Value>>& rows) {
        const std::size_t n = specs_.size();
        std::vector<Cell> cells(rows.size() * n);
        std::vector<double> ranges(n, 1.0);

        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (rows[r].size() != n)
                throw std::invalid_argument("Catalog row " + std::to_string(r) + " has " +
                                            std::to_string(rows[r].size()) + " values, expected " +
                                            std::to_string(n));
            for (std::size_t a = 0; a < n; ++a)
                cells[r * n + a] = toCell(a, rows[r][a], "catalog row " + std::to_string(r));
        }

        for (std::size_t a = 0; a < n; ++a) {
            const AttributeSpec& s = specs_[a];
            if (s.kind != Kind::Numeric) continue;
            if (s.range > 0.0) {
                ranges[a] = s.range;
                continue;
            }
            double lo = std::numeric_limits<double>::infinity();
            double hi = -std::numeric_limits<double>::infinity();
            for (std::size_t r = 0; r < rows.size(); ++r) {
                const Cell& c = cells[r * n + a];
                if (!c.has) continue;
                lo = std::min(lo, c.num.lo);
                hi = std::max(hi, c.num.hi);
            }
            ranges[a] = (hi > lo) ? hi - lo : 1.0;
        }

        cells_ = std::move(cells);
        ranges_ = std::move(ranges);
        rowCount_ = rows.size();
    }

    // Throws std::invalid_argument for unknown attributes, wrong target types or min > max.
    SearchResult search(const Query& query) const {
        const std::vector<Active> active = resolve(query);

        SearchResult result;
        result.catalogSize = rowCount_;
        for (const Active& a : active) result.activeAttributes.push_back(a.attr);

        std::vector<std::pair<double, std::size_t>> scored;  // (score, row)
        scored.reserve(rowCount_);
        for (std::size_t r = 0; r < rowCount_; ++r) {
            if (passesFilters(r, active)) scored.emplace_back(scoreRow(r, active, nullptr), r);
        }
        result.candidates = scored.size();

        const std::size_t k = std::min(query.limit, scored.size());
        std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(k), scored.end(),
                          [](const auto& a, const auto& b) {
                              if (a.first != b.first) return a.first > b.first;
                              return a.second < b.second;  // ties keep catalog order
                          });

        result.matches.reserve(k);
        for (std::size_t i = 0; i < k; ++i) {
            Match m;
            m.row = scored[i].second;
            m.score = scoreRow(m.row, active, &m.breakdown);
            result.matches.push_back(std::move(m));
        }
        return result;
    }

    const std::vector<AttributeSpec>& specs() const { return specs_; }
    std::size_t catalogSize() const { return rowCount_; }
    std::optional<std::size_t> indexOf(const std::string& name) const {
        auto it = index_.find(name);
        if (it == index_.end()) return std::nullopt;
        return it->second;
    }
    double effectiveRange(std::size_t attr) const { return ranges_.at(attr); }

private:
    struct Item {
        std::string text;  // normalised
        int level = -1;    // index into levels, -1 if none
    };

    struct Cell {
        bool has = false;
        Interval num;             // Numeric
        std::vector<Item> items;  // Categorical (1) / Set (n)
        std::string text;         // Keywords (normalised haystack)
        std::vector<std::string> terms;  // Keywords targets only
    };

    struct Active {
        std::size_t attr;
        Cell target;  // has == false when the criterion has no target
        std::optional<double> min, max;
        bool required;
        double tolerance;
        double weight;
    };

    Item makeItem(std::size_t attr, const std::string& raw) const {
        Item it;
        it.text = normalizeText(raw);
        auto lv = levelIndex_[attr].find(it.text);
        if (lv != levelIndex_[attr].end()) it.level = lv->second;
        return it;
    }

    static std::vector<std::string> splitTerms(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        auto flush = [&] {
            std::string t = normalizeText(cur);
            if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t));
            cur.clear();
        };
        for (char c : s) {
            if (c == ',' || c == ';') flush();
            else cur.push_back(c);
        }
        flush();
        return out;
    }

    Cell toCell(std::size_t attr, const Value& v, const std::string& where, bool isTarget = false) const {
        const AttributeSpec& s = specs_[attr];
        Cell c;
        if (isEmpty(v)) return c;

        auto typeError = [&](const char* expected) {
            return std::invalid_argument("Attribute '" + s.name + "' expects " + expected + " (" + where + ")");
        };

        switch (s.kind) {
            case Kind::Numeric: {
                const Interval* iv = std::get_if<Interval>(&v);
                if (!iv) throw typeError("a number");
                if (!std::isfinite(iv->lo) || !std::isfinite(iv->hi)) throw typeError("a finite number");
                c.has = true;
                c.num = Interval{std::min(iv->lo, iv->hi), std::max(iv->lo, iv->hi)};
                return c;
            }
            case Kind::Categorical: {
                const std::string* str = std::get_if<std::string>(&v);
                if (!str) throw typeError("text");
                Item it = makeItem(attr, *str);
                if (it.text.empty()) return c;
                c.items.push_back(std::move(it));
                c.has = true;
                return c;
            }
            case Kind::Set: {
                std::vector<std::string> raw;
                if (const auto* list = std::get_if<std::vector<std::string>>(&v)) raw = *list;
                else if (const auto* str = std::get_if<std::string>(&v)) raw.push_back(*str);
                else throw typeError("text or a list of text");
                for (const auto& r : raw) {
                    Item it = makeItem(attr, r);
                    if (it.text.empty()) continue;
                    bool dup = std::any_of(c.items.begin(), c.items.end(),
                                           [&](const Item& e) { return e.text == it.text; });
                    if (!dup) c.items.push_back(std::move(it));
                }
                c.has = !c.items.empty();
                return c;
            }
            case Kind::Keywords: {
                std::string joined;
                if (const auto* str = std::get_if<std::string>(&v)) joined = *str;
                else if (const auto* list = std::get_if<std::vector<std::string>>(&v)) {
                    for (const auto& p : *list) joined += p + ", ";
                } else throw typeError("text");
                if (isTarget) {
                    c.terms = splitTerms(joined);
                    c.has = !c.terms.empty();
                } else {
                    c.text = normalizeText(joined);
                    c.has = !c.text.empty();
                }
                return c;
            }
        }
        return c;
    }

    std::vector<Active> resolve(const Query& query) const {
        std::vector<Active> active;
        active.reserve(query.criteria.size());
        for (const auto& [name, crit] : query.criteria) {
            auto it = index_.find(name);
            if (it == index_.end()) throw std::invalid_argument("Unknown attribute in query: " + name);
            const std::size_t a = it->second;
            const AttributeSpec& s = specs_[a];

            if ((crit.min || crit.max) && s.kind != Kind::Numeric)
                throw std::invalid_argument("Min/max only apply to numeric attributes ('" + name + "')");
            if (crit.min && crit.max && *crit.min > *crit.max)
                throw std::invalid_argument("Minimum is greater than maximum for '" + name + "'");

            Cell target = toCell(a, crit.target, "query", /*isTarget=*/true);
            if (!target.has && !crit.min && !crit.max) continue;  // nothing to do

            const double w = std::max(crit.weight.value_or(s.weight), 0.0);
            active.push_back({a, std::move(target), crit.min, crit.max, crit.required && !isEmpty(crit.target),
                              crit.tolerance, w});
        }
        std::sort(active.begin(), active.end(), [](const Active& x, const Active& y) { return x.attr < y.attr; });
        return active;
    }

    const Cell& cell(std::size_t row, std::size_t attr) const { return cells_[row * specs_.size() + attr]; }

    // Positive = on the good side of the target, negative = on the bad side.
    static double signedGap(Direction dir, double target, double v) {
        switch (dir) {
            case Direction::HigherIsBetter: return v - target;
            case Direction::LowerIsBetter:  return target - v;
            case Direction::Symmetric:      return -std::abs(v - target);
        }
        return 0.0;
    }

    // Gap from the target to the closest point of the record's range (0 when inside).
    static double closestPoint(const Interval& iv, double t) { return std::clamp(t, iv.lo, iv.hi); }

    double itemSimilarity(std::size_t attr, const Item& req, const std::vector<Item>& offered) const {
        const AttributeSpec& s = specs_[attr];
        double best = 0.0;
        for (const Item& off : offered) {
            if (off.text == req.text) return 1.0;
            if (std::find(wildcards_[attr].begin(), wildcards_[attr].end(), off.text) != wildcards_[attr].end())
                return 1.0;
            auto rel = related_[attr].find({req.text, off.text});
            if (rel != related_[attr].end()) best = std::max(best, rel->second);
            if (req.level >= 0 && off.level >= 0 && s.levels.size() > 1) {
                const double steps = std::abs(req.level - off.level) / static_cast<double>(s.levels.size() - 1);
                best = std::max(best, 1.0 - std::min(steps * s.shortfallFactor, 1.0));
            }
        }
        return best;
    }

    double similarity(std::size_t attr, const Cell& target, const Cell& c) const {
        const AttributeSpec& s = specs_[attr];
        if (!c.has) return 1.0 - std::clamp(s.missingDissimilarity, 0.0, 1.0);

        switch (s.kind) {
            case Kind::Numeric: {
                const double t = target.num.lo;
                const double g = signedGap(s.direction, t, closestPoint(c.num, t)) / ranges_[attr];
                const double d = (g < 0.0) ? -g * s.shortfallFactor : g * s.excessFactor;
                return 1.0 - std::min(d, 1.0);
            }
            case Kind::Categorical:
            case Kind::Set: {
                double sum = 0.0;
                for (const Item& req : target.items) sum += itemSimilarity(attr, req, c.items);
                return target.items.empty() ? 1.0 : sum / static_cast<double>(target.items.size());
            }
            case Kind::Keywords: {
                std::size_t found = 0;
                for (const auto& term : target.terms)
                    if (c.text.find(term) != std::string::npos) ++found;
                return target.terms.empty() ? 1.0
                                            : static_cast<double>(found) / static_cast<double>(target.terms.size());
            }
        }
        return 0.0;
    }

    bool passesFilters(std::size_t row, const std::vector<Active>& active) const {
        constexpr double eps = 1e-9;
        for (const Active& a : active) {
            const Cell& c = cell(row, a.attr);
            if (a.min || a.max) {
                if (!c.has) return false;
                if (a.min && c.num.hi < *a.min - eps) return false;
                if (a.max && c.num.lo > *a.max + eps) return false;
            }
            if (!a.required) continue;
            if (!c.has) return false;  // unknown value can't satisfy a hard requirement

            const AttributeSpec& s = specs_[a.attr];
            if (s.kind == Kind::Numeric) {
                const double t = a.target.num.lo;
                if (signedGap(s.direction, t, closestPoint(c.num, t)) < -a.tolerance - eps) return false;
            } else if (similarity(a.attr, a.target, c) < 1.0 - eps) {
                return false;
            }
        }
        return true;
    }

    double scoreRow(std::size_t row, const std::vector<Active>& active, std::vector<AttributeScore>* breakdown) const {
        double sum = 0.0, wsum = 0.0;
        for (const Active& a : active) {
            const Cell& c = cell(row, a.attr);
            AttributeScore as;
            as.attribute = a.attr;
            as.hasValue = c.has;
            if (a.target.has && a.weight > 0.0) {
                as.scored = true;
                as.similarity = similarity(a.attr, a.target, c);
                sum += a.weight * as.similarity;
                wsum += a.weight;
            }
            if (breakdown) breakdown->push_back(as);
        }
        return wsum > 0.0 ? sum / wsum : 1.0;  // no scored criteria: every candidate matches equally
    }

    std::vector<AttributeSpec> specs_;
    std::map<std::string, std::size_t> index_;
    std::vector<std::map<std::string, int>> levelIndex_;
    std::vector<std::vector<std::string>> wildcards_;
    std::vector<std::map<std::pair<std::string, std::string>, double>> related_;
    std::vector<Cell> cells_;  // row-major [record][attribute]
    std::vector<double> ranges_;
    std::size_t rowCount_ = 0;
};

}  // namespace paintmatch
