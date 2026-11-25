#include "paint_schema.hpp"

#include "csv_reader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

namespace paintdb {

using paintmatch::AttributeSpec;
using paintmatch::Direction;
using paintmatch::Interval;
using paintmatch::Kind;
using paintmatch::Value;

namespace {

// Lowercase letters and digits only: "Bathroom Rated?" -> "bathroomrated"
std::string canonical(std::string_view s) {
    std::string out;
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

const std::vector<std::string>& columnsTable() {
    static const std::vector<std::string> cols = {
        "Location of Purchase",
        "Brand",
        "Coating Type",
        "Line",
        "Primary Industry Use",
        "Advertised Purpose",
        "Interior or Exterior",
        "Typical Substrate(s)",
        "Barstow/Lawnwood Range",
        "Avg. Price Point",
        "Coating Base",
        "Low or Zero VOC?",
        "Typical Availability",
        "Lower Temperature Threshold",
        "Humidity Threshold on Substrate",
        "Typical Dry Time",
        "Typical Cure Time",
        "Average Coverage / Square Foot",
        "Bathroom Rated?",
        "Sheens",
        "Special Properties",
        "Colors",
        "Special Application Method",
        "Product Data Sheet",
        "Safety Data Sheet",
        "Picture Example",
    };
    return cols;
}

const std::vector<std::string>& canonicalColumns() {
    static const std::vector<std::string> canon = [] {
        std::vector<std::string> v;
        for (const auto& c : columnsTable()) v.push_back(canonical(c));
        return v;
    }();
    return canon;
}

AttributeDef makeDef(std::string key, std::string label, std::string group, std::vector<std::string> columns,
                     Parser parser, Kind kind, double weight) {
    AttributeDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.group = std::move(group);
    d.columns = std::move(columns);
    d.parser = parser;
    d.spec.name = d.key;
    d.spec.kind = kind;
    d.spec.weight = weight;
    return d;
}

AttributeDef makeNumeric(std::string key, std::string label, std::string group, std::string column, Parser parser,
                         Direction dir, double weight, std::string unit, double range, double inMin, double inMax,
                         double step, int decimals) {
    AttributeDef d = makeDef(std::move(key), std::move(label), std::move(group), {std::move(column)}, parser,
                             Kind::Numeric, weight);
    d.spec.direction = dir;
    d.spec.range = range;
    d.unit = std::move(unit);
    d.inputMin = inMin;
    d.inputMax = inMax;
    d.inputStep = step;
    d.decimals = decimals;
    return d;
}

void bothWays(AttributeSpec& s, const std::string& a, const std::string& b, double sim) {
    s.related.push_back({a, b, sim});
    s.related.push_back({b, a, sim});
}

std::vector<AttributeDef> buildDefs() {
    const std::string gProduct = "Product";
    const std::string gUse = "Use & Environment";
    const std::string gApp = "Application";
    const std::string gFinish = "Finish & Appearance";

    std::vector<AttributeDef> defs;

    // ---------------------------------------------------------------- Product
    {
        auto d = makeDef("coatingType", "Coating Type", gProduct, {"Coating Type"}, Parser::Text, Kind::Categorical, 3.0);
        d.options = {"Paint", "Primer", "Stain", "Varnish", "Drywall Sealer", "Block Filler", "DTM", "Clear Coat",
                     "Epoxy", "Intumscent Coating", "Permanizer", "Electrostatic Coating"};
        d.hint = "Type of coating.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("coatingBase", "Coating Base", gProduct, {"Coating Base"}, Parser::List, Kind::Set, 1.5);
        d.options = {"Water/Latex", "Oil", "Alkyd", "Acrylic", "Epoxy", "Urethane", "Polyvinyl"};
        bothWays(d.spec, "Water/Latex", "Acrylic", 0.8);
        bothWays(d.spec, "Oil", "Alkyd", 0.8);
        bothWays(d.spec, "Epoxy", "Urethane", 0.5);
        d.hint = "Binder / base chemistry. Pick one or more; related bases (latex~acrylic, oil~alkyd, "
                 "epoxy~urethane) earn partial credit.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("brand", "Brand", gProduct, {"Brand"}, Parser::Text, Kind::Categorical, 1.0);
        d.options = {"Sherwin Williams", "Benjamin Moore", "California Paints", "Renner", "Ecos Paints", "PPG",
                     "Valspar", "Zinnser", "Bin", "Insl-X", "Behr", "General Finishes", "Davlin", "Portola",
                     "Mad Dog", "Sikkens", "Cabot", "Minwax"};
        defs.push_back(d);
    }
    {
        auto d = makeDef("location", "Location of Purchase", gProduct, {"Location of Purchase"}, Parser::Text,
                         Kind::Categorical, 0.5);
        d.options = {"Sherwin Williams", "VT Paint", "PPG", "Lowes", "Home Depot", "Ace Hardware",
                     "Able Paint & Glass", "Aubuchon", "Distinctive Paints", "Curtis Lumber"};
        defs.push_back(d);
    }
    {
        auto d = makeDef("availability", "Typical Availability", gProduct, {"Typical Availability"}, Parser::Text,
                         Kind::Categorical, 0.5);
        d.options = {"Immediate", "Order Ahead"};
        d.spec.related.push_back({"Order Ahead", "Immediate", 1.0});  // can wait -> in-stock is fine too
        d.hint = "Choosing 'Order Ahead' also fully accepts products that are available immediately.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("price", "Avg. Price Point", gProduct, "Avg. Price Point", Parser::Currency,
                             Direction::LowerIsBetter, 1.0, "$", 0.0, 0.0, 1000.0, 1.0, 2);
        d.hint = "Average price per gallon. Lower is better: cheaper than the target is barely penalised, "
                 "more expensive is penalised harder.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("barstow", "Barstow/Lawnwood Range", gProduct, "Barstow/Lawnwood Range", Parser::Range,
                             Direction::Symmetric, 1.0, "", 4.0, 1.0, 5.0, 0.5, 1);
        d.options = {"1-5", "1-2", "1-3", "3-5", "4-5", "5"};
        d.hint = "Rated range on the 1-5 scale. A target inside the product's range is a full match; "
                 "otherwise the distance to the nearest end counts, in either direction.";
        defs.push_back(d);
    }

