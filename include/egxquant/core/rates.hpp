#pragma once

/// Interest-rate conventions.
///
/// Every pricing function in this library works with *continuously
/// compounded* rates. Egyptian market rates (CBE T-bill yields, the
/// overnight rate) are quoted as simple annual money-market yields, so
/// convert them here first.

namespace egxq {

/// Simple (money-market) rate over a period of length t (years)
/// to the equivalent continuously compounded rate.
///   (1 + r_s * t) = exp(r_c * t)
double simple_to_continuous(double simple_rate, double t);

/// Continuously compounded rate to the equivalent simple rate over t.
double continuous_to_simple(double continuous_rate, double t);

/// Annually compounded rate to continuously compounded.
///   (1 + r_a) = exp(r_c)
double annual_to_continuous(double annual_rate);

/// Continuously compounded rate to annually compounded.
double continuous_to_annual(double continuous_rate);

/// Discount factor exp(-r * t) for a continuously compounded rate.
double discount_factor(double continuous_rate, double t);

}  // namespace egxq
