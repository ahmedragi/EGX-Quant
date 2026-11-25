#pragma once

#include <vector>

/// Cost-of-carry fair value for equity index futures (e.g. EGX30).
///
/// No-arbitrage argument: buying the index today and holding it to expiry
/// costs the financing rate r and earns the dividend yield q, so
///
///     F = S * exp((r - q) * T)
///
/// All rates are continuously compounded; T is in years.

namespace egxq {

struct CarryInputs {
    double spot;            ///< Index level S (points)
    double rate;            ///< Risk-free rate r, continuously compounded
    double dividend_yield;  ///< Dividend yield q, continuously compounded
    double time_to_expiry;  ///< T in years, >= 0
};

/// A known cash dividend (in index points) paid at time t (years from now).
struct Dividend {
    double time;
    double amount;
};

/// F = S * exp((r - q) * T)
double fair_value(const CarryInputs& in);

/// Discrete-dividend version: F = (S - PV(divs)) * exp(r * T).
/// Only dividends with 0 <= time <= T are included.
double fair_value_discrete(double spot, double rate, double time_to_expiry,
                           const std::vector<Dividend>& dividends);

/// Rate implied by an observed futures price, given S, q and T:
///   r_implied = ln(F / S) / T + q
double implied_rate(double futures_price, double spot,
                    double dividend_yield, double time_to_expiry);

/// Dividend yield implied by an observed futures price, given S, r and T:
///   q_implied = r - ln(F / S) / T
double implied_dividend_yield(double futures_price, double spot,
                              double rate, double time_to_expiry);

/// Contract notional in EGP = futures price (points) * multiplier (EGP/point).
double contract_notional(double futures_price, double multiplier);

}  // namespace egxq
