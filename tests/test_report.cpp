#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "egxquant/core/format.hpp"
#include "egxquant/data/market_db.hpp"
#include "egxquant/data/synthetic.hpp"
#include "egxquant/report/chart.hpp"
#include "egxquant/report/dashboard.hpp"
#include "egxquant/risk/stress.hpp"

using namespace egxq;
using namespace egxq::report;

namespace {

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::size_t count(const std::string& s, const std::string& part) {
    std::size_t n = 0;
    for (auto p = s.find(part); p != std::string::npos; p = s.find(part, p + part.size())) ++n;
    return n;
}

}  // namespace

TEST(Format, Grouped) {
    EXPECT_EQ(fmt::grouped(51462.0, 2), "51,462.00");
    EXPECT_EQ(fmt::grouped(1234567.891, 1), "1,234,567.9");
    EXPECT_EQ(fmt::grouped(-0.004, 2), "0.00");
    EXPECT_EQ(fmt::grouped(-1999.996, 2), "-2,000.00");
    EXPECT_EQ(fmt::money(-1234.4), "-1,234");
    EXPECT_EQ(fmt::signed_pct(0.0512), "+5.1%");
}

TEST(Chart, Escape) { EXPECT_EQ(escape("a<b & \"c\" 'd'>"), "a&lt;b &amp; &quot;c&quot; &#39;d&#39;&gt;"); }

TEST(Chart, NiceTicks) {
    EXPECT_EQ(nice_ticks(0.0, 100.0, 5), (std::vector<double>{0, 20, 40, 60, 80, 100}));
    EXPECT_EQ(nice_ticks(23.0, 61.0, 5), (std::vector<double>{20, 30, 40, 50, 60, 70}));
    const auto t = nice_ticks(-4.2, 3.1, 5);
    EXPECT_EQ(t.front(), -6.0);
    EXPECT_EQ(t.back(), 4.0);
    EXPECT_EQ(nice_ticks(5.0, 5.0, 5).size() >= 2, true);  // flat series still gets an axis
}

TEST(Chart, DateTicksAreMonthStarts) {
    const double from = day_number(2023, 1, 2), to = day_number(2026, 11, 25);
    const auto t = date_ticks(from, to);
    ASSERT_GE(t.size(), 3u);
    EXPECT_LE(t.size(), 8u);
    EXPECT_EQ(t.front(), day_number(2024, 1, 1));  // 12-month steps over ~4 years
    for (double d : t) {
        EXPECT_GE(d, from);
        EXPECT_LE(d, to);
    }
    const auto short_range = date_ticks(day_number(2026, 3, 10), day_number(2026, 7, 20));
    EXPECT_EQ(short_range.front(), day_number(2026, 4, 1));
}

TEST(Chart, RendersSeriesLegendAndData) {
    Series a{"EGP <index>", {{day_number(2026, 1, 1), 100, ""}, {day_number(2026, 2, 1), 110, ""}}, Mark::Line,
             "--series-1"};
    Series b{"Jumps", {{day_number(2026, 1, 15), 105, "jump \"day\""}}, Mark::Dots, "--series-2"};
    ChartOptions o;
    o.title = "Test & chart";
    const auto html = time_chart({a, b}, o);
    EXPECT_TRUE(contains(html, "<svg"));
    EXPECT_TRUE(contains(html, "Test &amp; chart"));
    EXPECT_TRUE(contains(html, "EGP &lt;index&gt;"));  // escaped legend
    EXPECT_EQ(count(html, "<polyline"), 1u);
    EXPECT_EQ(count(html, "<circle"), 1u);
    EXPECT_TRUE(contains(html, "class=\"legend\""));
    EXPECT_TRUE(contains(html, "application/json"));
    EXPECT_TRUE(contains(html, "jump \\\"day\\\""));  // JSON-escaped label
    EXPECT_FALSE(contains(html, "nan,"));
    EXPECT_FALSE(contains(html, "-nan"));
}

TEST(Chart, EmptyAndSingleSeries) {
    ChartOptions o;
    o.title = "Nothing";
    EXPECT_TRUE(contains(time_chart({}, o), "No data yet"));
    Series one{"Only", {{day_number(2026, 11, 26), 0.6, ""}}, Mark::Dots, "--series-1"};
    const auto html = time_chart({one}, o);
    EXPECT_FALSE(contains(html, "class=\"legend\""));  // one series: the title names it
    EXPECT_TRUE(contains(html, "26 Nov 2026"));  // single-date axis label
}

TEST(Stress, InstantShockSettlesWithinLimit) {
    risk::StressPosition pos;
    pos.contracts = 1.0;
    pos.futures_price = 100.0;
    pos.fx = 10.0;
    risk::MarginRules rules;
    rules.initial = 10.0;
    rules.maintenance = 7.5;
    const auto r = risk::instant_shock(-0.25, 0.0, pos, rules, 1.0);
    EXPECT_EQ(r.locked_days, 2u);  // 90, 81, then 75
    EXPECT_NEAR(r.days.back().settle, 75.0, 1e-9);
    EXPECT_NEAR(r.days.back().pnl_egp, -25.0, 1e-9);
    EXPECT_THROW(risk::instant_shock(-1.0, 0.0, pos, rules, 1.0), std::invalid_argument);
}

TEST(Dashboard, BuildsFromSyntheticData) {
    auto db = MarketDb::in_memory();
    db.insert(generate_synthetic(SyntheticConfig{}));
    DashboardSettings s;
    s.mc_paths = 5000;
    s.margin_paths = 500;
    const auto html = build_dashboard(db, s);
    EXPECT_TRUE(contains(html, "<!doctype html>"));
    for (const char* id : {"snapshot", "futures", "prices", "risk", "stress", "data"})
        EXPECT_TRUE(contains(html, std::string("<section id=\"") + id + "\"")) << id;
    EXPECT_GE(count(html, "<figure class=\"chart\">"), 4u);
    EXPECT_TRUE(contains(html, "Kupiec"));
    EXPECT_TRUE(contains(html, "float-2024"));
    // No NaN or infinity printed anywhere (plain "nan" also appears in "maintenance").
    for (const char* bad : {"nan%", "nan<", ">nan", "nan,", ",nan", "-nan", "inf%", ">inf", "-inf"})
        EXPECT_FALSE(contains(html, bad)) << bad;
    // Nothing is loaded from the network.
    EXPECT_FALSE(contains(html, "http://"));
    EXPECT_FALSE(contains(html, "https://"));
    // Deterministic for a fixed seed and no timestamp.
    EXPECT_EQ(html, build_dashboard(db, s));
}

TEST(Dashboard, EmptyDatabaseStillRenders) {
    const auto db = MarketDb::in_memory();
    const auto html = build_dashboard(db, DashboardSettings{});
    EXPECT_TRUE(contains(html, "No futures settlements yet"));
    EXPECT_TRUE(contains(html, "No CPI data"));
    EXPECT_TRUE(contains(html, "</html>"));
}
