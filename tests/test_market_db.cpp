#include <gtest/gtest.h>

#include <stdexcept>

#include "egxquant/data/market_csv.hpp"
#include "egxquant/data/market_db.hpp"
#include "test_util.hpp"

using namespace egxq;

namespace {

/// In-memory database loaded from tests/fixtures/market (hand-written values).
MarketDb fixture_db() {
    MarketDb db = MarketDb::in_memory();
    db.insert(read_market_dir(EGXQ_FIXTURE_DIR "/market"));
    return db;
}

Date D(const char* s) { return parse_date(s); }

}  // namespace

TEST(MarketDb, LoadsFixture) {
    const auto s = fixture_db().summary();
    EXPECT_EQ(s.index_levels, 4u);
    EXPECT_EQ(s.futures, 3u);
    EXPECT_EQ(s.tbills, 6u);
    EXPECT_EQ(s.cpi, 2u);
    EXPECT_EQ(s.fx, 2u);
}

TEST(MarketDb, IndexLookups) {
    const auto db = fixture_db();
    EXPECT_DOUBLE_EQ(*db.index_close(D("2026-03-02")), 37100.50);
    EXPECT_FALSE(db.index_close(D("2026-03-06")).has_value());  // Friday, no trading

    const auto prev = db.index_close_on_or_before(D("2026-03-07"));  // Saturday
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(format_date(prev->date), "2026-03-05");
    EXPECT_DOUBLE_EQ(prev->close, 37250.0);

    EXPECT_FALSE(db.index_close_on_or_before(D("2026-02-28")).has_value());
}

TEST(MarketDb, FxAndCpiLookups) {
    const auto db = fixture_db();
    const auto fx = db.fx_on_or_before(D("2026-03-04"));
    ASSERT_TRUE(fx.has_value());
    EXPECT_DOUBLE_EQ(fx->rate, 50.5);
    EXPECT_DOUBLE_EQ(db.fx_on_or_before(D("2026-03-31"))->rate, 50.75);

    EXPECT_DOUBLE_EQ(*db.cpi(parse_month("2026-02")), 152.4);
    EXPECT_FALSE(db.cpi(parse_month("2026-03")).has_value());
}

TEST(MarketDb, TbillUsesLatestAuctionPerTenor) {
    const auto db = fixture_db();
    // Before the 2026-03-05 auction, the 91-day rate is the 2026-02-26 one.
    auto q = db.tbill_for_horizon(D("2026-03-04"), 91);
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(q->tenor_days, 91);
    EXPECT_DOUBLE_EQ(q->yield_pct, 22.0);
    EXPECT_EQ(format_date(q->auction_date), "2026-02-26");

    // On the auction day the new yield applies.
    q = db.tbill_for_horizon(D("2026-03-05"), 91);
    EXPECT_DOUBLE_EQ(q->yield_pct, 21.75);

    // 182-day is only available from its 2026-03-01 auction.
    EXPECT_EQ(db.tbill_for_horizon(D("2026-02-27"), 182)->tenor_days, 91);
    EXPECT_EQ(db.tbill_for_horizon(D("2026-03-01"), 182)->tenor_days, 182);
}

TEST(MarketDb, TbillPicksNearestTenor) {
    const auto db = fixture_db();
    const Date d = D("2026-03-05");
    EXPECT_EQ(db.tbill_for_horizon(d, 25)->tenor_days, 91);    // shorter than all
    EXPECT_EQ(db.tbill_for_horizon(d, 136)->tenor_days, 91);   // 45 vs 46 away
    EXPECT_EQ(db.tbill_for_horizon(d, 137)->tenor_days, 182);  // 46 vs 45 away
    EXPECT_EQ(db.tbill_for_horizon(d, 300)->tenor_days, 273);
    EXPECT_EQ(db.tbill_for_horizon(d, 999)->tenor_days, 364);
    EXPECT_FALSE(db.tbill_for_horizon(D("2026-02-25"), 91).has_value());
}

TEST(MarketDb, TbillTieGoesToShorterTenor) {
    MarketDb db = MarketDb::in_memory();
    db.insert(std::vector<TbillYield>{{D("2026-03-01"), 100, 20.0}, {D("2026-03-01"), 200, 21.0}});
    EXPECT_EQ(db.tbill_for_horizon(D("2026-03-01"), 150)->tenor_days, 100);
}

TEST(MarketDb, FuturesOrderedByExpiry) {
    const auto db = fixture_db();
    const auto f = db.futures_on(D("2026-03-01"));
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0].contract, "EGX30-202603");
    EXPECT_EQ(format_date(f[0].expiry), "2026-03-26");
    EXPECT_DOUBLE_EQ(f[0].settle, 37560.0);
    EXPECT_EQ(f[0].volume, 500);
    EXPECT_EQ(f[0].open_interest, 1500);
    EXPECT_EQ(f[1].contract, "EGX30-202604");
    EXPECT_TRUE(db.futures_on(D("2026-03-05")).empty());
}

TEST(MarketDb, Ranges) {
    const auto db = fixture_db();
    const auto idx = db.index_between(D("2026-03-02"), D("2026-03-05"));
    ASSERT_EQ(idx.size(), 2u);
    EXPECT_EQ(format_date(idx[0].date), "2026-03-02");
    EXPECT_EQ(format_date(idx[1].date), "2026-03-05");

    const auto fut = db.futures_between(D("2026-03-01"), D("2026-03-31"));
    ASSERT_EQ(fut.size(), 3u);
    EXPECT_EQ(format_date(fut[2].date), "2026-03-02");
}

TEST(MarketDb, ReloadIsIdempotentAndCorrectionsOverwrite) {
    MarketDb db = fixture_db();
    db.insert(read_market_dir(EGXQ_FIXTURE_DIR "/market"));
    EXPECT_EQ(db.summary().index_levels, 4u);
    EXPECT_EQ(db.summary().futures, 3u);

    db.insert(std::vector<IndexLevel>{{D("2026-03-02"), 37111.0}});
    EXPECT_DOUBLE_EQ(*db.index_close(D("2026-03-02")), 37111.0);
    EXPECT_EQ(db.summary().index_levels, 4u);
}

TEST(MarketDb, FailedInsertRollsBackWholeBatch) {
    MarketDb db = MarketDb::in_memory();
    const std::vector<IndexLevel> rows{{D("2026-03-01"), 100.0}, {D("2026-03-02"), -1.0}};  // CHECK fails
    EXPECT_THROW(db.insert(rows), std::runtime_error);
    EXPECT_EQ(db.summary().index_levels, 0u);

    db.insert(std::vector<IndexLevel>{{D("2026-03-01"), 100.0}});  // db still usable
    EXPECT_EQ(db.summary().index_levels, 1u);
}

TEST(MarketDb, PersistsToFile) {
    testutil::TempDir dir;
    const auto path = dir.path() / "egx.db";
    {
        MarketDb db(path);
        db.insert(read_market_dir(EGXQ_FIXTURE_DIR "/market"));
    }
    const MarketDb reopened(path);
    EXPECT_EQ(reopened.summary().tbills, 6u);
    EXPECT_DOUBLE_EQ(*reopened.index_close(D("2026-03-08")), 37400.0);
}
