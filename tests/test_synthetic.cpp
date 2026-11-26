#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <stdexcept>

#include "egxquant/data/market_csv.hpp"
#include "egxquant/data/market_db.hpp"
#include "egxquant/data/synthetic.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "test_util.hpp"

using namespace egxq;
using namespace std::chrono;

namespace {

SyntheticConfig small_config() {
    SyntheticConfig cfg;
    cfg.start = parse_date("2026-01-01");
    cfg.end = parse_date("2026-06-30");
    cfg.futures_start = parse_date("2026-03-01");
    return cfg;
}

}  // namespace

TEST(Synthetic, CalendarHelpers) {
    EXPECT_TRUE(is_egx_weekday(parse_date("2026-03-01")));   // Sunday
    EXPECT_TRUE(is_egx_weekday(parse_date("2026-03-05")));   // Thursday
    EXPECT_FALSE(is_egx_weekday(parse_date("2026-03-06")));  // Friday
    EXPECT_FALSE(is_egx_weekday(parse_date("2026-03-07")));  // Saturday
}

TEST(Synthetic, SameSeedSameData) {
    const auto a = generate_synthetic(small_config());
    const auto b = generate_synthetic(small_config());
    ASSERT_EQ(a.index_levels.size(), b.index_levels.size());
    for (std::size_t i = 0; i < a.index_levels.size(); ++i)
        EXPECT_EQ(a.index_levels[i].close, b.index_levels[i].close);
    ASSERT_EQ(a.futures.size(), b.futures.size());
    EXPECT_EQ(a.futures.back().settle, b.futures.back().settle);

    auto cfg = small_config();
    cfg.seed = 7;
    const auto c = generate_synthetic(cfg);
    EXPECT_NE(a.index_levels.back().close, c.index_levels.back().close);
}

TEST(Synthetic, OnlyEgxTradingDays) {
    const auto d = generate_synthetic(small_config());
    ASSERT_FALSE(d.index_levels.empty());
    EXPECT_EQ(d.index_levels.size(), d.fx.size());
    for (const auto& r : d.index_levels) {
        EXPECT_TRUE(is_egx_weekday(r.date)) << format_date(r.date);
        EXPECT_GT(r.close, 0.0);
    }
    EXPECT_EQ(format_date(d.index_levels.front().date), "2026-01-01");  // a Thursday
    EXPECT_DOUBLE_EQ(d.index_levels.front().close, small_config().index_start);
}

TEST(Synthetic, TbillAuctionSchedule) {
    const auto d = generate_synthetic(small_config());
    ASSERT_FALSE(d.tbills.empty());
    for (const auto& t : d.tbills) {
        const weekday wd{sys_days{t.date}};
        if (t.tenor_days == 91 || t.tenor_days == 273) EXPECT_EQ(wd, Thursday);
        else if (t.tenor_days == 182 || t.tenor_days == 364) EXPECT_EQ(wd, Sunday);
        else ADD_FAILURE() << "unexpected tenor " << t.tenor_days;
        EXPECT_GT(t.yield_pct, 0.0);
        EXPECT_LT(t.yield_pct, 100.0);
    }
}

TEST(Synthetic, FuturesFollowQuarterlyContractCycle) {
    const auto cfg = small_config();
    const auto d = generate_synthetic(cfg);
    ASSERT_FALSE(d.futures.empty());

    std::map<std::string, double> spot_by_date;
    for (const auto& r : d.index_levels) spot_by_date[format_date(r.date)] = r.close;

    std::map<std::string, int> per_day;
    for (const auto& f : d.futures) {
        EXPECT_GE(sys_days{f.date}, sys_days{cfg.futures_start});
        EXPECT_GE(days_between(f.date, f.expiry), 0);
        EXPECT_LE(days_between(f.date, f.expiry), 190);  // second contract: at most ~6 months
        const year_month ym = f.expiry.year() / f.expiry.month();
        EXPECT_TRUE(egx30::is_quarterly_month(ym.month())) << f.contract;
        EXPECT_EQ(f.expiry, egx30::expiry_date(ym));
        EXPECT_EQ(f.contract, egx30::contract_code(ym));
        // With rates far above the dividend yield, futures trade above spot.
        const double ratio = f.settle / spot_by_date.at(format_date(f.date));
        EXPECT_GT(ratio, 0.97);
        EXPECT_LT(ratio, 1.20);
        ++per_day[format_date(f.date)];
    }
    for (const auto& [day, n] : per_day) EXPECT_EQ(n, 2) << day;
    EXPECT_EQ(d.futures.front().contract, "EGX30FMAR26");  // on 2026-03-01 the March contract is front
}

TEST(Synthetic, CpiOnePointPerMonthAndRising) {
    const auto d = generate_synthetic(small_config());
    ASSERT_EQ(d.cpi.size(), 6u);
    EXPECT_EQ(format_month(d.cpi.front().month), "2026-01");
    EXPECT_EQ(format_month(d.cpi.back().month), "2026-06");
    EXPECT_GT(d.cpi.back().index, d.cpi.front().index);
}

TEST(Synthetic, RejectsBadRange) {
    auto cfg = small_config();
    cfg.end = parse_date("2025-12-31");
    EXPECT_THROW(generate_synthetic(cfg), std::invalid_argument);
}

TEST(Synthetic, CsvAndDatabaseRoundTrip) {
    const auto data = generate_synthetic(small_config());
    testutil::TempDir dir;
    write_market_dir(dir.path(), data);

    MarketDb db = MarketDb::in_memory();
    db.insert(read_market_dir(dir.path()));
    const auto s = db.summary();
    EXPECT_EQ(s.index_levels, data.index_levels.size());
    EXPECT_EQ(s.futures, data.futures.size());
    EXPECT_EQ(s.tbills, data.tbills.size());
    EXPECT_EQ(s.cpi, data.cpi.size());
    EXPECT_EQ(s.fx, data.fx.size());

    const auto& last_row = data.index_levels.back();
    EXPECT_DOUBLE_EQ(*db.index_close(last_row.date), last_row.close);
}
