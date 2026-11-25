// Console tests for the Qt-free core (CSV reader, paint schema, matching engine).
// Usage: core_tests [path/to/paints.csv]

#include "csv_reader.hpp"
#include "paint_match.hpp"
#include "paint_schema.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

bool intervalIs(const std::optional<paintmatch::Interval>& iv, double lo, double hi) {
    return iv && near(iv->lo, lo) && near(iv->hi, hi);
}

void testCsv() {
    const std::string text = "\xEF\xBB\xBF" "a,b,c\r\n1,\"x, y\",\"he said \"\"hi\"\"\"\r\n2,\"multi\nline\",\r\n\r\n3,,\n";
    csv::Document d = csv::parse(text);
    CHECK(d.rows.size() == 4);
    CHECK(d.rows[0][0] == "a");
    CHECK(d.rows[1][1] == "x, y");
    CHECK(d.rows[1][2] == "he said \"hi\"");
    CHECK(d.rows[2][1] == "multi\nline");
    CHECK(d.rows[2].size() == 3 && d.rows[2][2].empty());
    CHECK(d.rows[3][0] == "3" && d.rows[3].size() == 3);
    CHECK(d.lineNumbers[3] == 6);
    CHECK(!d.unterminatedQuote);

    csv::Document semi = csv::parse("a;b;c\n1;2,5;3\n");
    CHECK(semi.delimiter == ';');
    CHECK(semi.rows[1][1] == "2,5");

    CHECK(csv::parse("a,\"open\n").unterminatedQuote);
}

void testParsers() {
    using namespace paintdb::parse;
    CHECK(number("$1,059.50") && near(*number("$1,059.50"), 1059.5));
    CHECK(!number("abc"));
    CHECK(near(percent("15.00%").value_or(-1), 15.0));
    CHECK(near(percent("0.15").value_or(-1), 15.0));
    CHECK(near(percent("12").value_or(-1), 12.0));

    CHECK(intervalIs(rangeValue("3-5"), 3, 5));
    CHECK(intervalIs(rangeValue("3 - 5"), 3, 5));
    CHECK(intervalIs(rangeValue("5"), 5, 5));
    CHECK(intervalIs(rangeValue("2026-03-05"), 3, 5));        // Excel date damage
    CHECK(intervalIs(rangeValue("2026-01-03 00:00:00"), 1, 3));
    CHECK(intervalIs(rangeValue("3/5/2026"), 3, 5));
    CHECK(intervalIs(rangeValue("5-Mar"), 3, 5));
    CHECK(intervalIs(rangeValue("Mar-05"), 3, 5));
    CHECK(intervalIs(rangeValue("1 to 3"), 1, 3));
    CHECK(!rangeValue("high"));

    CHECK(intervalIs(dryTime("1-2 to touch, 4 to recoat", false), 1, 2));
    CHECK(intervalIs(dryTime("1-2 to touch, 4 to recoat", true), 4, 4));
    CHECK(intervalIs(dryTime("0.25 to touch, 0.5 to recoat", true), 0.5, 0.5));
    CHECK(intervalIs(dryTime("4-6 to touch, 16 to recoat", false), 4, 6));
    CHECK(intervalIs(dryTime("30 min to touch, 2 hrs to recoat", false), 0.5, 0.5));
    CHECK(intervalIs(dryTime("30 min to touch, 2 hrs to recoat", true), 2, 2));
    CHECK(intervalIs(dryTime("Touch: 1 hr, Recoat: 4-6 hr", true), 4, 6));
    CHECK(!dryTime("overnight", true));

    CHECK(intervalIs(days("14-30 days"), 14, 30));
    CHECK(intervalIs(days("7 days"), 7, 7));
    CHECK(intervalIs(days("2 weeks"), 14, 14));

    CHECK(boolean("TRUE") == std::optional<bool>(true));
    CHECK(boolean("no") == std::optional<bool>(false));
    CHECK(!boolean("maybe"));

    auto l = list("Flat, Matte; Satin, NA, ");
    CHECK(l.size() == 3 && l[2] == "Satin");
    CHECK(isNotApplicable(" N/A "));

    CHECK(paintdb::formatNumber(2.50) == "2.5");
    CHECK(paintdb::formatNumber(3.0) == "3");
}

