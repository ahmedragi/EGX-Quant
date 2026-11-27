#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "egxquant/risk/jump_diffusion.hpp"
#include "egxquant/risk/random.hpp"

using namespace egxq::risk;

namespace {

// z quantiles from standard normal tables.
constexpr double kZ99 = 2.3263478740408408;
constexpr double kZ95 = 1.6448536269514722;

double weight_sum(const std::vector<MixtureComponent>& mix) {
    double s = 0.0;
    for (const auto& c : mix) s += c.weight;
    return s;
}

/// A model with devaluation jumps in USD/EGP and small EGX30 jumps.
EgxFxModel jumpy() {
    EgxFxModel m;
    m.index = {0.25, 0.20, 0.5, -0.04, 0.03};
    m.fx = {0.15, 0.03, 1.0, 0.20, 0.10};
    m.rho = 0.1;
    m.passthrough = 0.3;
    return m;
}

}  // namespace

TEST(Rng, PoissonMomentsAndDeterminism) {
    Rng rng(7);
    const double lambda = 1.7;
    const int n = 200000;
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const double k = rng.poisson(lambda);
        sum += k;
        sum2 += k * k;
    }
    const double mean = sum / n;
    EXPECT_NEAR(mean, lambda, 0.01);  // s.e. = sqrt(1.7 / 2e5) = 0.003
    EXPECT_NEAR(sum2 / n - mean * mean, lambda, 0.03);

    Rng a(99), b(99);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(a.poisson(0.3), b.poisson(0.3));
    EXPECT_EQ(Rng(1).poisson(0.0), 0u);
}

TEST(JumpDiffusion, NoJumpsIsLognormal) {
    EgxFxModel m;
    m.index = {0.30, 0.25, 0.0, 0.0, 0.0};
    const double T = 10.0 / 250.0;
    const auto mix = horizon_mixture(m, View::Egp, T);
    ASSERT_EQ(mix.size(), 1u);
    EXPECT_NEAR(mix[0].mean, 0.30 * T, 1e-15);
    EXPECT_NEAR(mix[0].stdev, 0.25 * std::sqrt(T), 1e-15);

    // Long VaR = 1 - exp(gT - sigma sqrt(T) z); short VaR = exp(gT + sigma sqrt(T) z) - 1.
    const auto lng = exact_tail(mix, 0.99, false);
    EXPECT_NEAR(lng.var, -std::expm1(0.30 * T - 0.25 * std::sqrt(T) * kZ99), 1e-10);
    const auto sht = exact_tail(mix, 0.95, true);
    EXPECT_NEAR(sht.var, std::expm1(0.30 * T + 0.25 * std::sqrt(T) * kZ95), 1e-10);
    EXPECT_GT(lng.cvar, lng.var);
    EXPECT_GT(sht.cvar, sht.var);
}

TEST(JumpDiffusion, DiffusionDriftRemovesExpectedJumps) {
    const auto m = jumpy();
    EXPECT_NEAR(index_diffusion_drift(m), 0.25 - 0.5 * -0.04 - 0.3 * 1.0 * 0.20, 1e-15);
    EXPECT_NEAR(fx_diffusion_drift(m), 0.15 - 1.0 * 0.20, 1e-15);

    // So the mixture mean equals the total drift.
    const double T = 0.5;
    for (View v : {View::Egp, View::Usd}) {
        double mean = 0.0;
        const auto mix = horizon_mixture(m, v, T);
        for (const auto& c : mix) mean += c.weight * c.mean;
        const double expected = v == View::Egp ? 0.25 * T : (0.25 - 0.15) * T;
        EXPECT_NEAR(mean, expected, 1e-9);
        EXPECT_NEAR(weight_sum(mix), 1.0, 1e-11);
    }
}

TEST(JumpDiffusion, PureJumpCdfMatchesPoissonSum) {
    // FX only jumps (no diffusion, no drift): USD view = -(sum of jumps).
    EgxFxModel m;
    m.fx = {0.0, 0.0, 2.0, 0.1, 0.05};
    m.fx.drift = m.fx.jump_rate * m.fx.jump_mean;  // zero diffusion drift
    const double T = 1.0;
    const auto mix = horizon_mixture(m, View::Usd, T);
    // P(R <= -0.15) = P(sum of jumps >= 0.15) = sum_{n>=1} P(N=n) Phi((0.1 n - 0.15) / (0.05 sqrt n)).
    double expected = 0.0, p = std::exp(-2.0);
    for (int n = 1; n < 40; ++n) {
        p *= 2.0 / n;
        const double z = (0.1 * n - 0.15) / (0.05 * std::sqrt(static_cast<double>(n)));
        expected += p * 0.5 * std::erfc(-z / std::sqrt(2.0));
    }
    EXPECT_NEAR(mixture_cdf(mix, -0.15), expected, 1e-10);
}

TEST(JumpDiffusion, UsdViewSubtractsFx) {
    // EGX30 flat; USD/EGP a pure GBM: USD return = -(FX return).
    EgxFxModel m;
    m.fx = {0.20, 0.10, 0.0, 0.0, 0.0};
    const auto mix = horizon_mixture(m, View::Usd, 1.0);
    ASSERT_EQ(mix.size(), 1u);
    EXPECT_NEAR(mix[0].mean, -0.20, 1e-15);
    EXPECT_NEAR(mix[0].stdev, 0.10, 1e-15);
}

TEST(JumpDiffusion, MonteCarloMatchesExact) {
    const auto m = jumpy();
    const double T = 21.0 / 250.0;
    const auto draws = simulate_horizon(m, T, 400000, 2024);
    for (View v : {View::Egp, View::Usd})
        for (bool is_short : {false, true})
            for (double c : {0.95, 0.99}) {
                const auto mc = mc_tail(draws, v, c, is_short);
                const auto ex = exact_tail(horizon_mixture(m, v, T), c, is_short);
                EXPECT_GT(mc.var_stderr, 0.0);
                EXPECT_NEAR(mc.tail.var, ex.var, 4.0 * mc.var_stderr + 1e-4)
                    << "view " << int(v) << " short " << is_short << " c " << c;
                EXPECT_NEAR(mc.tail.cvar, ex.cvar, 0.03 * ex.cvar + 1e-3);
            }
}

TEST(JumpDiffusion, SimulationIsDeterministic) {
    const auto a = simulate_horizon(jumpy(), 0.1, 1000, 5);
    const auto b = simulate_horizon(jumpy(), 0.1, 1000, 5);
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].egp, b[i].egp);
        EXPECT_EQ(a[i].usd, b[i].usd);
    }
}

TEST(JumpDiffusion, RejectsBadModels) {
    auto m = jumpy();
    m.rho = 1.5;
    EXPECT_THROW(simulate_horizon(m, 0.1, 10, 1), std::invalid_argument);
    m = jumpy();
    m.fx.sigma = -0.1;
    EXPECT_THROW(horizon_mixture(m, View::Egp, 0.1), std::invalid_argument);
    EXPECT_THROW(horizon_mixture(jumpy(), View::Egp, 0.0), std::invalid_argument);
}
