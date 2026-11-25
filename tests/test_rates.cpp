#include <gtest/gtest.h>

#include <stdexcept>

#include "egxquant/core/rates.hpp"

using namespace egxq;

// Reference values computed independently (Python math module).

TEST(Rates, SimpleToContinuous) {
    // 25% simple over 6 months: ln(1.125) / 0.5
    EXPECT_NEAR(simple_to_continuous(0.25, 0.5), 0.2355660713127669, 1e-12);
}

TEST(Rates, SimpleContinuousRoundTrip) {
    for (double r : {0.0, 0.05, 0.22, 0.30}) {
        for (double t : {0.1, 0.25, 0.5, 1.0}) {
            EXPECT_NEAR(continuous_to_simple(simple_to_continuous(r, t), t), r, 1e-12);
        }
    }
}

TEST(Rates, HighRatesContinuousIsBelowSimple) {
    // With Egyptian-level rates the gap between conventions is material.
    const double r = 0.25;
    EXPECT_LT(simple_to_continuous(r, 0.25), r);
    EXPECT_GT(r - simple_to_continuous(r, 0.25), 0.007);
}

TEST(Rates, AnnualToContinuous) {
    EXPECT_NEAR(annual_to_continuous(0.03), 0.02955880224154443, 1e-12);
    EXPECT_NEAR(continuous_to_annual(annual_to_continuous(0.18)), 0.18, 1e-12);
}

TEST(Rates, DiscountFactor) {
    EXPECT_NEAR(discount_factor(0.05, 1.0), 1.0 / 1.0512710963760241, 1e-12);
    EXPECT_DOUBLE_EQ(discount_factor(0.2, 0.0), 1.0);
}

TEST(Rates, RejectsBadInputs) {
    EXPECT_THROW(simple_to_continuous(0.1, 0.0), std::invalid_argument);
    EXPECT_THROW(simple_to_continuous(-3.0, 1.0), std::invalid_argument);
    EXPECT_THROW(annual_to_continuous(-1.0), std::invalid_argument);
}
