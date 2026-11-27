#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

/// Portable random numbers. mt19937_64 is fully specified by the standard; the
/// conversions to uniform, normal and Poisson below are our own (the standard
/// distributions are implementation-defined), so a seed gives the same stream
/// on every compiler and platform.

namespace egxq::risk {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : eng_(seed) {}

    /// Uniform in the open interval (0, 1).
    double uniform() { return (static_cast<double>(eng_() >> 11) + 0.5) * 0x1.0p-53; }

    /// Standard normal via Box-Muller.
    double normal() {
        if (has_spare_) {
            has_spare_ = false;
            return spare_;
        }
        const double u1 = uniform(), u2 = uniform();
        const double r = std::sqrt(-2.0 * std::log(u1));
        const double theta = 2.0 * 3.14159265358979323846 * u2;
        spare_ = r * std::sin(theta);
        has_spare_ = true;
        return r * std::cos(theta);
    }

    /// Poisson(mean) by inversion of the CDF; fine for the small means of jump
    /// counts. Throws for a mean above 500 (e^-mean would underflow).
    unsigned poisson(double mean) {
        if (mean < 0.0 || mean > 500.0) throw std::invalid_argument("poisson: mean must be in [0, 500]");
        if (mean == 0.0) return 0;
        const double u = uniform();
        double p = std::exp(-mean);
        double cdf = p;
        unsigned k = 0;
        while (u > cdf && k < 10000) {
            ++k;
            p *= mean / static_cast<double>(k);
            cdf += p;
        }
        return k;
    }

private:
    std::mt19937_64 eng_;
    double spare_ = 0.0;
    bool has_spare_ = false;
};

}  // namespace egxq::risk
