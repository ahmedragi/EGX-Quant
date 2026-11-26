#pragma once

#include <cstddef>
#include <span>

/// Small statistics toolkit for the risk modules: the normal distribution,
/// sample moments and the chi-square(1) tail used by VaR backtests.

namespace egxq::stats {

double normal_pdf(double x);
double normal_cdf(double x);

/// Inverse of the standard normal CDF (Acklam's rational approximation with
/// one Halley refinement step; accurate to about 1e-15). Throws unless 0 < p < 1.
double normal_quantile(double p);

/// P(X > x) for X ~ chi-square with 1 degree of freedom.
double chi2_1_sf(double x);

struct Moments {
    std::size_t n = 0;
    double mean = 0.0;
    double stdev = 0.0;            ///< Sample standard deviation (n - 1)
    double skewness = 0.0;         ///< m3 / m2^1.5 (population moments)
    double excess_kurtosis = 0.0;  ///< m4 / m2^2 - 3 (population moments)
    double min = 0.0;
    double max = 0.0;
};

/// Moments of a sample. Throws if it has fewer than 2 values.
Moments moments(std::span<const double> x);

}  // namespace egxq::stats
