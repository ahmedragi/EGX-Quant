#include "egxquant/risk/stats.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace egxq::stats {

double normal_pdf(double x) { return std::exp(-0.5 * x * x) / std::sqrt(2.0 * std::numbers::pi); }

double normal_cdf(double x) { return 0.5 * std::erfc(-x / std::numbers::sqrt2); }

double normal_quantile(double p) {
    if (!(p > 0.0 && p < 1.0)) throw std::invalid_argument("normal_quantile: p must be in (0, 1)");

    // Peter J. Acklam's algorithm.
    constexpr double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                            1.383577518672690e+02,  -3.066479806614716e+01, 2.506628277459239e+00};
    constexpr double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                            6.680131188771972e+01,  -1.328068155288572e+01};
    constexpr double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                            -2.549732539343734e+00, 4.374664141464968e+00,  2.938163982698783e+00};
    constexpr double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                            3.754408661907416e+00};
    constexpr double p_low = 0.02425;

    double x = 0.0;
    if (p < p_low) {
        const double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p <= 1.0 - p_low) {
        const double q = p - 0.5;
        const double r = q * q;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log1p(-p));
        x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }

    // One Halley step against the exact CDF.
    const double e = normal_cdf(x) - p;
    const double u = e * std::sqrt(2.0 * std::numbers::pi) * std::exp(0.5 * x * x);
    return x - u / (1.0 + 0.5 * x * u);
}

double chi2_1_sf(double x) {
    if (x <= 0.0) return 1.0;
    return std::erfc(std::sqrt(0.5 * x));
}

Moments moments(std::span<const double> x) {
    if (x.size() < 2) throw std::invalid_argument("moments: need at least 2 values");
    Moments m;
    m.n = x.size();
    const double n = static_cast<double>(x.size());

    double sum = 0.0;
    for (double v : x) sum += v;
    m.mean = sum / n;

    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    for (double v : x) {
        const double dv = v - m.mean;
        const double d2 = dv * dv;
        m2 += d2;
        m3 += d2 * dv;
        m4 += d2 * d2;
    }
    m.stdev = std::sqrt(m2 / (n - 1.0));
    m2 /= n;
    m3 /= n;
    m4 /= n;
    if (m2 > 0.0) {
        m.skewness = m3 / std::pow(m2, 1.5);
        m.excess_kurtosis = m4 / (m2 * m2) - 3.0;
    }
    const auto [lo, hi] = std::minmax_element(x.begin(), x.end());
    m.min = *lo;
    m.max = *hi;
    return m;
}

}  // namespace egxq::stats
