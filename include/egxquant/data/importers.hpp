#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "egxquant/data/market_data.hpp"

/// Converters from third-party export formats into the pipeline records.

namespace egxq {

/// A dated price taken from a third-party export.
struct DatedPrice {
    Date date;
    double price;
};

/// Investing.com "Historical Data" CSV export, e.g. "EGX 30 Historical Data.csv"
/// or "USD_EGP Historical Data.csv":
///
///     "Date","Price","Open","High","Low","Vol.","Change %"
///     "11/25/2026","50,538.66",...
///
/// Uses the Date and Price (closing) columns. Dates may be MM/DD/YYYY (the
/// English export), DD/MM/YYYY (detected when a first part is above 12),
/// "Nov 25, 2026", or ISO. If every date is ambiguous (both parts at most 12),
/// MM/DD is assumed. Returned in ascending date order; a repeated date throws.
std::vector<DatedPrice> read_investing_history(const std::filesystem::path& path);

std::vector<IndexLevel> to_index_levels(const std::vector<DatedPrice>& prices);
std::vector<FxRate> to_fx_rates(const std::vector<DatedPrice>& prices);

/// Result of merging new rows into an existing series.
template <typename Row>
struct MergeResult {
    std::vector<Row> rows;  ///< Ascending by date
    std::size_t added = 0;    ///< Dates not present before
    std::size_t changed = 0;  ///< Dates present before with a different value
};

/// Merge `update` into `base` by date; `update` wins where both have a date.
MergeResult<IndexLevel> merge_by_date(const std::vector<IndexLevel>& base, const std::vector<IndexLevel>& update);
MergeResult<FxRate> merge_by_date(const std::vector<FxRate>& base, const std::vector<FxRate>& update);

/// Merge T-bill yields keyed by (date, tenor); `update` wins. Sorted by date, then tenor.
MergeResult<TbillYield> merge_by_date(const std::vector<TbillYield>& base, const std::vector<TbillYield>& update);

/// Merge futures settlements keyed by (date, contract); `update` wins. Sorted by date, then expiry.
MergeResult<FuturesSettlement> merge_by_date(const std::vector<FuturesSettlement>& base,
                                             const std::vector<FuturesSettlement>& update);

/// EGX30 rows read from one day's EGX derivatives bulletin.
struct FuturesBulletin {
    std::vector<FuturesSettlement> rows;  ///< EGX30 contracts, ordered by expiry
    std::size_t other_contracts = 0;      ///< Other futures in the table (single stocks), ignored
    std::size_t no_settlement = 0;        ///< EGX30 rows with no settlement price, skipped
};

/// EGX "Derivatives Market Watch" page saved as HTML (Arabic or English).
///
/// Columns are found by header name. Uses the contract name ("EGX30 Index
/// Futures - December 2026") or code for the contract, "closing and settlement
/// price" for the settlement, and the trading volume. Open interest is not
/// published there, so it is left at 0. The page does not reliably show its
/// own date, so the trading date is passed in.
///
/// Each row is checked against the exchange's 10% daily price limit (settlement
/// vs the previous day's settlement) and against traded value / volume, which
/// catches misplaced decimal points.
FuturesBulletin read_egx_futures_bulletin_html(const std::filesystem::path& path, const Date& trade_date);

/// If `day` (one trading day's settlements) is identical, contract for contract
/// in settlement and volume, to the latest earlier day in `existing`, return that
/// earlier date. On a holiday the EGX page keeps showing the last trading day,
/// so an identical bulletin means it was saved on a non-trading day.
std::optional<Date> repeats_previous_day(const std::vector<FuturesSettlement>& existing,
                                         const std::vector<FuturesSettlement>& day);

/// Result of reading a T-bill auction workbook.
struct TbillImport {
    std::vector<TbillYield> rows;  ///< Sorted by date, then tenor
    std::vector<int> tenors;       ///< Tenors found, ascending
    std::size_t skipped = 0;       ///< Data rows without a yield (e.g. auction cancelled)
};

/// EGP T-bill auction workbook (.xlsx) with one sheet per tenor, in the layout
///
///     Tenor (days) | ISIN | Issue Date | Maturity Date | ... | Weighted Avg. Yield (%)
///
/// The header row is found by name on each sheet (title rows above it are
/// skipped); sheets without these columns are ignored. Dates may be Excel
/// serials or dd/MM/yyyy text.
///
/// The ISSUE date is stored as the date: the auction result is certainly
/// public by then, so using it avoids look-ahead in backtests (the auction
/// itself is a day or two earlier). Each row is checked so that maturity minus
/// issue is within 7 days of the tenor.
TbillImport read_tbill_auctions_xlsx(const std::filesystem::path& path);

}  // namespace egxq
