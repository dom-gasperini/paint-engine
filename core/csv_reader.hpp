// csv_reader.hpp - small RFC 4180 CSV parser (pure C++17)
//
// Handles quoted fields, doubled quotes ("") inside quotes, embedded line breaks,
// CRLF / LF / CR line endings and a leading UTF-8 byte-order mark.
// The delimiter is detected from the header line: ',', ';' (Excel in many European
// locales) or tab.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace csv {

struct Document {
    std::vector<std::vector<std::string>> rows;  // rows[0] is the header when present
    std::vector<std::size_t> lineNumbers;        // 1-based source line where each row starts
    char delimiter = ',';
    bool unterminatedQuote = false;              // file ended inside a quoted field
};

char detectDelimiter(std::string_view content);
Document parse(std::string_view content);

}  // namespace csv
