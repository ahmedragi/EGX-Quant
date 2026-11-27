#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "egxquant/risk/margin.hpp"

/// Stress replay: run today's position through a historical (or hypothetical)
/// sequence of daily EGX30 and USD/EGP moves.
///
/// The position is an optional basket of EGX30 stocks plus futures. Futures
/// follow the index moves (carry is ignored over a short stress window) but
/// settle within the daily price limit, so a crash beyond the limit is
/// realised over several days and margin calls arrive day by day.
///
/// Wealth in EGP = basket + margin deposit + futures P&L; margin calls are
/// transfers from the investor's other cash, not gains or losses. Wealth in USD
/// converts at each day's USD/EGP rate, so a futures hedge protects pounds but
/// not dollars: the margin deposit and the basket both lose value in a devaluation.

namespace egxq::risk {

struct StressPosition {
    double basket_egp = 0.0;  ///< Value of a stock basket tracking the EGX30 (0 = none)
    double contracts = 0.0;  ///< Signed: negative = short futures (the hedge)
    double futures_price = 0.0;
    double fx = 0.0;  ///< USD/EGP at the start
    double excess_cash = 0.0;  ///< Extra cash in the margin account
};

struct StressDay {
    double index_level = 1.0;  ///< EGX30 relative to the start
    double fx = 0.0;
    double settle = 0.0;
    bool limit_locked = false;
    double margin_call = 0.0;
    double pnl_egp = 0.0;  ///< Cumulative, whole position
    double pnl_usd = 0.0;  ///< Cumulative, whole position, in USD
    double unhedged_pnl_egp = 0.0;  ///< Basket alone
    double unhedged_pnl_usd = 0.0;
};

struct StressResult {
    std::vector<StressDay> days;
    MarginRun margin;
    double worst_pnl_egp = 0.0;  ///< Lowest cumulative P&L along the path
    double worst_pnl_usd = 0.0;
    std::size_t locked_days = 0;
    double start_wealth_egp = 0.0;
    double start_wealth_usd = 0.0;
};

/// Replay daily log returns (same length) of the EGX30 and USD/EGP.
StressResult stress_replay(std::span<const double> index_log, std::span<const double> fx_log,
                           const StressPosition& pos, const MarginRules& rules, double multiplier);

}  // namespace egxq::risk