    // ---------------------------------------------------------------- Use & Environment
    {
        auto d = makeDef("intExt", "Interior or Exterior", gUse, {"Interior or Exterior"}, Parser::Text,
                         Kind::Categorical, 3.0);
        d.options = {"Interior", "Exterior", "Both"};
        d.spec.related = {{"Interior", "Both", 1.0}, {"Exterior", "Both", 1.0},
                          {"Both", "Interior", 0.5}, {"Both", "Exterior", 0.5}};
        d.hint = "A product rated 'Both' fully satisfies an Interior or Exterior request.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("industryUse", "Primary Industry Use", gUse, {"Primary Industry Use"}, Parser::Text,
                         Kind::Categorical, 1.5);
        d.options = {"Interior Residential", "Interior Commercial", "Interior Industrial",
                     "Exterior Residential", "Exterior Commercial", "Exterior Industrial"};
        for (const char* side : {"Interior", "Exterior"}) {
            const std::string s = side;
            bothWays(d.spec, s + " Residential", s + " Commercial", 0.5);
            bothWays(d.spec, s + " Commercial", s + " Industrial", 0.5);
            bothWays(d.spec, s + " Residential", s + " Industrial", 0.25);
        }
        d.hint = "Same interior/exterior side earns partial credit.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("substrates", "Typical Substrate(s)", gUse, {"Typical Substrate(s)"}, Parser::List,
                         Kind::Set, 2.0);
        d.options = {"Unpainted sheetrock", "Painted sheetrock/plaster", "Painted wood", "Unpainted wood",
                     "Unpainted metal", "Painted metal", "Primed Fiberglass", "Primed PVC", "ACT", "Vinyl",
                     "Unpainted CMU", "Painted CMU"};
        d.hint = "Surfaces to coat. Pick one or more; the score is the share of them the product covers.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("bathroom", "Bathroom Rated?", gUse, {"Bathroom Rated?"}, Parser::Bool, Kind::Categorical, 1.0);
        d.options = {"Yes", "No"};
        defs.push_back(d);
    }
    {
        auto d = makeDef("lowVoc", "Low or Zero VOC?", gUse, {"Low or Zero VOC?"}, Parser::Bool, Kind::Categorical, 1.0);
        d.options = {"Yes", "No"};
        defs.push_back(d);
    }

    // ---------------------------------------------------------------- Application
    {
        auto d = makeNumeric("lowerTemp", "Lower Temperature Threshold", gApp, "Lower Temperature Threshold",
                             Parser::Number, Direction::LowerIsBetter, 2.0, "\xC2\xB0" "F", 20.0, 0.0, 120.0, 5.0, 0);
        d.hint = "Lowest temperature the product can be applied at. Lower is better (usable in colder "
                 "conditions). Use Max to exclude products that need warmer conditions than you have.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("humidity", "Humidity Threshold on Substrate", gApp, "Humidity Threshold on Substrate",
                             Parser::Percent, Direction::HigherIsBetter, 1.0, "%", 10.0, 0.0, 100.0, 1.0, 0);
        d.hint = "Highest substrate moisture the product tolerates. Higher is better.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("dryTouch", "Dry to Touch", gApp, "Typical Dry Time", Parser::DryTouch,
                             Direction::LowerIsBetter, 0.5, "h", 4.0, 0.0, 48.0, 0.25, 2);
        d.hint = "Hours until dry to the touch (from 'Typical Dry Time'). Lower is better.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("recoat", "Recoat Time", gApp, "Typical Dry Time", Parser::DryRecoat,
                             Direction::LowerIsBetter, 1.0, "h", 12.0, 0.0, 72.0, 0.5, 1);
        d.hint = "Hours until it can be recoated (from 'Typical Dry Time'). Lower is better.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("cureTime", "Typical Cure Time", gApp, "Typical Cure Time", Parser::Days,
                             Direction::LowerIsBetter, 0.5, "days", 30.0, 0.0, 90.0, 1.0, 0);
        d.hint = "Days to full cure. Lower is better.";
        defs.push_back(d);
    }
    {
        auto d = makeNumeric("coverage", "Coverage", gApp, "Average Coverage / Square Foot", Parser::Number,
                             Direction::HigherIsBetter, 1.0, "sq ft/gal", 0.0, 0.0, 1000.0, 25.0, 0);
        d.hint = "Average coverage in square feet per gallon. Higher is better.";
        defs.push_back(d);
    }

    // ---------------------------------------------------------------- Finish & Appearance
    {
        auto d = makeDef("sheens", "Sheens", gFinish, {"Sheens"}, Parser::List, Kind::Set, 1.5);
        d.options = {"Flat", "Matte", "Low Lustre", "Pearl", "Eggshell", "Satin", "Medium Lustre",
                     "Semi Gloss", "Gloss", "High Gloss"};
        d.spec.levels = d.options;   // ordered dullest -> glossiest
        d.spec.shortfallFactor = 3.0;  // 1 step away = 0.67, 2 = 0.33, 3+ = 0
        d.hint = "Sheen(s) wanted. If the product does not offer one, the nearest sheen it does offer earns "
                 "partial credit.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("colors", "Colors", gFinish, {"Colors"}, Parser::List, Kind::Set, 1.0);
        d.options = {"White", "Black or Gray", "Specific"};
        d.spec.wildcards = {"Any"};
        d.hint = "Products listed as 'Any' color satisfy every color request.";
        defs.push_back(d);
    }
    {
        auto d = makeDef("keywords", "Keywords", gFinish,
                         {"Line", "Advertised Purpose", "Special Properties", "Special Application Method"},
                         Parser::Combined, Kind::Keywords, 1.5);
        d.hint = "Comma-separated words or phrases searched in Line, Advertised Purpose, Special Properties "
                 "and Special Application Method, e.g. \"stain blocking, mildew\".";
        defs.push_back(d);
    }

    return defs;
}

