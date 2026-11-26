#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "egxquant/risk/var.hpp"

using namespace egxq::risk;

namespace {

/// Returns -10%, -9%, ..., +9% (20 values), in scrambled order.
std::vector<double> ladder() {
    std::vector<double> r;
    for (int i = 0; i < 20; ++i) r.push_back(static_cast<double>((i * 7) % 20 - 10) / 100.0);
    return r;
}

}  // namespace

TEST(Returns, LogReturns) {
    const std::vector<double> px{100.0, 110.0, 99.0};
    const auto r = log_returns(px);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_NEAR(r[0], std::log(1.1), 1e-15);
    EXPECT_NEAR(r[1], std::log(0.9), 1e-15);
    const std::vector<double> bad{100.0, 0.0};
    EXPECT_THROW(log_returns(bad), std::invalid_argument);
}

TEST(Returns, HorizonReturnsAreOverlappingSums) {
    const std::vector<double> r{0.01, -0.02, 0.03, 0.04};
    const auto h2 = horizon_log_returns(r, 2);
    ASSERT_EQ(h2.size(), 3u);
    EXPECT_NEAR(h2[0], -0.01, 1e-15);
    EXPECT_NEAR(h2[1], 0.01, 1e-15);
    EXPECT_NEAR(h2[2], 0.07, 1e-15);
    EXPECT_EQ(horizon_log_returns(r, 1).size(), 4u);
    EXPECT_TRUE(horizon_log_returns(r, 5).empty());
    EXPECT_THROW(horizon_log_returns(r, 0), std::invalid_argument);
}

TEST(Returns, SimpleFromLog) {
    const std::vector<double> r{std::log(1.25)};
    EXPECT_NEAR(to_simple_returns(r)[0], 0.25, 1e-15);
}

// Losses sorted ascending are -0.09 ... 0.10 (index 0..19).
TEST(HistoricalVar, EmpiricalQuantile) {
    const auto r = ladder();
    const auto v95 = historical_var(r, 0.95);  // k = ceil(19) - 1 = 18
    EXPECT_NEAR(v95.var, 0.09, 1e-15);
    EXPECT_NEAR(v95.cvar, 0.095, 1e-15);
    EXPECT_EQ(v95.tail_count, 2u);
    EXPECT_EQ(v95.sample_size, 20u);

    const auto v90 = historical_var(r, 0.90);  // k = 17
    EXPECT_NEAR(v90.var, 0.08, 1e-15);
    EXPECT_NEAR(v90.cvar, 0.09, 1e-15);

    const auto v99 = historical_var(r, 0.99);  // ceil(19.8) - 1 = 19: the worst loss
    EXPECT_NEAR(v99.var, 0.10, 1e-15);
    EXPECT_NEAR(v99.cvar, 0.10, 1e-15);
    EXPECT_EQ(v99.tail_count, 1u);
}

TEST(HistoricalVar, CvarAtLeastVar) {
    const auto r = ladder();
    for (double c : {0.5, 0.75, 0.9, 0.95, 0.975, 0.99}) {
        const auto t = historical_var(r, c);
        EXPECT_GE(t.cvar, t.var) << c;
    }
}

TEST(HistoricalVar, RejectsBadInput) {
    const std::vector<double> empty;
    EXPECT_THROW(historical_var(empty, 0.95), std::invalid_argument);
    EXPECT_THROW(historical_var(ladder(), 1.0), std::invalid_argument);
}

TEST(NormalVar, Formula) {
    // z(0.99) = 2.3263478740408408, phi(z) = 0.026652142203458
    const auto t = normal_var(0.001, 0.02, 0.99);
    EXPECT_NEAR(t.var, -0.001 + 0.02 * 2.3263478740408408, 1e-12);
    EXPECT_NEAR(t.cvar, -0.001 + 0.02 * 0.026652142203458 / 0.01, 1e-10);
}

TEST(Kupiec, KnownLikelihoodRatio) {
    // n = 250, x = 10, p = 1%: LR = -2 ln[(0.99^240 0.01^10) / (0.96^240 0.04^10)] = 12.9555
    const auto k = kupiec_pof(250, 10, 0.99);
    EXPECT_NEAR(k.lr, 12.9555, 1e-3);
    EXPECT_NEAR(k.observed_rate, 0.04, 1e-15);
    EXPECT_LT(k.p_value, 0.001);
}

TEST(Kupiec, ExactRateGivesZero) {
    const auto k = kupiec_pof(200, 10, 0.95);
    EXPECT_NEAR(k.lr, 0.0, 1e-12);
    EXPECT_NEAR(k.p_value, 1.0, 1e-9);
}

TEST(Kupiec, NoExceptions) {
    // x = 0: LR = -2 n ln(1 - p)
    const auto k = kupiec_pof(500, 0, 0.99);
    EXPECT_NEAR(k.lr, -2.0 * 500.0 * std::log(0.99), 1e-12);
}

TEST(Backtest, CountsExceptions) {
    // Window of 20 ladder returns gives 95% VaR = 9%; then a 12% loss and a 5% loss.
    auto r = ladder();
    r.push_back(-0.12);
    r.push_back(-0.05);
    const auto bt = rolling_backtest(r, 20, 0.95);
    ASSERT_EQ(bt.days.size(), 2u);
    EXPECT_NEAR(bt.days[0].var, 0.09, 1e-15);
    EXPECT_TRUE(bt.days[0].exception);
    EXPECT_EQ(bt.days[1].index, 21u);
    EXPECT_FALSE(bt.days[1].exception);
    EXPECT_EQ(bt.kupiec.exceptions, 1u);
    EXPECT_EQ(bt.kupiec.observations, 2u);
}

TEST(Backtest, TooShortIsEmpty) {
    const auto bt = rolling_backtest(ladder(), 20, 0.95);
    EXPECT_TRUE(bt.days.empty());
    EXPECT_EQ(bt.kupiec.observations, 0u);
}
