#include "csv_reader.hpp"

namespace csv {

namespace {

std::string_view stripBom(std::string_view s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF)
        s.remove_prefix(3);
    return s;
}

}  // namespace

char detectDelimiter(std::string_view content) {
    content = stripBom(content);
    std::size_t commas = 0, semicolons = 0, tabs = 0;
    bool inQuotes = false;
    for (char c : content) {
        if (c == '"') inQuotes = !inQuotes;
        else if (!inQuotes) {
            if (c == '\n' || c == '\r') break;
            if (c == ',') ++commas;
            else if (c == ';') ++semicolons;
            else if (c == '\t') ++tabs;
        }
    }
    if (semicolons > commas && semicolons >= tabs) return ';';
    if (tabs > commas && tabs > semicolons) return '\t';
    return ',';
}

Document parse(std::string_view content) {
    content = stripBom(content);

    Document doc;
    doc.delimiter = detectDelimiter(content);
    const char delim = doc.delimiter;

    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    bool rowHasContent = false;  // distinguishes an empty line from a row of empty fields
    std::size_t line = 1;
    std::size_t rowStartLine = 1;

    auto endField = [&] {
        row.push_back(std::move(field));
        field.clear();
    };
    auto endRow = [&] {
        endField();
        if (rowHasContent || row.size() > 1 || !row.front().empty()) {
            doc.rows.push_back(std::move(row));
            doc.lineNumbers.push_back(rowStartLine);
        }
        row.clear();
        rowHasContent = false;
    };

    for (std::size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];

        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < content.size() && content[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                if (c == '\n') ++line;
                else if (c == '\r' && !(i + 1 < content.size() && content[i + 1] == '\n')) ++line;
                field.push_back(c);
            }
            continue;
        }

        if (c == '"') {
            inQuotes = true;
            rowHasContent = true;
        } else if (c == delim) {
            endField();
            rowHasContent = true;
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < content.size() && content[i + 1] == '\n') ++i;
            endRow();
            ++line;
            rowStartLine = line;
        } else {
            field.push_back(c);
            rowHasContent = true;
        }
    }

    if (inQuotes) doc.unterminatedQuote = true;
    if (rowHasContent || !field.empty() || !row.empty()) endRow();
    return doc;
}

}  // namespace csv
