#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "egxquant/core/daycount.hpp"

/// Plain records for the market data the engine consumes.
///
/// Units follow Egyptian market quoting:
///   - index levels and futures prices in index points
///   - T-bill yields as simple annual percentages (22.5 means 22.5%)
///   - USD/EGP as EGP per 1 USD

namespace egxq {

/// EGX30 daily close.
struct IndexLevel {
    Date date;
    double close;
};

/// Daily settlement of one EGX30 futures contract.
struct FuturesSettlement {
    Date date;
    std::string contract;  ///< e.g. "EGX30-202703"
    Date expiry;
    double settle;
    std::int64_t volume = 0;
    std::int64_t open_interest = 0;
};

/// CBE T-bill auction result for one tenor.
struct TbillYield {
    Date date;         ///< Auction date
    int tenor_days;    ///< 91, 182, 273, 364
    double yield_pct;  ///< Simple annual yield, percent
};

/// Monthly consumer price index level.
struct CpiPoint {
    std::chrono::year_month month;
    double index;
};

/// Official USD/EGP rate (EGP per USD).
struct FxRate {
    Date date;
    double rate;
};

/// Everything the pipeline handles, in one bundle.
struct MarketData {
    std::vector<IndexLevel> index_levels;
    std::vector<FuturesSettlement> futures;
    std::vector<TbillYield> tbills;
    std::vector<CpiPoint> cpi;
    std::vector<FxRate> fx;
};

/// Format as "YYYY-MM-DD".
std::string format_date(const Date& d);

/// Format as "YYYY-MM".
std::string format_month(const std::chrono::year_month& ym);

/// Parse "YYYY-MM". Throws std::invalid_argument on bad input.
std::chrono::year_month parse_month(std::string_view s);

}  // namespace egxq
