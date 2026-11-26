#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// Minimal HTML table reader: the text of every <table> in a page.
///
/// Scripts, styles and comments are skipped; entities are decoded; whitespace
/// inside a cell is collapsed to single spaces. Nested tables are returned as
/// tables of their own, and their text also counts toward the enclosing cell
/// (so a cell holding a nested table reads as that table's text).

namespace egxq::html {

struct Table {
    std::string id;                              ///< The table's id attribute, if any
    std::vector<std::vector<std::string>> rows;  ///< Cell texts (td and th), row by row
};

/// All tables in document order (an outer table before the tables inside it).
std::vector<Table> read_tables(std::string_view html);

/// Read a saved page from disk and return its tables.
std::vector<Table> read_tables_file(const std::filesystem::path& path);

}  // namespace egxq::html
