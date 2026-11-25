#pragma once

#include <string_view>

#include "egxquant/futures/cost_of_carry.hpp"

/// Basis analysis and no-arbitrage bounds with frictions.

namespace egxq {

/// Market frictions that widen the no-arbitrage band around fair value.
struct Frictions {
    double borrow_rate;       ///< Rate to fund a long spot position (cont. comp.)
    double lend_rate;         ///< Rate earned on short-sale proceeds (cont. comp.)
    double spot_cost = 0.0;   ///< Round-trip spot cost as a fraction (e.g. 0.002)
    double futures_cost = 0.0;///< Round-trip futures cost as a fraction of price
};

struct ArbitrageBounds {
    double lower;  ///< Below this: reverse cash-and-carry is profitable
    double upper;  ///< Above this: cash-and-carry is profitable
};

enum class Signal {
    None,               ///< Market price inside the band
    CashAndCarry,       ///< Futures rich: buy spot, sell futures
    ReverseCashAndCarry ///< Futures cheap: short spot, buy futures
};

struct BasisReport {
    double fair_value;
    double market_price;
    double basis;              ///< F_market - S
    double theoretical_basis;  ///< F_fair - S
    double mispricing;         ///< F_market - F_fair (points)
    double mispricing_pct;     ///< mispricing / F_fair
    double annualized_basis;   ///< ln(F_market / S) / T  (implied carry)
    ArbitrageBounds bounds;
    Signal signal;
    double edge;               ///< Points beyond the violated bound, 0 if None
};

/// No-arbitrage band:
///   upper = S * (1 + c_s) * exp((r_borrow - q) T) * (1 + c_f)
///   lower = S * (1 - c_s) * exp((r_lend   - q) T) * (1 - c_f)
ArbitrageBounds arbitrage_bounds(double spot, double dividend_yield,
                                 double time_to_expiry, const Frictions& fr);

/// Full basis analysis of an observed futures price against fair value.
BasisReport analyze_basis(const CarryInputs& in, double market_price,
                          const Frictions& fr);

std::string_view to_string(Signal s);

}  // namespace egxq