std::string replaceDashes(std::string s) {
    // en dash (E2 80 93) and em dash (E2 80 94) -> '-'
    for (const char* dash : {"\xE2\x80\x93", "\xE2\x80\x94"}) {
        std::size_t pos;
        while ((pos = s.find(dash)) != std::string::npos) s.replace(pos, 3, "-");
    }
    return s;
}

std::optional<double> toDouble(const std::string& s) {
    double v = 0.0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    auto res = std::from_chars(first, last, v);
    if (res.ec != std::errc() || res.ptr != last || !std::isfinite(v)) return std::nullopt;
    return v;
}

int monthFromName(std::string name) {
    static const char* months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                   "jul", "aug", "sep", "oct", "nov", "dec"};
    if (name.size() < 3) return 0;
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < 12; ++i)
        if (name.compare(0, 3, months[i]) == 0) return i + 1;
    return 0;
}

std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string fixed(double v, int decimals) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, decimals);
    return std::string(buf, res.ptr);
}

}  // namespace

// ==================================================================== schema access

const std::vector<AttributeDef>& attributeDefs() {
    static const std::vector<AttributeDef> defs = buildDefs();
    return defs;
}

std::vector<AttributeSpec> engineSpecs() {
    std::vector<AttributeSpec> specs;
    for (const auto& d : attributeDefs()) specs.push_back(d.spec);
    return specs;
}

