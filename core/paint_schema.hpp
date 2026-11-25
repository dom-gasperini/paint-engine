// paint_schema.hpp - paint database layout and attribute definitions (pure C++17)
//
// Column names, option lists and sheen order come from the
// "Product Picker - Painting With Purpose" workbook (sheet "Picker"), including the
// workbook's data-validation lists. The CSV is expected to be that sheet saved as CSV
// (header row + one product per row). Columns are matched by name, so their order
// does not matter, and unknown columns are ignored.

#pragma once

#include "paint_match.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace paintdb {

// How a CSV cell is turned into an engine value.
enum class Parser {
    Text,       // single text value; "NA" / "N/A" count as missing
    List,       // comma / semicolon separated values
    Bool,       // TRUE/FALSE, Yes/No, Y/N, 1/0 -> "Yes" / "No"
    Number,     // plain number
    Currency,   // "$59.00", "59"
    Percent,    // "15%", "15.00%" or a fraction "0.15" -> 15
    Range,      // "3-5", "4", "3 to 5"; also repairs Excel dates such as 2026-03-05 -> 3-5
    DryTouch,   // "1-2 to touch, 4 to recoat" -> 1..2 hours
    DryRecoat,  // "1-2 to touch, 4 to recoat" -> 4 hours
    Days,       // "14-30 days", "7 days", "2 weeks"
    Combined    // several text columns joined (keyword search)
};

struct AttributeDef {
    std::string key;    // stable id: engine attribute name and settings key
    std::string label;  // UI label
    std::string group;  // UI section
    std::string unit;   // "$", "%", "°F", "h", "days", "sq ft/gal" or empty
    std::string hint;   // tooltip text
    std::vector<std::string> columns;  // CSV column(s) the value is read from
    Parser parser = Parser::Text;
    paintmatch::AttributeSpec spec;    // kind, direction, factory default weight, ...
    std::vector<std::string> options;  // input choices from the workbook's validation lists

    // Numeric input range for the UI
    double inputMin = 0.0;
    double inputMax = 100.0;
    double inputStep = 1.0;
    int decimals = 0;

    bool isNumeric() const { return spec.kind == paintmatch::Kind::Numeric; }
    bool isBoolean() const { return parser == Parser::Bool; }
    double defaultWeight() const { return spec.weight; }
};

// All attributes, in UI order. Index i here is attribute i in the engine and Database.
const std::vector<AttributeDef>& attributeDefs();
std::vector<paintmatch::AttributeSpec> engineSpecs();
std::optional<std::size_t> attributeIndex(std::string_view key);

// Every column of the workbook, in workbook order.
const std::vector<std::string>& knownColumns();
std::optional<std::size_t> columnIndex(std::string_view name);  // name compared loosely

namespace col {
inline constexpr std::string_view Location = "Location of Purchase";
inline constexpr std::string_view Brand = "Brand";
inline constexpr std::string_view CoatingType = "Coating Type";
inline constexpr std::string_view Line = "Line";
inline constexpr std::string_view AdvertisedPurpose = "Advertised Purpose";
inline constexpr std::string_view SpecialProperties = "Special Properties";
inline constexpr std::string_view ApplicationMethod = "Special Application Method";
inline constexpr std::string_view ProductDataSheet = "Product Data Sheet";
inline constexpr std::string_view SafetyDataSheet = "Safety Data Sheet";
inline constexpr std::string_view PictureExample = "Picture Example";
}  // namespace col

struct Product {
    std::vector<std::string> fields;  // raw text, indexed like knownColumns(); empty = missing
    std::size_t sourceLine = 0;       // 1-based line in the CSV

    const std::string& field(std::string_view column) const;
    std::string displayName() const;  // "Brand Line"
};

struct Database {
    std::string source;
    std::vector<Product> products;
    std::vector<std::vector<paintmatch::Value>> values;  // [product][attribute]
    std::vector<std::vector<std::string>> options;       // [attribute] input choices (workbook list + data)
    std::vector<bool> attributeHasData;                  // [attribute]
    std::vector<std::string> missingColumns;             // known columns absent from the file
    std::vector<std::string> ignoredColumns;             // file columns that are not known
    std::vector<std::string> warnings;                   // cell-level problems (capped)
    std::size_t warningCount = 0;                        // total, including ones not stored
    std::size_t blankRowsSkipped = 0;
};

class LoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parse CSV text (UTF-8). Throws LoadError if it is empty or does not look like the paint database.
Database parseDatabase(std::string_view csvContent, const std::string& sourceName);

// Convenience for tools and tests (narrow path; the Qt app reads the file itself).
Database loadDatabaseFile(const std::string& path);

// Display helpers
std::string formatNumber(double v, int maxDecimals = 2);
std::string formatValue(const AttributeDef& def, const paintmatch::Value& v);  // "" when missing

// Cell parsers, exposed for tests
namespace parse {
std::optional<double> number(std::string_view s);
std::optional<double> percent(std::string_view s);
std::optional<paintmatch::Interval> rangeValue(std::string_view s);
std::optional<paintmatch::Interval> dryTime(std::string_view s, bool recoat);
std::optional<paintmatch::Interval> days(std::string_view s);
std::optional<bool> boolean(std::string_view s);
std::vector<std::string> list(std::string_view s);
std::string trimmed(std::string_view s);
bool isNotApplicable(std::string_view s);  // "", "NA", "N/A", "None", "-"
}  // namespace parse

}  // namespace paintdb
