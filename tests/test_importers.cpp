#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "egxquant/data/importers.hpp"
#include "test_util.hpp"

using namespace egxq;

namespace {

const char* kHeader = "\"Date\",\"Price\",\"Open\",\"High\",\"Low\",\"Vol.\",\"Change %\"\n";

std::vector<DatedPrice> read_text(const std::string& body) {
    testutil::TempDir dir;
    const auto p = dir.path() / "export.csv";
    testutil::write_text(p, kHeader + body);
    return read_investing_history(p);
}

}  // namespace

TEST(Investing, ReadsRealExportFixture) {
    // First rows of a real Investing.com EGX30 export: BOM, quotes, newest first.
    const auto rows = read_investing_history(EGXQ_FIXTURE_DIR "/investing/EGX 30 Historical Data.csv");
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(format_date(rows.front().date), "2026-11-19");  // sorted ascending
    EXPECT_DOUBLE_EQ(rows.front().price, 51458.04);
    EXPECT_EQ(format_date(rows.back().date), "2026-11-25");
    EXPECT_DOUBLE_EQ(rows.back().price, 50538.66);
}

TEST(Investing, DetectsDayFirstDates) {
    const auto rows = read_text("\"25/11/2026\",\"100.5\",,,,,\n\"03/11/2026\",\"99\",,,,,\n");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(format_date(rows[0].date), "2026-11-03");
    EXPECT_EQ(format_date(rows[1].date), "2026-11-25");
}

TEST(Investing, AmbiguousDatesDefaultToMonthFirst) {
    const auto rows = read_text("\"03/11/2026\",\"99\",,,,,\n");
    EXPECT_EQ(format_date(rows[0].date), "2026-03-11");
}

TEST(Investing, AcceptsMonthNameAndIsoDates) {
    const auto rows = read_text("\"Nov 25, 2026\",\"50,538.66\",,,,,\n\"2026-11-24\",\"50,885.33\",,,,,\n");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(format_date(rows[0].date), "2026-11-24");
    EXPECT_EQ(format_date(rows[1].date), "2026-11-25");
    EXPECT_DOUBLE_EQ(rows[1].price, 50538.66);
}

TEST(Investing, RejectsBadInput) {
    EXPECT_THROW(read_text("\"11/25/2026\",\"1\",,,,,\n\"11/25/2026\",\"2\",,,,,\n"), std::runtime_error);  // dup
    EXPECT_THROW(read_text("\"25/11/2026\",\"1\",,,,,\n\"11/25/2026\",\"2\",,,,,\n"), std::runtime_error);  // mixed
    EXPECT_THROW(read_text("\"02/30/2026\",\"1\",,,,,\n"), std::runtime_error);   // no such day
    EXPECT_THROW(read_text("\"11/25/26\",\"1\",,,,,\n"), std::runtime_error);     // 2-digit year
    EXPECT_THROW(read_text("\"11/25/2026\",\"-\",,,,,\n"), std::runtime_error);   // missing price

    testutil::TempDir dir;
    testutil::write_text(dir.path() / "x.csv", "date,close\n2026-11-25,1\n");
    EXPECT_THROW(read_investing_history(dir.path() / "x.csv"), std::runtime_error);  // not this format
}

TEST(Investing, ErrorNamesLine) {
    try {
        read_text("\"11/25/2026\",\"1\",,,,,\n\"11/24/2026\",\"n/a\",,,,,\n");
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("export.csv:3"), std::string::npos) << e.what();
    }
}

TEST(Merge, UpdateWinsAndCountsChanges) {
    const std::vector<IndexLevel> base{{parse_date("2026-11-01"), 100.0}, {parse_date("2026-11-02"), 101.0}};
    const std::vector<IndexLevel> update{{parse_date("2026-11-03"), 103.0},
                                         {parse_date("2026-11-02"), 101.5},
                                         {parse_date("2026-11-01"), 100.0}};
    const auto m = merge_by_date(base, update);
    EXPECT_EQ(m.added, 1u);
    EXPECT_EQ(m.changed, 1u);
    ASSERT_EQ(m.rows.size(), 3u);
    EXPECT_EQ(format_date(m.rows[0].date), "2026-11-01");
    EXPECT_DOUBLE_EQ(m.rows[1].close, 101.5);
    EXPECT_EQ(format_date(m.rows[2].date), "2026-11-03");
}

TEST(Merge, FxRates) {
    const auto m = merge_by_date(std::vector<FxRate>{}, to_fx_rates({{parse_date("2026-11-25"), 50.25}}));
    EXPECT_EQ(m.added, 1u);
    ASSERT_EQ(m.rows.size(), 1u);
    EXPECT_DOUBLE_EQ(m.rows[0].rate, 50.25);
}
