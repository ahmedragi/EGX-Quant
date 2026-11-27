#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "egxquant/risk/hedge.hpp"
#include "egxquant/risk/margin.hpp"
#include "egxquant/risk/stress.hpp"

using namespace egxq::risk;

namespace {

MarginRules simple_rules() {
    MarginRules r;
    r.initial = 10.0;
    r.maintenance = 7.5;
    r.price_limit = 0.10;
    return r;
}

}  // namespace

// ---- hedge ratio ----

TEST(Hedge, ExactLinearRelationship) {
    const std::vector<double> h{0.01, -0.02, 0.015, -0.005, 0.03};
    std::vector<double> p;
    for (double x : h) p.push_back(0.001 + 2.0 * x);
    const auto f = fit_hedge(p, h, 250.0);
    EXPECT_NEAR(f.beta, 2.0, 1e-12);
    EXPECT_NEAR(f.alpha, 0.001, 1e-12);
    EXPECT_NEAR(f.r_squared, 1.0, 1e-12);
    EXPECT_NEAR(f.hedged_vol, 0.0, 1e-9);
    EXPECT_NEAR(f.portfolio_vol, 2.0 * f.hedge_vol, 1e-12);
}

TEST(Hedge, BetaAndEffectivenessByHand) {
    // h = 1, -1, 1, -1 (mean 0, sum h^2 = 4); p = 1, 0, 0, -1 (mean 0).
    // cov sum = 1 + 0 + 0 + 1 = 2 -> beta = 0.5; sum p^2 = 2 -> corr = 2 / sqrt(8), R^2 = 0.5.
    const std::vector<double> h{1, -1, 1, -1};
    const std::vector<double> p{1, 0, 0, -1};
    const auto f = fit_hedge(p, h, 1.0);
    EXPECT_NEAR(f.beta, 0.5, 1e-15);
    EXPECT_NEAR(f.r_squared, 0.5, 1e-15);
    // residual sum of squares = 2 - 0.5 * 2 = 1 -> vol sqrt(1/3)
    EXPECT_NEAR(f.hedged_vol, std::sqrt(1.0 / 3.0), 1e-15);
}

