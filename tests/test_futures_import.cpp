#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <string>

#include "egxquant/core/rates.hpp"
#include "egxquant/data/basis_history.hpp"
#include "egxquant/data/html_table.hpp"
#include "egxquant/data/importers.hpp"
#include "egxquant/futures/cost_of_carry.hpp"
#include "test_util.hpp"

using namespace egxq;

namespace {

/// An English-language bulletin with the columns in the order the English
/// page shows them (operations first, contract code last).
std::string english_bulletin(const std::string& dec_settle, const std::string& dec_value) {
    return "<html><body><table id='grid'>"
           "<tr><th>NUMBER OF OPERATIONS</th><th>TOTAL TRADING VOLUME</th><th>TOTAL TRADING VALUE</th>"
           "<th>PRICE CHANGE PERCENTAGE</th><th>CLOSING AND SETTLEMENT PRICE</th><th>LAST EXECUTION PRICE</th>"
           "<th>CLOSING AND SETTLEMENT PRICE FOR THE PREVIOUS DAY</th><th>OPENING PRICE</th>"
           "<th>INITIAL MARGIN FOR EACH CONTRACT</th><th>CONTRACT CODE*</th></tr>"
           "<tr><td colspan='10'>Index futures</td></tr>"
           "<tr><td>2</td><td>6</td><td>" + dec_value + "</td><td>-0.89%</td><td>" + dec_settle +
           "</td><td>51462.000</td><td>51925.000</td><td>51925.000</td><td>5500.000</td>"
           "<td><table><tr><td>EGX30 Index Futures - December 2026</td></tr></table></td></tr>"
           "<tr><td>0</td><td>0</td><td>0</td><td>0.00%</td><td>54100.000</td><td>&nbsp;</td><td>54230.000</td>"
           "<td>54230.000</td><td>5900.000</td><td>EGX30 Index Futures - March 2027</td></tr>"
           "<tr><td>2</td><td>24</td><td>308,208</td><td>-0.50%</td><td>128.420</td><td>128.420</td><td>129.070</td>"
           "<td>129.070</td><td>1500.000</td><td>Commercial International Bank Futures - December 2026</td></tr>"
           "<tr><td>4</td><td>30</td><td>1,877,950</td><td>Total (3)</td></tr>"
           "</table></body></html>";
}

std::vector<FuturesSettlement> read_english(const std::string& html) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "bulletin.html", html);
    return read_egx_futures_bulletin_html(dir.path() / "bulletin.html", parse_date("2026-11-26")).rows;
}

}  // namespace

TEST(HtmlTable, NestedTablesEntitiesAndScripts) {
    const auto tables = html::read_tables(
        "<script>var s = '<table><tr><td>not a table</td></tr></table>';</script>"
        "<!-- <table><tr><td>comment</td></tr></table> -->"
        "<table id=\"outer\"><tr><th>A &amp; B</th><td>x<table><tr><td>inner&nbsp;1</td><td>2</td></tr></table></td></tr>"
        "<tr><td>  spaced\n   out  </td><td>&#1605;</td></tr></table>");
    ASSERT_EQ(tables.size(), 2u);
    EXPECT_EQ(tables[0].id, "outer");
    ASSERT_EQ(tables[0].rows.size(), 2u);
    ASSERT_EQ(tables[0].rows[0].size(), 2u);
    EXPECT_EQ(tables[0].rows[0][0], "A & B");
    EXPECT_EQ(tables[0].rows[0][1], "x inner 1 2");  // nested table text counts toward the cell
    EXPECT_EQ(tables[0].rows[1][0], "spaced out");
    EXPECT_EQ(tables[0].rows[1][1], "\xd9\x85");  // numeric entity, UTF-8 encoded
    ASSERT_EQ(tables[1].rows.size(), 1u);
    EXPECT_EQ(tables[1].rows[0][0], "inner 1");
}

