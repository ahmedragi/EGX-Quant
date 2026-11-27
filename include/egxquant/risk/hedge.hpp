#pragma once

#include <cstddef>
#include <span>

/// Minimum-variance hedging of a portfolio with EGX30 futures.
///
/// Regressing the portfolio's daily log returns on the hedge instrument's gives
/// beta = cov(p, h) / var(h), which is the hedge ratio that minimises the variance
/// of p - beta * h. Its R^2 is the hedge effectiveness: the share of the variance
/// that the hedge removes.

namespace egxq::risk {

struct HedgeFit {
    std::size_t n = 0;  ///< Paired daily returns used
    double beta = 0.0;  ///< Minimum-variance hedge ratio
    double alpha = 0.0;  ///< Daily intercept
    double correlation = 0.0;
    double r_squared = 0.0;  ///< Hedge effectiveness
    double portfolio_vol = 0.0;  ///< Annualised
    double hedge_vol = 0.0;  ///< Annualised
    double hedged_vol = 0.0;  ///< Annualised vol of p - beta * h
};

/// Fit on paired daily log returns. Throws on length mismatch, fewer than 3
/// pairs or a constant hedge series.
HedgeFit fit_hedge(std::span<const double> portfolio_log, std::span<const double> hedge_log,
                   double days_per_year = 250.0);

/// Futures contracts to sell to hedge `portfolio_value` (EGP) with the given
/// beta: beta * value / (futures price * multiplier). Not rounded.
double hedge_contracts(double portfolio_value, double beta, double futures_price, double multiplier);

}  // namespace egxq::risk
