#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "egxquant/data/market_data.hpp"

/// Read and write the pipeline CSV formats.
///
/// Each file needs a header row. Columns are matched by name (any order,
/// case-insensitive) and extra columns are ignored.
///
///   egx30.csv     date,close
///   futures.csv   date,contract,expiry,settle[,volume][,open_interest]
///   tbills.csv    date,tenor_days,yield_pct
///   cpi.csv       month,index                (month as YYYY-MM)
///   usdegp.csv    date,rate
///
/// Dates are ISO (YYYY-MM-DD). Numbers may contain thousands separators
/// ("41,500.25"). Errors name the file and line.

namespace egxq {

/// Default file name of each dataset inside a data directory.
namespace files {
inline constexpr const char* kIndex = "egx30.csv";
inline constexpr const char* kFutures = "futures.csv";
inline constexpr const char* kTbills = "tbills.csv";
inline constexpr const char* kCpi = "cpi.csv";
inline constexpr const char* kFx = "usdegp.csv";
}  // namespace files

std::vector<IndexLevel> read_index_csv(const std::filesystem::path& path);
std::vector<FuturesSettlement> read_futures_csv(const std::filesystem::path& path);
std::vector<TbillYield> read_tbills_csv(const std::filesystem::path& path);
std::vector<CpiPoint> read_cpi_csv(const std::filesystem::path& path);
std::vector<FxRate> read_fx_csv(const std::filesystem::path& path);

void write_index_csv(const std::filesystem::path& path, const std::vector<IndexLevel>& rows);
void write_futures_csv(const std::filesystem::path& path, const std::vector<FuturesSettlement>& rows);
void write_tbills_csv(const std::filesystem::path& path, const std::vector<TbillYield>& rows);
void write_cpi_csv(const std::filesystem::path& path, const std::vector<CpiPoint>& rows);
void write_fx_csv(const std::filesystem::path& path, const std::vector<FxRate>& rows);

/// Write all five files into a directory (created if needed).
void write_market_dir(const std::filesystem::path& dir, const MarketData& data);

/// Read whichever of the five files exist in a directory.
MarketData read_market_dir(const std::filesystem::path& dir);

/// Parse a number, accepting thousands separators and surrounding spaces.
/// Throws std::invalid_argument on bad input.
double parse_number(std::string_view s);

}  // namespace egxq
