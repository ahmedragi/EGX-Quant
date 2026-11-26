#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "egxquant/data/market_db.hpp"
#include "egxquant/data/risk_series.hpp"

using namespace egxq;
using namespace std::chrono;

namespace {
Date D(const char* s) { return parse_date(s); }
}  // namespace

TEST(RiskSeries, ParseView) {
    EXPECT_EQ(parse_risk_view("EGP"), RiskView::NominalEgp);
    EXPECT_EQ(parse_risk_view("usd"), RiskView::Usd);
    EXPECT_EQ(parse_risk_view("Real"), RiskView::RealEgp);
    EXPECT_FALSE(parse_risk_view("eur").has_value());
}

TEST(RiskSeries, UsdUsesLatestRateAndDropsStale) {
    const std::vector<IndexLevel> egp{{D("2024-03-04"), 30000.0},
                                      {D("2024-03-06"), 31000.0},
                                      {D("2024-03-07"), 31500.0},
                                      {D("2024-03-20"), 32000.0}};
    const std::vector<FxRate> fx{{D("2024-03-01"), 30.9}, {D("2024-03-06"), 49.55}};
    const auto s = in_usd(egp, fx, 7);
    ASSERT_EQ(s.levels.size(), 3u);
    EXPECT_EQ(s.dropped, 1u);  // 03-20 is 14 days after the last rate
    EXPECT_NEAR(s.levels[0].close, 30000.0 / 30.9, 1e-9);   // 03-04 uses 03-01
    EXPECT_NEAR(s.levels[1].close, 31000.0 / 49.55, 1e-9);  // same-day rate
    EXPECT_NEAR(s.levels[2].close, 31500.0 / 49.55, 1e-9);
}

TEST(RiskSeries, UsdDropsClosesBeforeFirstRate) {
    const std::vector<IndexLevel> egp{{D("2024-01-01"), 100.0}, {D("2024-01-03"), 101.0}};
    const std::vector<FxRate> fx{{D("2024-01-02"), 50.0}};
    const auto s = in_usd(egp, fx, 7);
    ASSERT_EQ(s.levels.size(), 1u);
    EXPECT_EQ(s.dropped, 1u);
}

TEST(RiskSeries, DeflateInterpolatesBetweenMidMonths) {
    // CPI 100 on Jan 15 and 110 on Feb 15 (31 days apart); reference = 110.
    const std::vector<CpiPoint> cpi{{2024y / January, 100.0}, {2024y / February, 110.0}};
    const std::vector<IndexLevel> egp{{D("2024-01-10"), 500.0},   // before first stamp: dropped
                                      {D("2024-01-15"), 500.0},   // on the stamp
                                      {D("2024-01-31"), 500.0},   // 16/31 of the way
                                      {D("2024-02-15"), 500.0},   // last stamp
                                      {D("2024-02-16"), 500.0}};  // after: dropped
    const auto s = deflated(egp, cpi);
    ASSERT_EQ(s.levels.size(), 3u);
    EXPECT_EQ(s.dropped, 2u);
    EXPECT_NEAR(s.levels[0].close, 500.0 * 110.0 / 100.0, 1e-9);
    const double w = 16.0 / 31.0;
    const double cpi_31 = std::exp((1 - w) * std::log(100.0) + w * std::log(110.0));
    EXPECT_NEAR(s.levels[1].close, 500.0 * 110.0 / cpi_31, 1e-9);
    EXPECT_NEAR(s.levels[2].close, 500.0, 1e-9);
}

TEST(RiskSeries, DeflateWithoutCpiDropsEverything) {
    const std::vector<IndexLevel> egp{{D("2024-01-10"), 500.0}};
    const auto s = deflated(egp, {});
    EXPECT_TRUE(s.levels.empty());
    EXPECT_EQ(s.dropped, 1u);
}

TEST(RiskSeries, IndexViewFromDatabase) {
    auto db = MarketDb::in_memory();
    db.insert(std::vector<IndexLevel>{{D("2024-01-15"), 1000.0}, {D("2024-02-15"), 1100.0}});
    db.insert(std::vector<FxRate>{{D("2023-12-20"), 30.0}, {D("2024-02-14"), 31.0}});
    db.insert(std::vector<CpiPoint>{{2024y / January, 200.0}, {2024y / February, 220.0}});

    const auto egp = index_view(db, D("2024-01-01"), D("2024-12-31"), RiskView::NominalEgp);
    ASSERT_EQ(egp.levels.size(), 2u);

    // The FX lookup reaches back before `from`, but 2023-12-20 is 26 days before
    // the first close, beyond the 7-day staleness limit, so that close is dropped.
    const auto usd = index_view(db, D("2024-01-01"), D("2024-12-31"), RiskView::Usd);
    ASSERT_EQ(usd.levels.size(), 1u);
    EXPECT_NEAR(usd.levels[0].close, 1100.0 / 31.0, 1e-9);

    const auto real = index_view(db, D("2024-01-01"), D("2024-12-31"), RiskView::RealEgp);
    ASSERT_EQ(real.levels.size(), 2u);
    EXPECT_NEAR(real.levels[0].close, 1000.0 * 220.0 / 200.0, 1e-9);
    EXPECT_NEAR(real.levels[1].close, 1100.0, 1e-9);

    EXPECT_EQ(db.fx_between(D("2024-01-01"), D("2024-12-31")).size(), 1u);
    EXPECT_EQ(db.cpi_between(2024y / January, 2024y / January).size(), 1u);
}
