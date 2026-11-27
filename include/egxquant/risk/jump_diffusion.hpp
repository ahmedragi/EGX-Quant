#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "egxquant/risk/var.hpp"

/// Two-factor Merton jump-diffusion for the EGX30 (in EGP) and USD/EGP.
///
/// Each factor's log return over a horizon of T years is
///
///     diffusion_drift * T + sigma * sqrt(T) * Z + (sum of N jumps),
///     N ~ Poisson(jump_rate * T),  each jump ~ Normal(jump_mean, jump_stdev^2)
///
/// where `drift` is the expected log return per year with jumps included and
/// the diffusion drift is what is left after the expected jumps. The EGX30 also
/// moves by `passthrough` times every USD/EGP jump: how the index reacts in
/// pounds on a devaluation day. The diffusion shocks of the two factors have
/// correlation `rho`.
///
/// Views: EGP = the EGX30 log return; USD = EGX30 minus USD/EGP log return.
/// All parameters are annual and in log terms (0.10 = a 10% log move).

namespace egxq::risk {

struct JumpDiffusion {
    double drift = 0.0;  ///< Expected log return per year, jumps included
    double sigma = 0.0;  ///< Diffusion volatility per sqrt(year)
    double jump_rate = 0.0;  ///< Expected jumps per year
    double jump_mean = 0.0;  ///< Mean log jump size
    double jump_stdev = 0.0;  ///< Standard deviation of the log jump size
};

struct EgxFxModel {
    JumpDiffusion index;  ///< EGX30 in EGP
    JumpDiffusion fx;  ///< USD/EGP, pounds per dollar (a rise is a devaluation)
    double rho = 0.0;  ///< Correlation of the diffusion shocks
    double passthrough = 0.0;  ///< EGX30 log move per unit USD/EGP log jump
};

enum class View { Egp, Usd };

/// Drift of the diffusion part: expected jumps removed from the total drift.
double index_diffusion_drift(const EgxFxModel& m);
double fx_diffusion_drift(const EgxFxModel& m);

/// Log returns of one simulated horizon.
struct HorizonDraw {
    double egp = 0.0;
    double usd = 0.0;
};

/// Sample the horizon log returns exactly (no time stepping is needed, since
/// VaR depends only on the horizon return). Deterministic for a given seed.
std::vector<HorizonDraw> simulate_horizon(const EgxFxModel& m, double years, std::size_t paths, std::uint64_t seed);

/// One normal component of the exact distribution: given the jump counts, the
/// horizon log return is normal.
struct MixtureComponent {
    double weight = 0.0;
    double mean = 0.0;
    double stdev = 0.0;
};

/// The horizon log return of a view as a Poisson-weighted mixture of normals,
/// truncated once the omitted Poisson mass is below 1e-12.
std::vector<MixtureComponent> horizon_mixture(const EgxFxModel& m, View view, double years);

/// P(log return <= x) under the mixture.
double mixture_cdf(const std::vector<MixtureComponent>& mix, double x);

/// The p-quantile of the mixture (bisection to about 1e-12).
double mixture_quantile(const std::vector<MixtureComponent>& mix, double p);

/// Exact VaR/CVaR of a long (or short) position over the horizon, as loss
/// fractions, from the mixture. A long loses -(e^R - 1), a short e^R - 1.
TailRisk exact_tail(const std::vector<MixtureComponent>& mix, double confidence, bool is_short);

/// Monte Carlo VaR/CVaR with a standard error for the VaR from `batches`
/// equal sub-samples (the spread of the batch VaRs / sqrt(batches)).
struct McTail {
    TailRisk tail;
    double var_stderr = 0.0;
};
McTail mc_tail(const std::vector<HorizonDraw>& draws, View view, double confidence, bool is_short,
               std::size_t batches = 10);

}  // namespace egxq::risk
