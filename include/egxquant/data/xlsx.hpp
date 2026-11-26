#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "egxquant/core/daycount.hpp"

/// Minimal read-only .xlsx reader: cell values of every worksheet as text.
///
/// Shared strings and inline strings are resolved; numbers come back as
/// written in the file (dates are Excel serial numbers, see excel_serial_to_date).
/// Formulas are returned as their cached value. Styles are ignored.

namespace egxq::xlsx {

struct SheetRow {
    std::size_t number;              ///< 1-based row number in the sheet
    std::vector<std::string> cells;  ///< Index 0 is column A; missing cells are ""
};

struct Sheet {
    std::string name;
    std::vector<SheetRow> rows;  ///< Only rows present in the file, in order
};

/// Read all worksheets in workbook order. Throws std::runtime_error on a
/// missing file or a file that is not a readable .xlsx workbook.
std::vector<Sheet> read_workbook(const std::filesystem::path& path);

/// Convert an Excel serial date (1900 date system) to a calendar date.
/// Serial 46350 is 2026-11-24. Fractions (time of day) are dropped.
/// Throws std::invalid_argument for serials before 1900-03-01 (61).
Date excel_serial_to_date(double serial);

/// Zero-based column index from a cell reference such as "B12" (-> 1).
std::size_t column_index(std::string_view cell_ref);

}  // namespace egxq::xlsx
