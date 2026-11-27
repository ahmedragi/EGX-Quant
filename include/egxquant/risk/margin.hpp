#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "egxquant/risk/jump_diffusion.hpp"

/// Futures margining: daily settlement prices under the exchange's price
/// limit, and the margin account they drive.
///
/// The account opens with the initial margin for every contract (plus any
/// excess cash). Each day the variation margin, contracts * multiplier *
/// (settlement change), is credited or debited. When the balance falls below
/// the maintenance margin, a margin call tops it back up to the initial margin.

namespace egxq::risk {

struct MarginRules {
    double initial = 5900.0;  ///< EGP per contract (EGX bulletin, 26 Nov 2026, March 2027 contract)
    double maintenance = 4425.0;  ///< EGP per contract; an assumption (75% of initial), not an EGX figure
    double price_limit = 0.10;  ///< Maximum daily settlement move (EGX specs: +-10%)
};

/// Settlement prices that follow a "fair" path start * exp(cumulative log
/// return) but never move more than `limit` from the previous settlement; a
/// move that does not fit carries over to the next day. Returns one price per
/// return (the start price is not included). `locked`, if given, receives
/// true for each day the limit bound.
std::vector<double> limit_settlements(double start, std::span<const double> log_returns, double limit,
                                      std::vector<bool>* locked = nullptr);

struct MarginDay {
    double settle = 0.0;
    double variation = 0.0;  ///< Credited (+) or debited (-) today
    double balance_before_call = 0.0;
    double call = 0.0;  ///< Cash added to restore the initial margin
    double balance = 0.0;  ///< After any call
};

struct MarginRun {
    std::vector<MarginDay> days;
    std::size_t calls = 0;
    double total_called = 0.0;
    double max_call = 0.0;
    double min_balance = 0.0;  ///< Lowest balance before a call; < 0 means a loss beyond the deposit
    double pnl = 0.0;  ///< Total variation margin
};

/// Run the account over settlement prices. `settlements[0]` is the entry
/// price. `contracts` is signed: positive long, negative short.
MarginRun run_margin(std::span<const double> settlements, double contracts, const MarginRules& rules,
                     double multiplier, double excess_cash = 0.0);

/// Margin-call statistics over simulated EGX30 paths (futures moves = index moves).
struct MarginRisk {
    std::size_t paths = 0;
    std::size_t days = 0;
    double p_call = 0.0;  ///< P(at least one margin call)
    double mean_calls = 0.0;
    double called_mean = 0.0;  ///< Mean total cash called
    double called_q95 = 0.0;  ///< 95th percentile of total cash called
    double called_q99 = 0.0;
    double p_deficit = 0.0;  ///< P(a day's loss exceeds the whole balance)
    double p_locked = 0.0;  ///< P(the price limit binds at least once)
};

MarginRisk margin_monte_carlo(const EgxFxModel& model, std::size_t days, std::size_t paths, std::uint64_t seed,
                              double contracts, double futures_price, const MarginRules& rules, double multiplier,
                              double excess_cash = 0.0, double days_per_year = 250.0);

}  // namespace egxq::risk