std::optional<std::size_t> attributeIndex(std::string_view key) {
    const auto& defs = attributeDefs();
    for (std::size_t i = 0; i < defs.size(); ++i)
        if (defs[i].key == key) return i;
    return std::nullopt;
}

const std::vector<std::string>& knownColumns() { return columnsTable(); }

std::optional<std::size_t> columnIndex(std::string_view name) {
    const std::string c = canonical(name);
    if (c.empty()) return std::nullopt;
    const auto& canon = canonicalColumns();
    for (std::size_t i = 0; i < canon.size(); ++i)
        if (canon[i] == c) return i;
    return std::nullopt;
}

const std::string& Product::field(std::string_view column) const {
    static const std::string empty;
    auto idx = columnIndex(column);
    if (!idx || *idx >= fields.size()) return empty;
    return fields[*idx];
}

std::string Product::displayName() const {
    const std::string& brand = field(col::Brand);
    const std::string& line = field(col::Line);
    if (brand.empty()) return line.empty() ? std::string("(unnamed product)") : line;
    if (line.empty()) return brand;
    return brand + " " + line;
}

// ==================================================================== cell parsers

namespace parse {

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool isNotApplicable(std::string_view s) {
    const std::string t = toLower(trimmed(s));
    return t.empty() || t == "na" || t == "n/a" || t == "none" || t == "-" || t == "--";
}

std::optional<double> number(std::string_view s) {
    std::string t;
    for (char c : trimmed(s)) {
        if (c == '$' || c == ',' || c == '%' || c == '+' || std::isspace(static_cast<unsigned char>(c))) continue;
        t.push_back(c);
    }
    if (t.empty()) return std::nullopt;
    return toDouble(t);
}

std::optional<double> percent(std::string_view s) {
    const bool hasSign = s.find('%') != std::string_view::npos;
    auto v = number(s);
    if (!v) return std::nullopt;
    if (!hasSign && *v <= 1.0) return *v * 100.0;  // fraction such as 0.15
    return v;
}

std::optional<Interval> rangeValue(std::string_view raw) {
    const std::string s = replaceDashes(trimmed(raw));
    if (s.empty()) return std::nullopt;

    static const std::regex iso(R"(^(\d{4})-(\d{1,2})-(\d{1,2})(?:[ T].*)?$)");
    static const std::regex usDate(R"(^(\d{1,2})/(\d{1,2})/(\d{2,4})(?:\s.*)?$)");
    static const std::regex dayMon(R"(^(\d{1,2})[- ]([A-Za-z]{3,9})\.?$)");
    static const std::regex monDay(R"(^([A-Za-z]{3,9})\.?[- ](\d{1,2})$)");
    static const std::regex span(R"(^(\d+(?:\.\d+)?)\s*(?:-|to)\s*(\d+(?:\.\d+)?)$)", std::regex::icase);

    std::smatch m;
    // Excel turns "3-5" into a date (5 March). Recover month-day as the range.
    if (std::regex_match(s, m, iso)) return paintmatch::Interval{toDouble(m[2]).value_or(0.0), toDouble(m[3]).value_or(0.0)};
    if (std::regex_match(s, m, usDate)) return paintmatch::Interval{toDouble(m[1]).value_or(0.0), toDouble(m[2]).value_or(0.0)};
    if (std::regex_match(s, m, dayMon)) {
        int month = monthFromName(m[2]);
        if (month) return paintmatch::Interval{static_cast<double>(month), toDouble(m[1]).value_or(0.0)};
    }
    if (std::regex_match(s, m, monDay)) {
        int month = monthFromName(m[1]);
        if (month) return paintmatch::Interval{static_cast<double>(month), toDouble(m[2]).value_or(0.0)};
    }
    if (std::regex_match(s, m, span)) {
        auto a = toDouble(m[1]), b = toDouble(m[2]);
        if (a && b) return paintmatch::Interval{std::min(*a, *b), std::max(*a, *b)};
    }
    if (auto v = number(s)) return paintmatch::Interval{*v, *v};
    return std::nullopt;
}

std::optional<Interval> dryTime(std::string_view raw, bool recoat) {
    const std::string s = replaceDashes(std::string(raw));
    // "1-2 to touch", "30 min to touch", "4 hrs to recoat"
    static const std::regex valueFirst(
        R"((\d+(?:\.\d+)?)(?:\s*-\s*(\d+(?:\.\d+)?))?\s*(minutes?|mins?|hours?|hrs?|h)?\s*(?:to\s+)?(touch|recoat))",
        std::regex::icase);
    // "Touch: 1 hr", "Recoat 4-6 hours"
    static const std::regex labelFirst(
        R"((touch|recoat)\s*:?\s*(\d+(?:\.\d+)?)(?:\s*-\s*(\d+(?:\.\d+)?))?\s*(minutes?|mins?|hours?|hrs?|h)?)",
        std::regex::icase);

    const std::string want = recoat ? "recoat" : "touch";
    auto build = [](const std::string& a, const std::string& b, const std::string& unit) -> std::optional<Interval> {
        auto lo = toDouble(a);
        if (!lo) return std::nullopt;
        auto hi = b.empty() ? lo : toDouble(b);
        if (!hi) return std::nullopt;
        const double f = toLower(unit).rfind("min", 0) == 0 ? 1.0 / 60.0 : 1.0;
        return Interval{std::min(*lo, *hi) * f, std::max(*lo, *hi) * f};
    };

    for (auto it = std::sregex_iterator(s.begin(), s.end(), valueFirst); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        if (toLower(m[4]) == want) return build(m[1], m[2], m[3]);
    }
    for (auto it = std::sregex_iterator(s.begin(), s.end(), labelFirst); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        if (toLower(m[1]) == want) return build(m[2], m[3], m[4]);
    }
    return std::nullopt;
}

std::optional<Interval> days(std::string_view raw) {
    const std::string s = replaceDashes(std::string(raw));
    static const std::regex re(
        R"((\d+(?:\.\d+)?)(?:\s*-\s*(\d+(?:\.\d+)?))?\s*(days?|weeks?|wks?|hours?|hrs?|d|w|h)?)",
        std::regex::icase);
    std::smatch m;
    if (!std::regex_search(s, m, re)) return std::nullopt;
    auto lo = toDouble(m[1]);
    if (!lo) return std::nullopt;
    auto hi = m[2].matched ? toDouble(m[2]) : lo;
    if (!hi) return std::nullopt;
    const std::string unit = toLower(m[3]);
    double f = 1.0;
    if (!unit.empty() && unit[0] == 'w') f = 7.0;
    else if (!unit.empty() && unit[0] == 'h') f = 1.0 / 24.0;
    return Interval{std::min(*lo, *hi) * f, std::max(*lo, *hi) * f};
}

std::optional<bool> boolean(std::string_view s) {
    const std::string t = toLower(trimmed(s));
    if (t == "true" || t == "yes" || t == "y" || t == "1" || t == "x" || t == "t") return true;
    if (t == "false" || t == "no" || t == "n" || t == "0" || t == "f") return false;
    return std::nullopt;
}

std::vector<std::string> list(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::string t = trimmed(cur);
        if (!isNotApplicable(t)) out.push_back(std::move(t));
        cur.clear();
    };
    for (char c : s) {
        if (c == ',' || c == ';') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

}  // namespace parse

// ==================================================================== loading

namespace {

class Loader {
public:
    explicit Loader(Database& db) : db_(db) {}

