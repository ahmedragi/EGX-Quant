#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

/// Minimal RFC 4180 CSV reader and writer.
///
/// Handles quoted fields (with embedded commas, quotes and newlines),
/// CRLF line endings and a UTF-8 byte-order mark, which covers files
/// exported from Excel and from typical data download pages.

namespace egxq::csv {

struct Row {
    std::size_t line;  ///< 1-based line number where the row starts (for error messages)
    std::vector<std::string> fields;
};

struct Table {
    std::vector<std::string> header;
    std::vector<Row> rows;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    /// Index of a header column (case-insensitive, surrounding spaces ignored),
    /// or npos when absent.
    std::size_t find_column(std::string_view name) const;

    /// Like find_column(), but throws std::runtime_error when absent.
    std::size_t column(std::string_view name) const;
};

/// Parse CSV text. The first non-empty record is the header. Blank lines are skipped.
Table parse(std::string_view text);

/// Read and parse a file. Throws std::runtime_error if it cannot be opened.
Table read_file(const std::filesystem::path& path);

/// Quote a field if it contains a comma, quote or newline.
std::string escape(std::string_view field);

/// Write one record terminated by a newline.
void write_row(std::ostream& out, const std::vector<std::string>& fields);

}  // namespace egxq::csv
