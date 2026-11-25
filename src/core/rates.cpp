#include "egxquant/core/rates.hpp"

#include <cmath>
#include <stdexcept>

namespace egxq {

double simple_to_continuous(double simple_rate, double t) {
    if (t <= 0.0) throw std::invalid_argument("simple_to_continuous: t must be > 0");
    const double growth = 1.0 + simple_rate * t;
    if (growth <= 0.0) throw std::invalid_argument("simple_to_continuous: 1 + r*t must be > 0");
    return std::log(growth) / t;
}

double continuous_to_simple(double continuous_rate, double t) {
    if (t <= 0.0) throw std::invalid_argument("continuous_to_simple: t must be > 0");
    return std::expm1(continuous_rate * t) / t;
}

double annual_to_continuous(double annual_rate) {
    if (annual_rate <= -1.0) throw std::invalid_argument("annual_to_continuous: rate must be > -100%");
    return std::log1p(annual_rate);
}

double continuous_to_annual(double continuous_rate) {
    return std::expm1(continuous_rate);
}

double discount_factor(double continuous_rate, double t) {
    return std::exp(-continuous_rate * t);
}

}  // namespace egxq
