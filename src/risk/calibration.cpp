#include "egxquant/risk/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "egxquant/risk/stats.hpp"

namespace egxq::risk {

namespace {

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const auto mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    const double hi = v[mid];
    if (v.size() % 2 == 1) return hi;
    const double lo = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid));
    return 0.5 * (lo + hi);
}

double mean(std::span<const double> v) {
    double s = 0.0;
    for (double x : v) s += x;
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}

/// Sample standard deviation; 0 for fewer than two values.
double stdev(std::span<const double> v) { return v.size() < 2 ? 0.0 : stats::moments(v).stdev; }

double correlation(std::span<const double> a, std::span<const double> b) {
    if (a.size() != b.size() || a.size() < 2) return 0.0;
    const double ma = mean(a), mb = mean(b);
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0.0 && sbb > 0.0 ? sab / std::sqrt(saa * sbb) : 0.0;
}

void check_inputs(std::size_t n, double days_per_year) {
    if (n < 3) throw std::invalid_argument("calibration: need at least 3 daily returns");
    if (!(days_per_year > 0.0)) throw std::invalid_argument("calibration: days_per_year must be positive");
}

}  // namespace

FactorFit fit_jump_diffusion(std::span<const double> daily_log, double days_per_year, const JumpRule& rule,
                             std::span<const std::size_t> exclude) {
    check_inputs(daily_log.size(), days_per_year);
    std::vector<bool> excluded(daily_log.size(), false);
    for (auto i : exclude)
        if (i < excluded.size()) excluded[i] = true;

    std::vector<double> candidates;
    for (std::size_t i = 0; i < daily_log.size(); ++i)
        if (!excluded[i]) candidates.push_back(daily_log[i]);
    const double med = median(candidates);
    std::vector<double> dev;
    for (double r : candidates) dev.push_back(std::fabs(r - med));
    const double robust_vol = 1.4826 * median(dev);

    FactorFit fit;
    fit.threshold = std::max(rule.jump_sigmas * robust_vol, rule.min_jump);
    std::vector<double> jumps, normal_days;
    for (std::size_t i = 0; i < daily_log.size(); ++i) {
        if (excluded[i]) continue;
        if (std::fabs(daily_log[i] - med) > fit.threshold) {
            fit.jump_days.push_back(i);
            jumps.push_back(daily_log[i]);
        } else {
            normal_days.push_back(daily_log[i]);
        }
    }

    const double years = static_cast<double>(daily_log.size()) / days_per_year;
    fit.model.drift = mean(daily_log) * days_per_year;
    fit.model.sigma = stdev(normal_days) * std::sqrt(days_per_year);
    fit.model.jump_rate = static_cast<double>(jumps.size()) / years;
    fit.model.jump_mean = mean(jumps);
    fit.model.jump_stdev = stdev(jumps);
    return fit;
}

EgxFxFit fit_egx_fx(std::span<const double> index_log, std::span<const double> fx_log, double days_per_year,
                    const JumpRule& index_rule, const JumpRule& fx_rule) {
    if (index_log.size() != fx_log.size()) throw std::invalid_argument("fit_egx_fx: series lengths differ");
    check_inputs(index_log.size(), days_per_year);

    EgxFxFit out;
    out.fx = fit_jump_diffusion(fx_log, days_per_year, fx_rule);
    out.index = fit_jump_diffusion(index_log, days_per_year, index_rule, out.fx.jump_days);

    // Pass-through: EGX30 move on FX jump days, regressed on the FX jump.
    double sxy = 0.0, sxx = 0.0;
    for (auto i : out.fx.jump_days) {
        sxy += index_log[i] * fx_log[i];
        sxx += fx_log[i] * fx_log[i];
    }
    out.model.passthrough = sxx > 0.0 ? sxy / sxx : 0.0;

    // Diffusion correlation on days with no jump in either series.
    std::vector<bool> jump_day(index_log.size(), false);
    for (auto i : out.fx.jump_days) jump_day[i] = true;
    for (auto i : out.index.jump_days) jump_day[i] = true;
    std::vector<double> a, b;
    for (std::size_t i = 0; i < index_log.size(); ++i)
        if (!jump_day[i]) {
            a.push_back(index_log[i]);
            b.push_back(fx_log[i]);
        }
    out.model.rho = correlation(a, b);
    out.total_rho = correlation(index_log, fx_log);
    out.model.index = out.index.model;
    out.model.fx = out.fx.model;
    return out;
}

EgxFxModel gbm_equivalent(std::span<const double> index_log, std::span<const double> fx_log, double days_per_year) {
    if (index_log.size() != fx_log.size()) throw std::invalid_argument("gbm_equivalent: series lengths differ");
    check_inputs(index_log.size(), days_per_year);
    EgxFxModel m;
    const double root = std::sqrt(days_per_year);
    m.index.drift = mean(index_log) * days_per_year;
    m.index.sigma = stdev(index_log) * root;
    m.fx.drift = mean(fx_log) * days_per_year;
    m.fx.sigma = stdev(fx_log) * root;
    m.rho = correlation(index_log, fx_log);
    return m;
}

}  // namespace egxq::risk