void testLoadErrors() {
    bool threw = false;
    try { paintdb::parseDatabase("", "empty"); } catch (const paintdb::LoadError&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { paintdb::parseDatabase("name,age\nbob,4\n", "wrong"); } catch (const paintdb::LoadError& e) {
        threw = true;
        std::printf("  (expected) %s\n", e.what());
    }
    CHECK(threw);

    // Header only + Excel's blank template rows -> no products
    threw = false;
    try {
        paintdb::parseDatabase("Brand,Line,Coating Type,Sheens,Colors,Low or Zero VOC?\n,,,,,FALSE\n,,,,,FALSE\n", "blank");
    } catch (const paintdb::LoadError&) { threw = true; }
    CHECK(threw);

    // Columns in a different order, one bad cell
    auto db = paintdb::parseDatabase(
        "Line,Brand,Lower Temperature Threshold,Sheens,Coating Type,Extra\n"
        "Aura,Benjamin Moore,abc,\"Flat, Satin\",Paint,zzz\n"
        ",,,,,\n",
        "reordered");
    CHECK(db.products.size() == 1);
    CHECK(db.blankRowsSkipped == 1);
    CHECK(db.warningCount == 1);
    CHECK(db.ignoredColumns.size() == 1 && db.ignoredColumns[0] == "Extra");
    CHECK(db.products[0].displayName() == "Benjamin Moore Aura");
}

void testEngine() {
    using namespace paintmatch;
    AttributeSpec n;
    n.name = "temp";
    n.direction = Direction::LowerIsBetter;
    n.range = 20;
    AttributeSpec c;
    c.name = "side";
    c.kind = Kind::Categorical;
    c.related = {{"interior", "both", 1.0}};
    AttributeSpec s;
    s.name = "sheen";
    s.kind = Kind::Set;
    s.levels = {"flat", "matte", "satin", "gloss"};
    s.shortfallFactor = 3.0;
    AttributeSpec col;
    col.name = "color";
    col.kind = Kind::Set;
    col.wildcards = {"Any"};

    Matcher m({n, c, s, col});
    m.setCatalog({
        {range(35, 50), text("Both"), items({"Flat"}), items({"Any"})},
        {number(50), text("Interior"), items({"Satin", "Gloss"}), items({"White"})},
        {number(40), text("Exterior"), items({"Matte"}), Value{}},
    });

    Query q;
    q.criteria["side"].target = text("Interior");
    auto r = m.search(q);
    CHECK(r.matches.size() == 3);
    CHECK(near(r.matches[0].score, 1.0) && near(r.matches[1].score, 1.0));  // Interior and Both
    CHECK(near(r.matches[2].score, 0.0));

    q.criteria["side"].required = true;
    CHECK(m.search(q).candidates == 2);

    Query t;
    t.criteria["temp"].target = number(40);  // inside 35..50 range -> perfect
    r = m.search(t);
    CHECK(r.matches[0].row == 0 || r.matches[0].row == 2);
    CHECK(near(r.matches[0].score, 1.0));

    Query b;
    b.criteria["temp"].max = 40;  // floor/ceiling only
    r = m.search(b);
    CHECK(r.candidates == 2);  // 35-50 overlaps, 40 ok, 50 excluded
    CHECK(near(r.matches[0].score, 1.0) && !r.matches[0].breakdown[0].scored);

    Query sh;
    sh.criteria["sheen"].target = items({"Matte"});
    r = m.search(sh);
    CHECK(r.matches[0].row == 2 && near(r.matches[0].score, 1.0));
    CHECK(near(r.matches[1].score, 0.0));  // 4 levels, factor 3: one step away already scores 0

    Query colq;
    colq.criteria["color"].target = items({"Black or Gray"});
    r = m.search(colq);
    CHECK(r.matches[0].row == 0 && near(r.matches[0].score, 1.0));  // "Any" wildcard

    Query bad;
    bad.criteria["temp"].min = 50;
    bad.criteria["temp"].max = 40;
    bool threw = false;
    try { m.search(bad); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    Query unknown;
    unknown.criteria["nope"].target = number(1);
    threw = false;
    try { m.search(unknown); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

void testSampleDatabase(const std::string& path) {
    paintdb::Database db;
    try {
        db = paintdb::loadDatabaseFile(path);
    } catch (const paintdb::LoadError& e) {
        std::printf("FAIL could not load %s: %s\n", path.c_str(), e.what());
        ++g_failures;
        return;
    }
    const auto& defs = paintdb::attributeDefs();
    std::printf("  loaded %zu products, %zu blank rows skipped, %zu warnings, %zu missing columns\n",
                db.products.size(), db.blankRowsSkipped, db.warningCount, db.missingColumns.size());
    for (const auto& w : db.warnings) std::printf("    warning: %s\n", w.c_str());
    CHECK(db.products.size() == 144);
    CHECK(db.missingColumns.empty());
    CHECK(db.warningCount == 0);
    for (std::size_t a = 0; a < defs.size(); ++a) {
        if (!db.attributeHasData[a]) std::printf("    no data: %s\n", defs[a].key.c_str());
        CHECK(db.attributeHasData[a]);
    }

    paintmatch::Matcher m(paintdb::engineSpecs());
    m.setCatalog(db.values);

    auto idx = [](const char* key) { return *paintdb::attributeIndex(key); };

    // Interior bathroom wall paint, satin, low temp application below 40F
    paintmatch::Query q;
    q.limit = 5;
    q.criteria["intExt"].target = paintmatch::text("Interior");
    q.criteria["intExt"].required = true;
    q.criteria["coatingType"].target = paintmatch::text("Paint");
    q.criteria["substrates"].target = paintmatch::items({"Painted sheetrock/plaster"});
    q.criteria["sheens"].target = paintmatch::items({"Satin"});
    q.criteria["bathroom"].target = paintmatch::text("Yes");
    q.criteria["lowerTemp"].target = paintmatch::number(35);
    q.criteria["lowerTemp"].max = 45;
    q.criteria["keywords"].target = paintmatch::text("mildew, scrub");
    auto r = m.search(q);

    std::printf("\n  Interior paint, painted drywall, satin, bathroom, temp target 35 (max 45), keywords:\n");
    std::printf("  %zu of %zu products pass the filters\n", r.candidates, r.catalogSize);
    for (const auto& match : r.matches) {
        const auto& p = db.products[match.row];
        std::printf("   %5.1f%%  %-45s", match.score * 100.0, p.displayName().c_str());
        for (const auto& as : match.breakdown) {
            const auto& def = defs[as.attribute];
            std::string v = paintdb::formatValue(def, db.values[match.row][as.attribute]);
            std::printf(" | %s=%s", def.key.c_str(), v.empty() ? "-" : v.c_str());
            if (as.scored) std::printf(" (%.0f%%)", as.similarity * 100.0);
        }
        std::printf("\n");

        const auto& side = std::get<std::string>(db.values[match.row][idx("intExt")]);
        CHECK(side == "Interior" || side == "Both");
        const auto& temp = std::get<paintmatch::Interval>(db.values[match.row][idx("lowerTemp")]);
        CHECK(temp.lo <= 45.0);
    }
    CHECK(!r.matches.empty());
    for (std::size_t i = 1; i < r.matches.size(); ++i) CHECK(r.matches[i - 1].score >= r.matches[i].score);

    // Empty query: everything is a candidate with score 1
    paintmatch::Query empty;
    r = m.search(empty);
    CHECK(r.candidates == db.products.size());

    // Spot-check formatting of parsed values
    const auto& first = db.values[0];
    std::printf("\n  first product parsed values:\n");
    for (std::size_t a = 0; a < defs.size(); ++a)
        std::printf("    %-32s %s\n", defs[a].label.c_str(), paintdb::formatValue(defs[a], first[a]).c_str());
    CHECK(paintdb::formatValue(defs[idx("price")], first[idx("price")]) == "$59.00");
    CHECK(paintdb::formatValue(defs[idx("barstow")], first[idx("barstow")]) == "3\xE2\x80\x93" "5");
    CHECK(paintdb::formatValue(defs[idx("humidity")], first[idx("humidity")]) == "15%");
    CHECK(paintdb::formatValue(defs[idx("recoat")], first[idx("recoat")]) == "4 h");

    // Options: workbook list first, extras from data appended
    const auto& sheenOpts = db.options[idx("sheens")];
    CHECK(sheenOpts.front() == "Flat" && sheenOpts.size() == 10);
    const auto& colorOpts = db.options[idx("colors")];
    bool hasAny = false;
    for (const auto& o : colorOpts) hasAny |= (o == "Any");
    CHECK(!hasAny);  // wildcard is not offered as a choice
}

}  // namespace

int main(int argc, char** argv) {
    const std::string csvPath = argc > 1 ? argv[1] : "data/paints.csv";
    testCsv();
    testParsers();
    testLoadErrors();
    testEngine();
    testSampleDatabase(csvPath);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