    void warn(const Product& p, const AttributeDef& def, const std::string& raw) {
        ++db_.warningCount;
        if (db_.warnings.size() >= kMaxWarnings) return;
        db_.warnings.push_back("Line " + std::to_string(p.sourceLine) + " (" + p.displayName() + "): could not read " +
                               def.label + " from \"" + raw + "\"");
    }

    Value parseCell(const AttributeDef& def, const Product& p) {
        const std::string& raw = p.field(def.columns.front());

        switch (def.parser) {
            case Parser::Text:
                if (parse::isNotApplicable(raw)) return {};
                return paintmatch::text(raw);

            case Parser::List: {
                auto items = parse::list(raw);
                if (items.empty()) return {};
                return paintmatch::items(std::move(items));
            }

            case Parser::Bool: {
                if (parse::trimmed(raw).empty()) return {};
                auto b = parse::boolean(raw);
                if (!b) {
                    warn(p, def, raw);
                    return {};
                }
                return paintmatch::text(*b ? "Yes" : "No");
            }

            case Parser::Number:
            case Parser::Currency:
            case Parser::Percent: {
                if (parse::isNotApplicable(raw)) return {};
                auto v = def.parser == Parser::Percent ? parse::percent(raw) : parse::number(raw);
                if (!v) {
                    warn(p, def, raw);
                    return {};
                }
                return paintmatch::number(*v);
            }

            case Parser::Range:
            case Parser::DryTouch:
            case Parser::DryRecoat:
            case Parser::Days: {
                if (parse::isNotApplicable(raw)) return {};
                std::optional<Interval> iv;
                if (def.parser == Parser::Range) iv = parse::rangeValue(raw);
                else if (def.parser == Parser::Days) iv = parse::days(raw);
                else iv = parse::dryTime(raw, def.parser == Parser::DryRecoat);
                if (!iv) {
                    warn(p, def, raw);
                    return {};
                }
                return Value{*iv};
            }

            case Parser::Combined: {
                std::string joined;
                for (const auto& c : def.columns) {
                    const std::string& part = p.field(c);
                    if (parse::isNotApplicable(part)) continue;
                    if (!joined.empty()) joined += " | ";
                    joined += part;
                }
                if (joined.empty()) return {};
                return paintmatch::text(std::move(joined));
            }
        }
        return {};
    }

private:
    static constexpr std::size_t kMaxWarnings = 200;
    Database& db_;
};

bool isBlankProduct(const Product& p) {
    for (auto c : {col::Brand, col::Line, col::CoatingType, col::AdvertisedPurpose})
        if (!p.field(c).empty()) return false;
    return true;
}

void collectText(const Value& v, std::vector<std::string>& out) {
    if (const auto* s = std::get_if<std::string>(&v)) out.push_back(*s);
    else if (const auto* l = std::get_if<std::vector<std::string>>(&v)) out.insert(out.end(), l->begin(), l->end());
}

}  // namespace

Database parseDatabase(std::string_view content, const std::string& sourceName) {
    const csv::Document doc = csv::parse(content);
    if (doc.rows.empty()) throw LoadError("The file is empty.");

    Database db;
    db.source = sourceName;

    const auto& known = knownColumns();
    const auto& header = doc.rows.front();
    std::vector<int> fileToKnown(header.size(), -1);
    std::vector<bool> seen(known.size(), false);
    std::size_t matched = 0;

    for (std::size_t j = 0; j < header.size(); ++j) {
        const std::string name = parse::trimmed(header[j]);
        if (name.empty()) continue;
        auto k = columnIndex(name);
        if (!k) {
            db.ignoredColumns.push_back(name);
            continue;
        }
        if (seen[*k]) {
            db.warnings.push_back("Column \"" + name + "\" appears more than once; using the first one.");
            ++db.warningCount;
            continue;
        }
        seen[*k] = true;
        fileToKnown[j] = static_cast<int>(*k);
        ++matched;
    }

    if (matched < 5) {
        std::string found;
        for (std::size_t j = 0; j < header.size() && j < 6; ++j) {
            if (j) found += ", ";
            found += "\"" + parse::trimmed(header[j]) + "\"";
        }
        throw LoadError("This does not look like the paint database: only " + std::to_string(matched) +
                        " of the expected column headers were found (e.g. Brand, Line, Coating Type, Sheens). "
                        "First header cells: " + found + ".");
    }

    for (std::size_t k = 0; k < known.size(); ++k)
        if (!seen[k]) db.missingColumns.push_back(known[k]);
    if (doc.unterminatedQuote) {
        db.warnings.push_back("The file ends inside a quoted field; the last row may be incomplete.");
        ++db.warningCount;
    }

    const auto& defs = attributeDefs();
    db.attributeHasData.assign(defs.size(), false);
    Loader loader(db);

    for (std::size_t i = 1; i < doc.rows.size(); ++i) {
        const auto& row = doc.rows[i];
        Product p;
        p.fields.assign(known.size(), std::string());
        p.sourceLine = doc.lineNumbers[i];
        for (std::size_t j = 0; j < row.size() && j < header.size(); ++j)
            if (fileToKnown[j] >= 0) p.fields[static_cast<std::size_t>(fileToKnown[j])] = parse::trimmed(row[j]);

        if (isBlankProduct(p)) {
            ++db.blankRowsSkipped;
            continue;
        }

        std::vector<Value> values;
        values.reserve(defs.size());
        for (std::size_t a = 0; a < defs.size(); ++a) {
            Value v = loader.parseCell(defs[a], p);
            if (!paintmatch::isEmpty(v)) db.attributeHasData[a] = true;
            values.push_back(std::move(v));
        }
        db.products.push_back(std::move(p));
        db.values.push_back(std::move(values));
    }

    if (db.products.empty())
        throw LoadError("No products were found in the file (" + std::to_string(db.blankRowsSkipped) +
                        " blank rows skipped).");

    // Input choices: the workbook's list first, then any other values found in the data.
    db.options.resize(defs.size());
    for (std::size_t a = 0; a < defs.size(); ++a) {
        const AttributeDef& def = defs[a];
        if (def.spec.kind != Kind::Categorical && def.spec.kind != Kind::Set) continue;

        std::set<std::string> taken;
        for (const auto& w : def.spec.wildcards) taken.insert(paintmatch::normalizeText(w));

        std::vector<std::string> opts;
        for (const auto& o : def.options)
            if (taken.insert(paintmatch::normalizeText(o)).second) opts.push_back(o);

        std::vector<std::string> extras;
        for (const auto& row : db.values) {
            std::vector<std::string> found;
            collectText(row[a], found);
            for (auto& f : found)
                if (taken.insert(paintmatch::normalizeText(f)).second) extras.push_back(f);
        }
        std::sort(extras.begin(), extras.end(),
                  [](const std::string& x, const std::string& y) { return toLower(x) < toLower(y); });
        opts.insert(opts.end(), extras.begin(), extras.end());
        db.options[a] = std::move(opts);
    }

    return db;
}

Database loadDatabaseFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw LoadError("Cannot open \"" + path + "\".");
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseDatabase(ss.str(), path);
}

// ==================================================================== formatting

std::string formatNumber(double v, int maxDecimals) {
    std::string s = fixed(v, std::max(0, maxDecimals));
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

std::string formatValue(const AttributeDef& def, const Value& v) {
    if (const auto* iv = std::get_if<Interval>(&v)) {
        auto one = [&](double x) {
            if (def.parser == Parser::Currency) return "$" + fixed(x, 2);
            return formatNumber(x, std::max(def.decimals, 2));
        };
        std::string s = one(iv->lo);
        if (iv->hi != iv->lo) s += "\xE2\x80\x93" + one(iv->hi);  // en dash
        if (def.parser == Parser::Currency || def.unit.empty()) return s;
        if (def.unit == "%") return s + "%";
        return s + " " + def.unit;
    }
    if (const auto* str = std::get_if<std::string>(&v)) return *str;
    if (const auto* list = std::get_if<std::vector<std::string>>(&v)) {
        std::string s;
        for (const auto& item : *list) {
            if (!s.empty()) s += ", ";
            s += item;
        }
        return s;
    }
    return {};
}

}  // namespace paintdb
