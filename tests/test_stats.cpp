#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "egxquant/risk/stats.hpp"

using namespace egxq::stats;

// Reference quantiles from standard normal tables (e.g. scipy.stats.norm.ppf).
TEST(Stats, NormalQuantileKnownValues) {
    EXPECT_NEAR(normal_quantile(0.5), 0.0, 1e-15);
    EXPECT_NEAR(normal_quantile(0.95), 1.6448536269514722, 1e-12);
    EXPECT_NEAR(normal_quantile(0.975), 1.959963984540054, 1e-12);
    EXPECT_NEAR(normal_quantile(0.99), 2.3263478740408408, 1e-12);
    EXPECT_NEAR(normal_quantile(0.05), -1.6448536269514722, 1e-12);
    EXPECT_NEAR(normal_quantile(1e-6), -4.753424308822899, 1e-10);   // lower tail branch
    EXPECT_NEAR(normal_quantile(0.9999), 3.719016485455709, 1e-10);  // upper tail branch
}

TEST(Stats, NormalQuantileInvertsCdf) {
    for (double p : {0.001, 0.02, 0.1, 0.3, 0.7, 0.9, 0.98, 0.999})
        EXPECT_NEAR(normal_cdf(normal_quantile(p)), p, 1e-14) << p;
}

TEST(Stats, NormalQuantileRejectsBadP) {
    EXPECT_THROW(normal_quantile(0.0), std::invalid_argument);
    EXPECT_THROW(normal_quantile(1.0), std::invalid_argument);
}

TEST(Stats, NormalPdfAndCdf) {
    EXPECT_NEAR(normal_pdf(0.0), 0.3989422804014327, 1e-15);
    EXPECT_NEAR(normal_cdf(1.959963984540054), 0.975, 1e-14);
}

TEST(Stats, ChiSquareOneTail) {
    EXPECT_NEAR(chi2_1_sf(3.841458820694124), 0.05, 1e-12);  // 95% critical value
    EXPECT_NEAR(chi2_1_sf(6.634896601021214), 0.01, 1e-12);  // 99% critical value
    EXPECT_DOUBLE_EQ(chi2_1_sf(0.0), 1.0);
}

TEST(Stats, MomentsByHand) {
    // x = 1, 2, 3, 4, 10: mean 4, deviations -3,-2,-1,0,6.
    // sum d^2 = 50, sum d^3 = -27-8-1+216 = 180, sum d^4 = 81+16+1+1296 = 1394.
    const std::vector<double> x{1, 2, 3, 4, 10};
    const auto m = moments(x);
    EXPECT_EQ(m.n, 5u);
    EXPECT_DOUBLE_EQ(m.mean, 4.0);
    EXPECT_NEAR(m.stdev, std::sqrt(50.0 / 4.0), 1e-15);
    EXPECT_NEAR(m.skewness, (180.0 / 5.0) / std::pow(10.0, 1.5), 1e-14);
    EXPECT_NEAR(m.excess_kurtosis, (1394.0 / 5.0) / 100.0 - 3.0, 1e-14);
    EXPECT_DOUBLE_EQ(m.min, 1.0);
    EXPECT_DOUBLE_EQ(m.max, 10.0);
}

TEST(Stats, MomentsNeedTwoValues) {
    const std::vector<double> one{1.0};
    EXPECT_THROW(moments(one), std::invalid_argument);
}
