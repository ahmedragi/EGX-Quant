#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "egxquant/data/csv.hpp"
#include "egxquant/data/market_csv.hpp"
#include "test_util.hpp"

using namespace egxq;
namespace fs = std::filesystem;

TEST(Csv, ParsesHeaderAndRows) {
    const auto t = csv::parse("date,close\n2026-03-01,41500.5\n2026-03-02,41600\n");
    ASSERT_EQ(t.header.size(), 2u);
    ASSERT_EQ(t.rows.size(), 2u);
    EXPECT_EQ(t.rows[0].fields[1], "41500.5");
    EXPECT_EQ(t.rows[1].line, 3u);
}

TEST(Csv, HandlesQuotesCrlfBomAndBlankLines) {
    const std::string text = "\xEF\xBB\xBF" "Date,\"Price\"\r\n\r\n2026-03-01,\"41,500.25\"\r\n"
                             "2026-03-02,\"say \"\"hi\"\"\"\r\n";
    const auto t = csv::parse(text);
    EXPECT_EQ(t.header[0], "Date");
    EXPECT_EQ(t.header[1], "Price");
    ASSERT_EQ(t.rows.size(), 2u);
    EXPECT_EQ(t.rows[0].fields[1], "41,500.25");
    EXPECT_EQ(t.rows[1].fields[1], "say \"hi\"");
}

TEST(Csv, ColumnLookupIsCaseAndSpaceInsensitive) {
    const auto t = csv::parse(" Date , CLOSE \n2026-03-01,1\n");
    EXPECT_EQ(t.column("date"), 0u);
    EXPECT_EQ(t.column("close"), 1u);
    EXPECT_EQ(t.find_column("volume"), csv::Table::npos);
    EXPECT_THROW(t.column("volume"), std::runtime_error);
}

TEST(Csv, RejectsMalformedQuotes) {
    EXPECT_THROW(csv::parse("a,b\n1,\"unterminated\n"), std::runtime_error);
    EXPECT_THROW(csv::parse("a,b\n1,ab\"c\"\n"), std::runtime_error);
}

TEST(Csv, EscapeRoundTrip) {
    std::ostringstream out;
    csv::write_row(out, {"plain", "with,comma", "with \"quote\""});
    const auto t = csv::parse("h1,h2,h3\n" + out.str());
    ASSERT_EQ(t.rows.size(), 1u);
    EXPECT_EQ(t.rows[0].fields[1], "with,comma");
    EXPECT_EQ(t.rows[0].fields[2], "with \"quote\"");
}

TEST(MarketCsv, ParseNumber) {
    EXPECT_DOUBLE_EQ(parse_number("41,500.25"), 41500.25);
    EXPECT_DOUBLE_EQ(parse_number(" 22.5 "), 22.5);
    EXPECT_DOUBLE_EQ(parse_number("-1.5"), -1.5);
    EXPECT_THROW(parse_number(""), std::invalid_argument);
    EXPECT_THROW(parse_number("12abc"), std::invalid_argument);
    EXPECT_THROW(parse_number("n/a"), std::invalid_argument);
}

TEST(MarketCsv, ParseMonth) {
    const auto ym = parse_month("2026-03");
    EXPECT_EQ(static_cast<int>(ym.year()), 2026);
    EXPECT_EQ(static_cast<unsigned>(ym.month()), 3u);
    EXPECT_EQ(format_month(ym), "2026-03");
    EXPECT_THROW(parse_month("2026-13"), std::invalid_argument);
    EXPECT_THROW(parse_month("2026/03"), std::invalid_argument);
}

TEST(MarketCsv, ReadsColumnsInAnyOrderWithExtras) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "egx30.csv",
                         "Volume,Close,Date\n1000,\"41,500.25\",2026-03-01\n900,41600,2026-03-02\n");
    const auto rows = read_index_csv(dir.path() / "egx30.csv");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(format_date(rows[0].date), "2026-03-01");
    EXPECT_DOUBLE_EQ(rows[0].close, 41500.25);
}

TEST(MarketCsv, ErrorsNameFileAndLine) {
    testutil::TempDir dir;
    const fs::path p = dir.path() / "tbills.csv";
    testutil::write_text(p, "date,tenor_days,yield_pct\n2026-03-01,91,22.5\n2026-03-05,91,abc\n");
    try {
        read_tbills_csv(p);
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("tbills.csv:3"), std::string::npos) << msg;
    }
}

TEST(MarketCsv, RejectsMissingColumnAndBadValues) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "a.csv", "date,price\n2026-03-01,1\n");
    EXPECT_THROW(read_index_csv(dir.path() / "a.csv"), std::runtime_error);

    testutil::write_text(dir.path() / "b.csv", "date,close\n2026-03-01,-5\n");
    EXPECT_THROW(read_index_csv(dir.path() / "b.csv"), std::runtime_error);

    testutil::write_text(dir.path() / "c.csv",
                         "date,contract,expiry,settle\n2026-04-01,EGX30-202603,2026-03-26,40000\n");
    EXPECT_THROW(read_futures_csv(dir.path() / "c.csv"), std::runtime_error);  // date after expiry
}

TEST(MarketCsv, FuturesOptionalColumnsDefaultToZero) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "f.csv",
                         "date,contract,expiry,settle\n2026-03-01,EGX30-202603,2026-03-26,40000\n");
    const auto rows = read_futures_csv(dir.path() / "f.csv");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].volume, 0);
    EXPECT_EQ(rows[0].open_interest, 0);
}

TEST(MarketCsv, WriteReadRoundTrip) {
    testutil::TempDir dir;
    MarketData d;
    d.index_levels = {{parse_date("2026-03-01"), 41500.25}};
    d.futures = {{parse_date("2026-03-01"), "EGX30-202603", parse_date("2026-03-26"), 42000.5, 10, 20}};
    d.tbills = {{parse_date("2026-03-01"), 91, 22.125}};
    d.cpi = {{parse_month("2026-02"), 150.5}};
    d.fx = {{parse_date("2026-03-01"), 50.1234}};
    write_market_dir(dir.path(), d);

    const MarketData r = read_market_dir(dir.path());
    ASSERT_EQ(r.index_levels.size(), 1u);
    EXPECT_DOUBLE_EQ(r.index_levels[0].close, 41500.25);
    ASSERT_EQ(r.futures.size(), 1u);
    EXPECT_EQ(r.futures[0].contract, "EGX30-202603");
    EXPECT_EQ(r.futures[0].open_interest, 20);
    ASSERT_EQ(r.tbills.size(), 1u);
    EXPECT_DOUBLE_EQ(r.tbills[0].yield_pct, 22.125);
    ASSERT_EQ(r.cpi.size(), 1u);
    EXPECT_EQ(format_month(r.cpi[0].month), "2026-02");
    ASSERT_EQ(r.fx.size(), 1u);
    EXPECT_DOUBLE_EQ(r.fx[0].rate, 50.1234);
}

TEST(MarketCsv, ReadDirSkipsMissingFiles) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "usdegp.csv", "date,rate\n2026-03-01,50.5\n");
    const MarketData r = read_market_dir(dir.path());
    EXPECT_EQ(r.fx.size(), 1u);
    EXPECT_TRUE(r.index_levels.empty());
    EXPECT_TRUE(r.futures.empty());
}
