#include "egxquant/futures/cost_of_carry.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace egxq {

namespace {
void require_positive(double v, const char* what) {
    if (!(v > 0.0)) throw std::invalid_argument(std::string(what) + " must be > 0");
}
void require_non_negative(double v, const char* what) {
    if (!(v >= 0.0)) throw std::invalid_argument(std::string(what) + " must be >= 0");
}
}  // namespace

double fair_value(const CarryInputs& in) {
    require_positive(in.spot, "spot");
    require_non_negative(in.time_to_expiry, "time_to_expiry");
    return in.spot * std::exp((in.rate - in.dividend_yield) * in.time_to_expiry);
}

double fair_value_discrete(double spot, double rate, double time_to_expiry,
                           const std::vector<Dividend>& dividends) {
    require_positive(spot, "spot");
    require_non_negative(time_to_expiry, "time_to_expiry");

    double pv_divs = 0.0;
    for (const auto& d : dividends) {
        if (d.time >= 0.0 && d.time <= time_to_expiry) {
            pv_divs += d.amount * std::exp(-rate * d.time);
        }
    }
    const double adjusted = spot - pv_divs;
    if (adjusted <= 0.0) throw std::invalid_argument("PV of dividends exceeds spot");
    return adjusted * std::exp(rate * time_to_expiry);
}

double implied_rate(double futures_price, double spot,
                    double dividend_yield, double time_to_expiry) {
    require_positive(futures_price, "futures_price");
    require_positive(spot, "spot");
    require_positive(time_to_expiry, "time_to_expiry");
    return std::log(futures_price / spot) / time_to_expiry + dividend_yield;
}

double implied_dividend_yield(double futures_price, double spot,
                              double rate, double time_to_expiry) {
    require_positive(futures_price, "futures_price");
    require_positive(spot, "spot");
    require_positive(time_to_expiry, "time_to_expiry");
    return rate - std::log(futures_price / spot) / time_to_expiry;
}

double contract_notional(double futures_price, double multiplier) {
    return futures_price * multiplier;
}

}  // namespace egxq
