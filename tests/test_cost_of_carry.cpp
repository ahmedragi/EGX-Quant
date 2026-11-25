#include <gtest/gtest.h>

#include <stdexcept>

#include "egxquant/futures/cost_of_carry.hpp"

using namespace egxq;

// Reference values computed independently (Python math module).

TEST(CostOfCarry, TextbookCase) {
    // S=100, r=5%, q=0, T=1  ->  100 * e^0.05
    EXPECT_NEAR(fair_value({100.0, 0.05, 0.0, 1.0}), 105.12710963760242, 1e-10);
}

TEST(CostOfCarry, EgxLikeCase) {
    // EGX30 ~ 40,000, r = 20%, q = 3%, three months
    EXPECT_NEAR(fair_value({40000.0, 0.20, 0.03, 0.25}), 41736.642254947124, 1e-6);
}

TEST(CostOfCarry, AtExpiryFuturesEqualsSpot) {
    EXPECT_DOUBLE_EQ(fair_value({41500.0, 0.22, 0.03, 0.0}), 41500.0);
}

TEST(CostOfCarry, BackwardationWhenYieldExceedsRate) {
    EXPECT_LT(fair_value({100.0, 0.02, 0.05, 0.5}), 100.0);
}

TEST(CostOfCarry, DiscreteDividends) {
    // S=100, r=10%, T=1, dividend of 2 at t=0.5
    const std::vector<Dividend> divs{{0.5, 2.0}};
    EXPECT_NEAR(fair_value_discrete(100.0, 0.10, 1.0, divs), 108.41454961481271, 1e-10);
}

TEST(CostOfCarry, DiscreteIgnoresDividendsAfterExpiry) {
    const std::vector<Dividend> divs{{1.5, 2.0}};
    EXPECT_NEAR(fair_value_discrete(100.0, 0.10, 1.0, divs),
                fair_value({100.0, 0.10, 0.0, 1.0}), 1e-12);
}

TEST(CostOfCarry, ImpliedRateRoundTrip) {
    const CarryInputs in{40000.0, 0.21, 0.035, 0.5};
    const double F = fair_value(in);
    EXPECT_NEAR(implied_rate(F, in.spot, in.dividend_yield, in.time_to_expiry), in.rate, 1e-12);
    EXPECT_NEAR(implied_dividend_yield(F, in.spot, in.rate, in.time_to_expiry), in.dividend_yield, 1e-12);
}

TEST(CostOfCarry, Notional) {
    EXPECT_DOUBLE_EQ(contract_notional(42000.0, 1.0), 42000.0);
    EXPECT_DOUBLE_EQ(contract_notional(42000.0, 10.0), 420000.0);
}

TEST(CostOfCarry, RejectsBadInputs) {
    EXPECT_THROW(fair_value({0.0, 0.2, 0.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(fair_value({100.0, 0.2, 0.0, -0.1}), std::invalid_argument);
    EXPECT_THROW(implied_rate(100.0, 100.0, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(fair_value_discrete(1.0, 0.1, 1.0, {{0.1, 5.0}}), std::invalid_argument);
}
