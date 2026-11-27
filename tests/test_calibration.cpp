#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "egxquant/risk/calibration.hpp"

using namespace egxq::risk;

namespace {

/// 500 days alternating +-0.1% (a managed rate), with devaluations of
/// 30% and 10% (log) on days 100 and 300.
std::vector<double> fx_series() {
    std::vector<double> r;
    for (int i = 0; i < 500; ++i) r.push_back(i % 2 == 0 ? 0.001 : -0.001);
    r[100] = 0.30;
    r[300] = 0.10;
    return r;
}

/// EGX30: alternating +-1%, a -3% / -1% reaction on the devaluation days and
/// an 8% crash on day 200.
std::vector<double> index_series() {
    std::vector<double> r;
    for (int i = 0; i < 500; ++i) r.push_back(i % 2 == 0 ? 0.01 : -0.01);
    r[100] = -0.03;
    r[300] = -0.01;
    r[200] = -0.08;
    return r;
}

}  // namespace

TEST(Calibration, FindsPlantedJumps) {
    const auto r = fx_series();
    const auto f = fit_jump_diffusion(r, 250.0, JumpRule{4.0, 0.02});
    ASSERT_EQ(f.jump_days.size(), 2u);
    EXPECT_EQ(f.jump_days[0], 100u);
    EXPECT_EQ(f.jump_days[1], 300u);
    EXPECT_NEAR(f.threshold, 0.02, 1e-15);  // robust vol is 0.1%, so the floor applies
    EXPECT_NEAR(f.model.jump_rate, 2.0 / (500.0 / 250.0), 1e-12);
    EXPECT_NEAR(f.model.jump_mean, 0.20, 1e-12);
    EXPECT_NEAR(f.model.jump_stdev, std::sqrt(0.02), 1e-12);  // sample sd of {0.3, 0.1}

    // The other 498 days: 248 of +0.1% and 250 of -0.1% (days 100 and 300 were +0.1% days).
    const double m = (248.0 - 250.0) * 0.001 / 498.0;
    const double ss = 248.0 * (0.001 - m) * (0.001 - m) + 250.0 * (-0.001 - m) * (-0.001 - m);
    EXPECT_NEAR(f.model.sigma, std::sqrt(ss / 497.0) * std::sqrt(250.0), 1e-12);
    double sum = 0.0;
    for (double x : r) sum += x;
    EXPECT_NEAR(f.model.drift, sum / 500.0 * 250.0, 1e-12);
}

TEST(Calibration, JointFitAttributesDevaluationDays) {
    // Index floor 5%: its +-1% days sit 2% from the median, which must not count.
    const auto fit = fit_egx_fx(index_series(), fx_series(), 250.0, JumpRule{4.0, 0.05}, JumpRule{4.0, 0.02});
    EXPECT_EQ(fit.fx.jump_days.size(), 2u);
    // Only the crash is an EGX30 jump; the devaluation-day moves are pass-through.
    ASSERT_EQ(fit.index.jump_days.size(), 1u);
    EXPECT_EQ(fit.index.jump_days[0], 200u);
    EXPECT_NEAR(fit.model.index.jump_mean, -0.08, 1e-15);
    // beta = (-0.03 * 0.3 + -0.01 * 0.1) / (0.09 + 0.01) = -0.1
    EXPECT_NEAR(fit.model.passthrough, -0.1, 1e-12);
    // On the remaining days index and FX move in lock step (+,- together).
    EXPECT_NEAR(fit.model.rho, 1.0, 1e-9);
}

TEST(Calibration, GbmEquivalentUsesTotalMoments) {
    const auto idx = index_series();
    const auto fx = fx_series();
    const auto g = gbm_equivalent(idx, fx, 250.0);
    double m = 0.0;
    for (double x : fx) m += x;
    m /= 500.0;
    double ss = 0.0;
    for (double x : fx) ss += (x - m) * (x - m);
    EXPECT_NEAR(g.fx.sigma, std::sqrt(ss / 499.0 * 250.0), 1e-12);
    EXPECT_EQ(g.fx.jump_rate, 0.0);
    EXPECT_EQ(g.index.jump_rate, 0.0);
}

TEST(Calibration, RejectsShortOrMismatchedSeries) {
    const std::vector<double> two{0.1, 0.2};
    EXPECT_THROW(fit_jump_diffusion(two, 250.0, JumpRule{}), std::invalid_argument);
    const std::vector<double> three{0.1, 0.2, 0.3};
    const std::vector<double> four{0.1, 0.2, 0.3, 0.4};
    EXPECT_THROW(fit_egx_fx(three, four, 250.0, JumpRule{}, JumpRule{}), std::invalid_argument);
}