TEST(Hedge, Contracts) {
    // 2,000,000 EGP with beta 1.2 at 50,000 x 1 EGP/pt: 48 contracts.
    EXPECT_NEAR(hedge_contracts(2'000'000.0, 1.2, 50'000.0, 1.0), 48.0, 1e-12);
    EXPECT_THROW(hedge_contracts(1.0, 1.0, 0.0, 1.0), std::invalid_argument);
    const std::vector<double> flat{0.0, 0.0, 0.0};
    const std::vector<double> any{0.1, 0.2, 0.3};
    EXPECT_THROW(fit_hedge(any, flat), std::invalid_argument);
}

// ---- price limit and margin account ----

TEST(Margin, PriceLimitCarriesOver) {
    // A 25% fall with a 10% limit: 90, 81, then the remaining move to 75.
    const std::vector<double> r{std::log(0.75), 0.0, 0.0, 0.0};
    std::vector<bool> locked;
    const auto s = limit_settlements(100.0, r, 0.10, &locked);
    ASSERT_EQ(s.size(), 4u);
    EXPECT_NEAR(s[0], 90.0, 1e-9);
    EXPECT_NEAR(s[1], 81.0, 1e-9);
    EXPECT_NEAR(s[2], 75.0, 1e-9);
    EXPECT_NEAR(s[3], 75.0, 1e-9);
    EXPECT_EQ(locked, (std::vector<bool>{true, true, false, false}));
}

TEST(Margin, MoveOfExactlyTheLimitIsNotLocked) {
    for (double move : {1.1, 0.9}) {
        const std::vector<double> r{std::log(move)};
        std::vector<bool> locked;
        const auto s = limit_settlements(51462.0, r, 0.10, &locked);
        EXPECT_FALSE(locked[0]) << move;
        EXPECT_NEAR(s[0], 51462.0 * move, 1e-6);
    }
}

TEST(Margin, AccountByHand) {
    // 2 long at 100, initial 10, maintenance 7.5 per contract: deposit 20, maintenance 15.
    const std::vector<double> s{100, 98, 95, 99};
    const auto run = run_margin(s, 2.0, simple_rules(), 1.0);
    ASSERT_EQ(run.days.size(), 3u);
    EXPECT_DOUBLE_EQ(run.days[0].balance, 16.0);  // -4, above 15
    EXPECT_DOUBLE_EQ(run.days[0].call, 0.0);
    EXPECT_DOUBLE_EQ(run.days[1].balance_before_call, 10.0);  // -6
    EXPECT_DOUBLE_EQ(run.days[1].call, 10.0);  // back to 20
    EXPECT_DOUBLE_EQ(run.days[2].balance, 28.0);  // +8
    EXPECT_EQ(run.calls, 1u);
    EXPECT_DOUBLE_EQ(run.total_called, 10.0);
    EXPECT_DOUBLE_EQ(run.min_balance, 10.0);
    EXPECT_DOUBLE_EQ(run.pnl, -2.0);
}

TEST(Margin, ShortGainsWhenPriceFalls) {
    const std::vector<double> s{100, 90};
    const auto run = run_margin(s, -3.0, simple_rules(), 1.0);
    EXPECT_DOUBLE_EQ(run.pnl, 30.0);
    EXPECT_EQ(run.calls, 0u);
}

TEST(Margin, MonteCarloWithoutRiskHasNoCalls) {
    EgxFxModel flat;  // no drift, no volatility, no jumps
    const auto r = margin_monte_carlo(flat, 10, 500, 1, 5.0, 50'000.0, MarginRules{}, 1.0);
    EXPECT_EQ(r.p_call, 0.0);
    EXPECT_EQ(r.called_q99, 0.0);
    EXPECT_EQ(r.p_locked, 0.0);
}

TEST(Margin, MonteCarloIsDeterministicAndSensible) {
    EgxFxModel m;
    m.index = {0.0, 0.30, 2.0, -0.08, 0.04};
    const auto a = margin_monte_carlo(m, 21, 4000, 9, 10.0, 50'000.0, MarginRules{}, 1.0);
    const auto b = margin_monte_carlo(m, 21, 4000, 9, 10.0, 50'000.0, MarginRules{}, 1.0);
    EXPECT_EQ(a.p_call, b.p_call);
    EXPECT_EQ(a.called_q99, b.called_q99);
    // 30% vol on a 50,000 index: a 21-day 1-sigma move is about 4,300 points, far beyond the
    // 1,475-point gap between initial and maintenance margin, so calls are likely.
    EXPECT_GT(a.p_call, 0.5);
    EXPECT_GE(a.called_q99, a.called_q95);
    EXPECT_GE(a.called_q95, 0.0);
}

// ---- stress replay ----

TEST(Stress, HedgeProtectsPoundsNotDollars) {
    // Basket 10,000 EGP hedged with 100 short futures at 100; EGX30 -5%, USD/EGP +50%.
    StressPosition pos;
    pos.basket_egp = 10'000.0;
    pos.contracts = -100.0;
    pos.futures_price = 100.0;
    pos.fx = 10.0;
    const std::vector<double> il{std::log(0.95)}, fl{std::log(1.5)};
    const auto r = stress_replay(il, fl, pos, simple_rules(), 1.0);
    ASSERT_EQ(r.days.size(), 1u);
    const auto& d = r.days[0];
    EXPECT_NEAR(d.pnl_egp, 0.0, 1e-9);  // 9,500 basket + 1,000 deposit + 500 futures = 11,000
    EXPECT_NEAR(d.pnl_usd, 11'000.0 / 15.0 - 11'000.0 / 10.0, 1e-9);
    EXPECT_NEAR(d.unhedged_pnl_usd, 9'500.0 / 15.0 - 1'000.0, 1e-9);
    EXPECT_NEAR(r.start_wealth_egp, 11'000.0, 1e-9);
}

TEST(Stress, LimitLockLeavesTheHedgeBehindForADay) {
    // EGX30 -20% in one day; futures can only go 90 then 81 then 80.
    StressPosition pos;
    pos.basket_egp = 10'000.0;
    pos.contracts = -100.0;
    pos.futures_price = 100.0;
    pos.fx = 10.0;
    const std::vector<double> il{std::log(0.8), 0.0, 0.0}, fl{0.0, 0.0, 0.0};
    const auto r = stress_replay(il, fl, pos, simple_rules(), 1.0);
    EXPECT_EQ(r.locked_days, 2u);
    EXPECT_NEAR(r.days[0].pnl_egp, -1'000.0, 1e-9);  // basket -2,000, futures +1,000
    EXPECT_NEAR(r.days[1].pnl_egp, -100.0, 1e-9);  // futures +1,900
    EXPECT_NEAR(r.days[2].pnl_egp, 0.0, 1e-9);
    EXPECT_NEAR(r.worst_pnl_egp, -1'000.0, 1e-9);
}

TEST(Stress, FuturesOnlyLongCrashCallsMargin) {
    StressPosition pos;
    pos.contracts = 2.0;
    pos.futures_price = 100.0;
    pos.fx = 10.0;
    const std::vector<double> il{std::log(0.95), std::log(0.95)}, fl{0.0, 0.0};
    const auto r = stress_replay(il, fl, pos, simple_rules(), 1.0);
    // Deposit 20; day 1 loses 10 -> 10 < 15: call 10. Day 2 loses 9.5 -> 10.5: call 9.5.
    EXPECT_EQ(r.margin.calls, 2u);
    EXPECT_NEAR(r.margin.total_called, 19.5, 1e-9);
    EXPECT_NEAR(r.days.back().pnl_egp, -19.5, 1e-9);
}
