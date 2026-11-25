#include "egxquant/futures/basis.hpp"

#include <cmath>
#include <stdexcept>

namespace egxq {

ArbitrageBounds arbitrage_bounds(double spot, double dividend_yield,
                                 double time_to_expiry, const Frictions& fr) {
    if (!(spot > 0.0)) throw std::invalid_argument("spot must be > 0");
    if (!(time_to_expiry >= 0.0)) throw std::invalid_argument("time_to_expiry must be >= 0");
    if (fr.spot_cost < 0.0 || fr.futures_cost < 0.0 || fr.spot_cost >= 1.0 || fr.futures_cost >= 1.0)
        throw std::invalid_argument("transaction costs must be in [0, 1)");

    const double T = time_to_expiry;
    const double upper = spot * (1.0 + fr.spot_cost)
                       * std::exp((fr.borrow_rate - dividend_yield) * T)
                       * (1.0 + fr.futures_cost);
    const double lower = spot * (1.0 - fr.spot_cost)
                       * std::exp((fr.lend_rate - dividend_yield) * T)
                       * (1.0 - fr.futures_cost);
    return {lower, upper};
}

BasisReport analyze_basis(const CarryInputs& in, double market_price,
                          const Frictions& fr) {
    if (!(market_price > 0.0)) throw std::invalid_argument("market_price must be > 0");

    BasisReport r{};
    r.fair_value        = fair_value(in);
    r.market_price      = market_price;
    r.basis             = market_price - in.spot;
    r.theoretical_basis = r.fair_value - in.spot;
    r.mispricing        = market_price - r.fair_value;
    r.mispricing_pct    = r.mispricing / r.fair_value;
    r.annualized_basis  = in.time_to_expiry > 0.0
                        ? std::log(market_price / in.spot) / in.time_to_expiry
                        : 0.0;
    r.bounds = arbitrage_bounds(in.spot, in.dividend_yield, in.time_to_expiry, fr);

    if (market_price > r.bounds.upper) {
        r.signal = Signal::CashAndCarry;
        r.edge   = market_price - r.bounds.upper;
    } else if (market_price < r.bounds.lower) {
        r.signal = Signal::ReverseCashAndCarry;
        r.edge   = r.bounds.lower - market_price;
    } else {
        r.signal = Signal::None;
        r.edge   = 0.0;
    }
    return r;
}

std::string_view to_string(Signal s) {
    switch (s) {
        case Signal::None:                return "NONE";
        case Signal::CashAndCarry:        return "CASH_AND_CARRY";
        case Signal::ReverseCashAndCarry: return "REVERSE_CASH_AND_CARRY";
    }
    return "UNKNOWN";
}

}  // namespace egxq
