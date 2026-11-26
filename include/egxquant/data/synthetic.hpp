#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "egxquant/data/market_data.hpp"

/// Synthetic EGX market data for building and testing the pipeline before real
/// data is available. These are NOT real prices.
///
/// The generator gives the same output for a given seed on every platform
/// (it avoids the standard library distributions, whose output varies by vendor).
///
/// Model:
///   - Trading days are Sunday to Thursday (the EGX week); holidays are ignored.
///   - EGX30 follows geometric Brownian motion.
///   - USD/EGP drifts slowly with occasional large devaluation jumps.
///   - The CBE policy rate moves in 100 bp steps at meetings about every six
///     weeks. T-bill yields sit above it by tenor, with auction noise.
///     Auctions: 91 and 273 days on Thursdays, 182 and 364 days on Sundays.
///   - CPI grows monthly, faster in the months after a devaluation.
///   - Futures follow the real EGX30 contract cycle (futures/contract_spec.hpp):
///     two quarterly contracts expiring on the third Wednesday, settling at
///     cost-of-carry fair value plus noise.

namespace egxq {

struct SyntheticConfig {
    Date start{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{1}};
    Date end{std::chrono::year{2026}, std::chrono::month{11}, std::chrono::day{25}};
    Date futures_start{std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{1}};
    std::uint64_t seed = 42;

    double index_start = 25000.0;
    double index_drift = 0.20;      ///< Annual drift of EGX30 (nominal EGP)
    double index_vol = 0.22;        ///< Annual volatility
    double dividend_yield = 0.03;   ///< Continuous, used to price futures

    double fx_start = 30.9;         ///< EGP per USD
    double fx_daily_vol = 0.002;
    double fx_jump_prob = 0.003;    ///< Chance of a devaluation per trading day
    double fx_jump_mean = 0.25;     ///< Average devaluation size (fraction)

    double policy_rate_start = 27.25;  ///< Percent
    double cpi_start = 100.0;
    double futures_noise = 0.004;   ///< Settlement noise around fair value (fraction)
};

/// Is this an EGX trading weekday (Sunday to Thursday)?
bool is_egx_weekday(const Date& d);

MarketData generate_synthetic(const SyntheticConfig& cfg);

}  // namespace egxq