TEST(FuturesBulletin, ReadsRealArabicPage) {
    const auto b = read_egx_futures_bulletin_html(EGXQ_FIXTURE_DIR "/bulletins/egx-futures-2026-11-26-ar.html",
                                                  parse_date("2026-11-26"));
    ASSERT_EQ(b.rows.size(), 2u);
    EXPECT_EQ(b.other_contracts, 4u);  // CIB and TMGH, Dec and Mar
    EXPECT_EQ(b.no_settlement, 0u);

    EXPECT_EQ(b.rows[0].contract, "EGX30FDEC26");
    EXPECT_EQ(format_date(b.rows[0].expiry), "2026-12-16");
    EXPECT_EQ(format_date(b.rows[0].date), "2026-11-26");
    EXPECT_DOUBLE_EQ(b.rows[0].settle, 51462.0);
    EXPECT_EQ(b.rows[0].volume, 6);
    EXPECT_EQ(b.rows[0].open_interest, 0);

    EXPECT_EQ(b.rows[1].contract, "EGX30FMAR27");
    EXPECT_EQ(format_date(b.rows[1].expiry), "2027-03-17");
    EXPECT_DOUBLE_EQ(b.rows[1].settle, 53747.0);
}

TEST(FuturesBulletin, ReadsEnglishPageInAnyColumnOrder) {
    const auto rows = read_english(english_bulletin("51462.000", "308,772"));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].contract, "EGX30FDEC26");
    EXPECT_DOUBLE_EQ(rows[0].settle, 51462.0);
    EXPECT_EQ(rows[0].volume, 6);
    EXPECT_EQ(rows[1].contract, "EGX30FMAR27");  // no trades: settlement still recorded
    EXPECT_DOUBLE_EQ(rows[1].settle, 54100.0);
    EXPECT_EQ(rows[1].volume, 0);
}

TEST(FuturesBulletin, RejectsMisplacedDecimal) {
    // "53747000" style display bug: 1000x the previous settlement.
    try {
        read_english(english_bulletin("51462000", "308,772"));
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("10% daily limit"), std::string::npos) << msg;
        EXPECT_NE(msg.find("December 2026"), std::string::npos) << msg;
    }
}

TEST(FuturesBulletin, RejectsValueThatDoesNotMatchVolume) {
    // 6 contracts at ~51,462 must be worth ~308,772 EGP, not 30,877.
    EXPECT_THROW(read_english(english_bulletin("51462.000", "30,877")), std::runtime_error);
}

TEST(FuturesBulletin, RejectsPagesWithoutTheTable) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "x.html", "<table><tr><td>Date</td><td>Price</td></tr></table>");
    EXPECT_THROW(read_egx_futures_bulletin_html(dir.path() / "x.html", parse_date("2026-11-26")), std::runtime_error);
}

TEST(FuturesBulletin, RejectsTradeDateAfterExpiry) {
    testutil::TempDir dir;
    testutil::write_text(dir.path() / "b.html", english_bulletin("51462.000", "308,772"));
    EXPECT_THROW(read_egx_futures_bulletin_html(dir.path() / "b.html", parse_date("2026-12-17")), std::runtime_error);
}

TEST(FuturesMerge, KeysOnDateAndContract) {
    const Date d1 = parse_date("2026-11-25"), d2 = parse_date("2026-11-26");
    const Date dec = parse_date("2026-12-16"), mar = parse_date("2027-03-17");
    const std::vector<FuturesSettlement> base{{d1, "EGX30FMAR27", mar, 54230.0, 4, 0},
                                              {d1, "EGX30FDEC26", dec, 51925.0, 4, 0}};
    const std::vector<FuturesSettlement> update{{d2, "EGX30FDEC26", dec, 51462.0, 6, 0},
                                                {d1, "EGX30FDEC26", dec, 51925.0, 5, 0}};  // corrected volume
    const auto m = merge_by_date(base, update);
    EXPECT_EQ(m.added, 1u);
    EXPECT_EQ(m.changed, 1u);
    ASSERT_EQ(m.rows.size(), 3u);
    EXPECT_EQ(m.rows[0].contract, "EGX30FDEC26");  // by date, then expiry
    EXPECT_EQ(m.rows[0].volume, 5);
    EXPECT_EQ(m.rows[1].contract, "EGX30FMAR27");
    EXPECT_EQ(format_date(m.rows[2].date), "2026-11-26");
}

