#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "egxquant/risk/jump_diffusion.hpp"

/// Fit the jump-diffusion model to daily log returns with a threshold rule:
/// a day is a jump when its return is further from the median than
///
///     max(jump_sigmas * robust daily volatility, min_jump)
///
/// where the robust volatility is 1.4826 * the median absolute deviation, which
/// one devaluation cannot inflate. The `min_jump` floor matters for USD/EGP: in
/// a managed rate most days barely move, so the robust volatility is near zero.
///
/// Then: jump rate = jumps / years; jump mean and stdev = the sample of jump-day
/// returns (stdev 0 with a single jump); sigma = stdev of the other days; drift
/// = the mean of all days. All annualised with `days_per_year`.

namespace egxq::risk {

struct JumpRule {
    double jump_sigmas = 4.0;
    double min_jump = 0.02;  ///< 2% log move
};

struct FactorFit {
    JumpDiffusion model;
    std::vector<std::size_t> jump_days;  ///< Indices into the returns
    double threshold = 0.0;  ///< Absolute daily log-return distance from the median
};

FactorFit fit_jump_diffusion(std::span<const double> daily_log, double days_per_year, const JumpRule& rule,
                             std::span<const std::size_t> exclude = {});

struct EgxFxFit {
    EgxFxModel model;
    FactorFit index;
    FactorFit fx;
    double total_rho = 0.0;  ///< Correlation of all daily returns (for the no-jump comparison model)
};

/// Fit both factors to returns on the same days. USD/EGP jumps are found first;
/// on those days the EGX30 move is attributed to pass-through (fitted by least
/// squares through the origin) rather than to an EGX30 jump. The diffusion
/// correlation uses the days with no jump in either series.
EgxFxFit fit_egx_fx(std::span<const double> index_log, std::span<const double> fx_log, double days_per_year,
                    const JumpRule& index_rule, const JumpRule& fx_rule);

/// The same series fitted with no jumps: drift, total volatility and total
/// correlation. The plain GBM that the jump model is compared against.
EgxFxModel gbm_equivalent(std::span<const double> index_log, std::span<const double> fx_log, double days_per_year);

}  // namespace egxq::risk
