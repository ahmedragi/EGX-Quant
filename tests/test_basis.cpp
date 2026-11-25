#include <gtest/gtest.h>

#include "egxquant/futures/basis.hpp"

using namespace egxq;

namespace {
// S=40,000, r=20%, q=3%, T=0.25; borrow 21%, lend 19%, 0.30% spot, 0.05% futures
const CarryInputs kIn{40000.0, 0.20, 0.03, 0.25};
const Frictions kFr{0.21, 0.19, 0.003, 0.0005};
}  // namespace

TEST(Basis, BoundsMatchReference) {
    const auto b = arbitrage_bounds(kIn.spot, kIn.dividend_yield, kIn.time_to_expiry, kFr);
    EXPECT_NEAR(b.upper, 41987.62105840748, 1e-6);
    EXPECT_NEAR(b.lower, 41486.77990795505, 1e-6);
}

TEST(Basis, FairValueInsideBand) {
    const auto b = arbitrage_bounds(kIn.spot, kIn.dividend_yield, kIn.time_to_expiry, kFr);
    const double fv = fair_value(kIn);
    EXPECT_LT(b.lower, fv);
    EXPECT_GT(b.upper, fv);
}

TEST(Basis, FrictionlessBandCollapsesToFairValue) {
    const Frictions none{0.20, 0.20, 0.0, 0.0};
    const auto b = arbitrage_bounds(kIn.spot, kIn.dividend_yield, kIn.time_to_expiry, none);
    EXPECT_NEAR(b.lower, fair_value(kIn), 1e-9);
    EXPECT_NEAR(b.upper, fair_value(kIn), 1e-9);
}

TEST(Basis, NoSignalAtFairValue) {
    const auto r = analyze_basis(kIn, fair_value(kIn), kFr);
    EXPECT_EQ(r.signal, Signal::None);
    EXPECT_NEAR(r.mispricing, 0.0, 1e-9);
    EXPECT_DOUBLE_EQ(r.edge, 0.0);
    EXPECT_NEAR(r.annualized_basis, kIn.rate - kIn.dividend_yield, 1e-12);
}

TEST(Basis, RichFuturesTriggersCashAndCarry) {
    const auto r = analyze_basis(kIn, 42200.0, kFr);
    EXPECT_EQ(r.signal, Signal::CashAndCarry);
    EXPECT_NEAR(r.edge, 42200.0 - 41987.62105840748, 1e-6);
    EXPECT_GT(r.mispricing, 0.0);
}

TEST(Basis, CheapFuturesTriggersReverse) {
    const auto r = analyze_basis(kIn, 41300.0, kFr);
    EXPECT_EQ(r.signal, Signal::ReverseCashAndCarry);
    EXPECT_NEAR(r.edge, 41486.77990795505 - 41300.0, 1e-6);
}

TEST(Basis, BasisDefinitions) {
    const auto r = analyze_basis(kIn, 41800.0, kFr);
    EXPECT_DOUBLE_EQ(r.basis, 1800.0);
    EXPECT_NEAR(r.theoretical_basis, fair_value(kIn) - 40000.0, 1e-9);
}

TEST(Basis, SignalNames) {
    EXPECT_EQ(to_string(Signal::None), "NONE");
    EXPECT_EQ(to_string(Signal::CashAndCarry), "CASH_AND_CARRY");
    EXPECT_EQ(to_string(Signal::ReverseCashAndCarry), "REVERSE_CASH_AND_CARRY");
}