TEST(BasisHistory, MatchesHandCalculation) {
    MarketDb db = MarketDb::in_memory();
    db.insert(std::vector<IndexLevel>{{parse_date("2026-11-25"), 50538.66}});
    db.insert(std::vector<TbillYield>{{parse_date("2026-11-24"), 91, 24.917}, {parse_date("2026-11-24"), 182, 25.358}});
    db.insert(std::vector<FuturesSettlement>{
        {parse_date("2026-11-25"), "EGX30FDEC26", parse_date("2026-12-16"), 51462.0, 6, 0},
        {parse_date("2026-11-26"), "EGX30FMAR27", parse_date("2027-03-17"), 53747.0, 0, 0}});

    BasisSettings s;
    s.dividend_yield_pct = 3.0;
    const auto h = basis_history(db, parse_date("2026-01-01"), parse_date("2026-12-31"), s);
    ASSERT_EQ(h.rows.size(), 2u);

    const auto& dec = h.rows[0];
    EXPECT_EQ(dec.days_to_expiry, 21);
    EXPECT_FALSE(dec.spot_is_stale);
    EXPECT_EQ(dec.rate.tenor_days, 91);
    const double T = 21.0 / 365.0;
    const double r = simple_to_continuous(0.24917, T);
    const double q = annual_to_continuous(0.03);
    const double fair = 50538.66 * std::exp((r - q) * T);
    EXPECT_NEAR(dec.report.fair_value, fair, 1e-6);
    EXPECT_NEAR(dec.report.mispricing, 51462.0 - fair, 1e-6);
    EXPECT_NEAR(dec.exchange_formula, 50538.66 * std::exp(0.24917 * T), 1e-6);
    // Implied rate reproduces the settlement.
    const double r_impl = simple_to_continuous(dec.implied_rate_pct / 100.0, T);
    EXPECT_NEAR(fair_value(CarryInputs{50538.66, r_impl, q, T}), 51462.0, 1e-6);

    const auto& mar = h.rows[1];
    EXPECT_TRUE(mar.spot_is_stale);  // no close on 2026-11-26 in this database
    EXPECT_EQ(format_date(mar.spot.date), "2026-11-25");
    EXPECT_EQ(mar.days_to_expiry, 111);
    EXPECT_EQ(mar.rate.tenor_days, 91);  // 111 days is nearer 91 than 182
}

TEST(BasisHistory, SkipsDaysWithoutSpotOrRate) {
    MarketDb db = MarketDb::in_memory();
    db.insert(std::vector<FuturesSettlement>{
        {parse_date("2026-11-26"), "EGX30FDEC26", parse_date("2026-12-16"), 51462.0, 6, 0}});
    auto h = basis_history(db, parse_date("2026-01-01"), parse_date("2026-12-31"));
    EXPECT_TRUE(h.rows.empty());
    EXPECT_EQ(h.skipped_no_spot, 1u);

    db.insert(std::vector<IndexLevel>{{parse_date("2026-11-26"), 50500.0}});
    h = basis_history(db, parse_date("2026-01-01"), parse_date("2026-12-31"));
    EXPECT_EQ(h.skipped_no_rate, 1u);
}

TEST(FuturesRepeat, DetectsHolidayCopyOfPreviousDay) {
    const Date dec = parse_date("2026-12-16"), mar = parse_date("2027-03-17");
    const std::vector<FuturesSettlement> existing{
        {parse_date("2026-11-25"), "EGX30FDEC26", dec, 51925.0, 4, 0},
        {parse_date("2026-11-26"), "EGX30FDEC26", dec, 51462.0, 6, 0},
        {parse_date("2026-11-26"), "EGX30FMAR27", mar, 53747.0, 6, 0}};

    // Saved on Sunday 2026-11-29 but showing Thursday's numbers.
    const std::vector<FuturesSettlement> same{{parse_date("2026-11-29"), "EGX30FDEC26", dec, 51462.0, 6, 0},
                                              {parse_date("2026-11-29"), "EGX30FMAR27", mar, 53747.0, 6, 0}};
    const auto prev = repeats_previous_day(existing, same);
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(format_date(*prev), "2026-11-26");

    // A real new day differs in at least one price or volume.
    auto moved = same;
    moved[1].settle = 53800.0;
    EXPECT_FALSE(repeats_previous_day(existing, moved).has_value());

    // A missing contract is not a repeat either.
    EXPECT_FALSE(repeats_previous_day(existing, {same[0]}).has_value());

    // Re-importing the same day is not a "repeat of an earlier day".
    std::vector<FuturesSettlement> again = same;
    for (auto& f : again) f.date = parse_date("2026-11-26");
    const auto p = repeats_previous_day(existing, again);
    EXPECT_FALSE(p.has_value() && format_date(*p) == "2026-11-26");
}
