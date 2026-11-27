#include "egxquant/report/dashboard.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "egxquant/core/format.hpp"
#include "egxquant/data/risk_series.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "egxquant/report/chart.hpp"
#include "egxquant/risk/calibration.hpp"
#include "egxquant/risk/jump_diffusion.hpp"
#include "egxquant/risk/scenarios.hpp"
#include "egxquant/risk/stress.hpp"
#include "egxquant/risk/var.hpp"

namespace egxq::report {

namespace {

using fmt::grouped;
using fmt::money;
using fmt::num;
using fmt::pct;
using fmt::signed_pct;

constexpr double kDaysPerYear = 250.0;

const Date kMinDate = parse_date("1900-03-01");
const Date kMaxDate = parse_date("2999-12-31");

double day_of(const Date& d) { return static_cast<double>(std::chrono::sys_days{d}.time_since_epoch().count()); }

std::string h(std::string_view s) { return escape(s); }

/// Sign class for a number shown in a table: "up" / "down" / "".
std::string tone(double v) { return v > 0 ? "up" : (v < 0 ? "down" : ""); }

std::string td(const std::string& text, const std::string& cls = "") {
    return cls.empty() ? "<td>" + text + "</td>" : "<td class=\"" + cls + "\">" + text + "</td>";
}

std::string tile(const std::string& label, const std::string& value, const std::string& note,
                 const std::string& delta = "", double delta_sign = 0.0) {
    std::string out = "<div class=\"tile\"><div class=\"label\">" + h(label) + "</div><div class=\"value\">" + value +
                      "</div>";
    if (!delta.empty()) {
        const char* arrow = delta_sign > 0 ? "&#9650; " : (delta_sign < 0 ? "&#9660; " : "");
        out += "<div class=\"delta " + tone(delta_sign) + "\">" + arrow + h(delta) + "</div>";
    }
    return out + "<div class=\"note\">" + h(note) + "</div></div>";
}

std::string section(const std::string& id, const std::string& title, const std::string& intro,
                    const std::string& body) {
    std::string out = "<section id=\"" + id + "\"><h2>" + h(title) + "</h2>";
    if (!intro.empty()) out += "<p class=\"intro\">" + intro + "</p>";
    return out + body + "</section>\n";
}

/// Position returns (long or short) from log returns.
double position_return(double log_ret, bool is_short) { return is_short ? -std::expm1(log_ret) : std::expm1(log_ret); }

/// Change between the last level and the one `back` observations earlier.
std::optional<double> change(const std::vector<double>& levels, std::size_t back) {
    if (levels.size() <= back) return std::nullopt;
    return levels.back() / levels[levels.size() - 1 - back] - 1.0;
}

struct Context {
    const MarketDb& db;
    const DashboardSettings& s;
    std::vector<IndexFxPoint> pts;  // aligned EGX30 / USD-EGP
    DailyLogReturns logs;
    std::vector<IndexLevel> index;
    std::vector<FxRate> fx;
    BasisHistory basis;
    std::vector<FuturesSettlement> latest_futures;
    double price = 0.0;  // futures entry price for the position
    std::string priced_as;
    double fx_now = 0.0;
    bool is_short = false;
    std::vector<std::string> notices;
};

// ---------------------------------------------------------------- snapshot

std::string snapshot(Context& c) {
    std::vector<double> idx_levels, usd_levels;
    for (const auto& l : c.index) idx_levels.push_back(l.close);
    for (const auto& p : c.pts) usd_levels.push_back(p.index / p.fx);
    std::vector<double> fx_levels;
    for (const auto& f : c.fx) fx_levels.push_back(f.rate);

    std::string tiles = "<div class=\"tiles\">";
    if (!c.index.empty()) {
        const auto& last = c.index.back();
        const auto d1 = change(idx_levels, 1), d21 = change(idx_levels, 21);
        tiles += tile("EGX30", grouped(last.close, 2), "close " + format_date(last.date) +
                                                           (d21 ? ", 1 month " + signed_pct(*d21) : ""),
                      d1 ? signed_pct(*d1, 2) + " on the day" : "", d1.value_or(0.0));
    }
    if (!c.fx.empty()) {
        const auto& last = c.fx.back();
        const auto d21 = change(fx_levels, 21);
        tiles += tile("USD/EGP", num(last.rate, 4), "rate " + format_date(last.date) + " (a rise is a weaker pound)",
                      d21 ? signed_pct(*d21, 2) + " in a month" : "", d21.value_or(0.0));
    }
    if (!usd_levels.empty()) {
        const auto d21 = change(usd_levels, 21);
        tiles += tile("EGX30 in USD", grouped(usd_levels.back(), 2), "EGX30 / USD-EGP",
                      d21 ? signed_pct(*d21, 2) + " in a month" : "", d21.value_or(0.0));
    }
    if (!c.index.empty())
        if (const auto r = c.db.tbill_for_horizon(c.index.back().date, 91))
            tiles += tile(std::to_string(r->tenor_days) + "-day T-bill", num(r->yield_pct, 3) + "%",
                          "auction " + format_date(r->auction_date) + ", simple annual");
    if (!c.latest_futures.empty()) {
        const auto& f = c.latest_futures.front();
        tiles += tile(f.contract, grouped(f.settle, 2),
                      "settlement " + format_date(f.date) + ", volume " + std::to_string(f.volume));
    }
    tiles += "</div>";
    return section("snapshot", "Market", "", tiles);
}

// ---------------------------------------------------------------- futures

std::string futures(Context& c) {
    std::string body;
    std::optional<Date> latest;
    for (const auto& r : c.basis.rows)
        if (!latest || r.futures.date > *latest) latest = r.futures.date;
    if (!latest) {
        return section("futures", "Futures and basis",
                       "No futures settlements yet. Save the EGX derivatives page after the close; the collector "
                       "imports it.",
                       "");
    }
    body += "<div class=\"scroll\"><table><thead><tr><th>Contract</th><th>Expiry</th><th>Days</th><th>Settle</th>"
            "<th>Volume</th><th>Spot</th><th>Rate</th><th>Fair value</th><th>Exchange formula</th>"
            "<th>Mispricing</th><th>Implied rate</th><th>No-arbitrage band</th><th>Signal</th></tr></thead><tbody>";
    for (const auto& r : c.basis.rows) {
        if (r.futures.date != *latest) continue;
        std::string flags;
        if (r.spot_is_stale) flags += " <span class=\"flag\" title=\"no EGX30 close on this date; previous close used\">stale spot</span>";
        if (r.futures.volume == 0) flags += " <span class=\"flag\">no trades</span>";
        body += "<tr>" + td(h(r.futures.contract) + flags, "l") + td(format_date(r.futures.expiry)) +
                td(std::to_string(r.days_to_expiry)) + td(grouped(r.futures.settle, 2)) +
                td(std::to_string(r.futures.volume)) + td(grouped(r.spot.close, 2)) +
                td(std::to_string(r.rate.tenor_days) + "d " + num(r.rate.yield_pct, 2) + "%") +
                td(grouped(r.report.fair_value, 2)) + td(grouped(r.exchange_formula, 2)) +
                td(signed_pct(r.report.mispricing_pct, 2), tone(r.report.mispricing_pct)) +
                td(num(r.implied_rate_pct, 2) + "%") +
                td(grouped(r.report.bounds.lower, 0) + " &ndash; " + grouped(r.report.bounds.upper, 0)) +
                td(h(std::string(to_string(r.report.signal))), "l") + "</tr>";
    }
    body += "</tbody></table></div>";

    // Mispricing history, one series per contract (at most three, the validated palette slots).
    std::vector<std::string> contracts;
    for (const auto& r : c.basis.rows)
        if (std::find(contracts.begin(), contracts.end(), r.futures.contract) == contracts.end())
            contracts.push_back(r.futures.contract);
    if (contracts.size() > 3) contracts.erase(contracts.begin(), contracts.end() - 3);
    std::vector<Series> series;
    const char* colors[] = {"--series-1", "--series-2", "--series-3"};
    for (std::size_t i = 0; i < contracts.size(); ++i) {
        Series s{contracts[i], {}, Mark::Line, colors[i]};
        for (const auto& r : c.basis.rows)
            if (r.futures.contract == contracts[i])
                s.points.push_back({day_of(r.futures.date), r.report.mispricing_pct * 100.0,
                                    format_date(r.futures.date) + "  " + contracts[i] + "  settle " +
                                        grouped(r.futures.settle, 2) + ", fair " + grouped(r.report.fair_value, 2) +
                                        " (" + signed_pct(r.report.mispricing_pct, 2) + ")"});
        if (s.points.size() < 5) s.mark = Mark::Dots;
        series.push_back(std::move(s));
    }
    ChartOptions o;
    o.title = "Settlement vs fair value";
    o.subtitle = "mispricing in %, by contract; above 0 the futures are rich";
    o.y_decimals = 2;
    o.y_suffix = "%";
    o.zero_line = true;
    body += time_chart(series, o);

    const std::string intro =
        "Fair value F = S&middot;e<sup>(r&minus;q)T</sup> with the T-bill nearest the contract horizon and a " +
        num(c.s.basis.dividend_yield_pct, 1) +
        "% dividend yield. The exchange formula is EGX's fallback settlement S&middot;e<sup>r&middot;T</sup> "
        "(3-month T-bill, no dividends): on thin days the settlement may simply be that price.";
    return section("futures", "Futures and basis", intro, body);
}

// ---------------------------------------------------------------- prices

std::string prices(Context& c) {
    std::string body;
    if (c.pts.size() >= 2) {
        const double i0 = c.pts.front().index, u0 = c.pts.front().index / c.pts.front().fx;
        Series egp{"EGX30 in EGP", {}, Mark::Line, "--series-1"};
        Series usd{"EGX30 in USD", {}, Mark::Line, "--series-2"};
        for (const auto& p : c.pts) {
            egp.points.push_back({day_of(p.date), 100.0 * p.index / i0, ""});
            usd.points.push_back({day_of(p.date), 100.0 * (p.index / p.fx) / u0, ""});
        }
        ChartOptions o;
        o.title = "EGX30 in pounds and in dollars";
        o.subtitle = "indexed to 100 on " + format_date(c.pts.front().date);
        o.y_decimals = 0;
        body += time_chart({egp, usd}, o);
    }
    if (c.fx.size() >= 2) {
        Series fx{"USD/EGP", {}, Mark::Line, "--series-1"};
        for (const auto& f : c.fx) fx.points.push_back({day_of(f.date), f.rate, ""});
        Series jumps{"Jump days (model fit)", {}, Mark::Dots, "--series-2"};
        if (c.logs.index.size() >= 3) {
            const auto fit = risk::fit_egx_fx(c.logs.index, c.logs.fx, kDaysPerYear, {}, {});
            for (auto i : fit.fx.jump_days)
                jumps.points.push_back({day_of(c.logs.dates[i]), c.pts[i + 1].fx,
                                        format_date(c.logs.dates[i]) + "  USD/EGP " + num(c.pts[i + 1].fx, 4) + " (" +
                                            signed_pct(std::expm1(c.logs.fx[i])) + " on the day)"});
        }
        ChartOptions o;
        o.title = "USD/EGP";
        o.subtitle = "pounds per dollar; dots mark the days the jump model counts as devaluation jumps";
        o.y_decimals = 0;
        body += time_chart({fx, jumps}, o);
    }
    if (body.empty()) body = "<p class=\"empty\">No EGX30 or USD/EGP history yet.</p>";
    return section("prices", "History", "", body);
}

// ---------------------------------------------------------------- risk

std::string risk_section(Context& c) {
    if (c.logs.index.size() < 60)
        return section("risk", "Value at Risk", "Needs at least 60 days of EGX30 closes with USD/EGP rates.", "");

    const double notional_egp = std::fabs(c.s.contracts) * c.price * egx30::kMultiplier;
    const double notional_usd = notional_egp / c.fx_now;
    std::vector<double> usd_log;
    for (std::size_t i = 0; i < c.logs.index.size(); ++i) usd_log.push_back(c.logs.index[i] - c.logs.fx[i]);

    const auto fit = risk::fit_egx_fx(c.logs.index, c.logs.fx, kDaysPerYear, {}, {});
    const auto gbm = risk::gbm_equivalent(c.logs.index, c.logs.fx, kDaysPerYear);
    const std::size_t horizons[] = {1, 10};
    const double confidences[] = {0.95, 0.99};

    std::string body = "<div class=\"scroll\"><table><thead><tr><th>View</th><th>Horizon</th><th>Confidence</th>"
                       "<th>Historical</th><th>Normal (GBM)</th><th>Jump-diffusion</th><th>JD CVaR</th>"
                       "<th>VaR amount</th><th>CVaR amount</th></tr></thead><tbody>";
    for (const auto view : {risk::View::Egp, risk::View::Usd}) {
        const bool usd = view == risk::View::Usd;
        const auto& daily = usd ? usd_log : c.logs.index;
        for (auto hz : horizons) {
            const double years = static_cast<double>(hz) / kDaysPerYear;
            std::vector<double> hist;
            for (double r : risk::horizon_log_returns(daily, hz)) hist.push_back(position_return(r, c.is_short));
            const auto draws = risk::simulate_horizon(fit.model, years, c.s.mc_paths, c.s.seed + hz);
            for (double conf : confidences) {
                const auto hv = risk::historical_var(hist, conf);
                const auto g = risk::exact_tail(risk::horizon_mixture(gbm, view, years), conf, c.is_short);
                const auto mc = risk::mc_tail(draws, view, conf, c.is_short);
                const double amount = usd ? notional_usd : notional_egp;
                const std::string ccy = usd ? " USD" : " EGP";
                body += "<tr>" + td(usd ? "USD" : "EGP", "l") + td(std::to_string(hz) + "d") + td(pct(conf, 0)) +
                        td(pct(hv.var)) + td(pct(g.var)) + td(pct(mc.tail.var)) + td(pct(mc.tail.cvar)) +
                        td(money(mc.tail.var * amount) + ccy) + td(money(mc.tail.cvar * amount) + ccy) + "</tr>";
            }
        }
    }
    body += "</tbody></table></div>";

    // Backtest chart: daily position returns, the rolling 99% VaR, and exceptions.
    std::vector<double> daily_simple;
    for (double r : c.logs.index) daily_simple.push_back(position_return(r, c.is_short));
    const std::size_t window = 250;
    std::string bt_text;
    if (daily_simple.size() > window + 20) {
        const auto bt99 = risk::rolling_backtest(daily_simple, window, 0.99);
        const auto bt95 = risk::rolling_backtest(daily_simple, window, 0.95);
        Series rets{"Daily return", {}, Mark::Line, "--series-1"};
        Series var{"99% VaR (loss shown below 0)", {}, Mark::Line, "--series-2"};
        Series exc{"Loss beyond VaR", {}, Mark::Dots, "--critical"};
        for (const auto& d : bt99.days) {
            const double x = day_of(c.logs.dates[d.index]);
            rets.points.push_back({x, d.ret * 100.0, ""});
            var.points.push_back({x, -d.var * 100.0, ""});
            if (d.exception)
                exc.points.push_back({x, d.ret * 100.0,
                                      format_date(c.logs.dates[d.index]) + "  " + signed_pct(d.ret, 2) +
                                          " vs VaR " + pct(d.var)});
        }
        ChartOptions o;
        o.title = "VaR backtest, EGP, 1 day";
        o.subtitle = "each day's VaR uses only the previous " + std::to_string(window) + " days";
        o.y_decimals = 0;
        o.y_suffix = "%";
        o.zero_line = true;
        body += time_chart({rets, var, exc}, o);
        auto verdict = [](const risk::KupiecResult& k) {
            return std::to_string(k.exceptions) + " exceptions in " + std::to_string(k.observations) +
                   " days (expected " + num(k.expected_rate * static_cast<double>(k.observations), 1) +
                   "), Kupiec p = " + num(k.p_value, 3) +
                   (k.p_value < 0.05 ? " &mdash; <strong>rejected</strong>" : " &mdash; consistent");
        };
        bt_text = "<p class=\"note\">95%: " + verdict(bt95.kupiec) + ". 99%: " + verdict(bt99.kupiec) + ".</p>";
    }
    body += bt_text;

    const std::string intro =
        "For " + std::string(c.is_short ? "short " : "long ") + money(std::fabs(c.s.contracts)) + " contracts at " +
        grouped(c.price, 2) + " (" + h(c.priced_as) + "): " + money(notional_egp) + " EGP, " + money(notional_usd) +
        " USD. Futures are delta-one, so EGX30 returns stand in for futures returns. "
        "<em>Normal</em> has the same mean and volatility as history but no jumps; <em>jump-diffusion</em> adds "
        "devaluation jumps fitted to USD/EGP (Monte Carlo, " + money(static_cast<double>(c.s.mc_paths)) + " paths).";
    return section("risk", "Value at Risk", intro, body);
}

// ---------------------------------------------------------------- stress

std::string stress_section(Context& c) {
    if (c.s.contracts == 0.0 || c.price <= 0.0 || c.fx_now <= 0.0) return "";
    risk::StressPosition pos;
    pos.contracts = c.s.contracts;
    pos.futures_price = c.price;
    pos.fx = c.fx_now;

    std::string body = "<h3>Past shocks replayed on today's position</h3><div class=\"scroll\"><table><thead><tr>"
                       "<th>Scenario</th><th>Window</th><th>EGX30</th><th>USD/EGP</th><th>Limit days</th>"
                       "<th>Margin calls</th><th>Cash called</th><th>Worst P&amp;L</th><th>End P&amp;L</th>"
                       "<th>End P&amp;L USD</th></tr></thead><tbody>";
    std::vector<std::string> missing;
    for (const auto& sc : risk::kScenarios) {
        const auto from = parse_date(sc.from), to = parse_date(sc.to);
        const auto pts = index_fx_between(c.db, from, to);
        const std::string name(sc.name);
        if (pts.size() < 2) {
            missing.push_back(name);
            body += "<tr>" + td("<span title=\"" + h(sc.what) + "\">" + h(name) + "</span>", "l") +
                    "<td colspan=\"9\" class=\"l muted\">no data for " + h(sc.from) + " to " + h(sc.to) + "</td></tr>";
            continue;
        }
        const auto logs = daily_log_returns(pts);
        const auto r = risk::stress_replay(logs.index, logs.fx, pos, c.s.margin, egx30::kMultiplier);
        const auto& end = r.days.back();
        std::string window = format_date(pts.front().date) + " &ndash; " + format_date(pts.back().date);
        if (std::chrono::sys_days{pts.front().date} > std::chrono::sys_days{from} + std::chrono::days{7})
            window += " <span class=\"flag\" title=\"the database starts after the scenario start\">partial</span>";
        body += "<tr>" + td("<span title=\"" + h(sc.what) + "\">" + h(name) + "</span>", "l") + td(window, "l") +
                td(signed_pct(end.index_level - 1.0), tone(end.index_level - 1.0)) +
                td(signed_pct(end.fx / pos.fx - 1.0)) + td(std::to_string(r.locked_days)) +
                td(std::to_string(r.margin.calls)) + td(money(r.margin.total_called)) +
                td(money(r.worst_pnl_egp), tone(r.worst_pnl_egp)) + td(money(end.pnl_egp), tone(end.pnl_egp)) +
                td(money(end.pnl_usd), tone(end.pnl_usd)) + "</tr>";
    }
    body += "</tbody></table></div>";
    if (!missing.empty())
        c.notices.push_back("Stress windows without data: " + [&] {
            std::string s;
            for (const auto& m : missing) s += (s.empty() ? "" : ", ") + m;
            return s;
        }() + ". Download EGX30 and USD/EGP history from before 2023 from Investing.com; the importer merges it.");

    // Instant shocks.
    const double idx_moves[] = {-0.30, -0.20, -0.10, 0.10};
    const double fx_moves[] = {0.0, 0.20, 0.50};
    body += "<h3>Instant shocks</h3><div class=\"scroll\"><table><thead><tr><th>EGX30</th><th>Sessions to settle</th>"
            "<th>Margin calls</th><th>Cash called</th><th>P&amp;L EGP</th>";
    for (double f : fx_moves) body += "<th>P&amp;L USD, USD/EGP " + signed_pct(f, 0) + "</th>";
    body += "</tr></thead><tbody>";
    for (double im : idx_moves) {
        const auto base = risk::instant_shock(im, 0.0, pos, c.s.margin, egx30::kMultiplier);
        const auto& end = base.days.back();
        body += "<tr>" + td(signed_pct(im, 0), tone(im)) + td(std::to_string(base.locked_days + 1)) +
                td(std::to_string(base.margin.calls)) + td(money(base.margin.total_called)) +
                td(money(end.pnl_egp), tone(end.pnl_egp));
        for (double f : fx_moves) {
            const auto r = risk::instant_shock(im, f, pos, c.s.margin, egx30::kMultiplier);
            body += td(money(r.days.back().pnl_usd), tone(r.days.back().pnl_usd));
        }
        body += "</tr>";
    }
    body += "</tbody></table></div>";

    // Margin-call Monte Carlo.
    if (c.logs.index.size() >= 60) {
        const auto fit = risk::fit_egx_fx(c.logs.index, c.logs.fx, kDaysPerYear, {}, {});
        const auto m = risk::margin_monte_carlo(fit.model, c.s.margin_days, c.s.margin_paths, c.s.seed,
                                                c.s.contracts, c.price, c.s.margin, egx30::kMultiplier);
        const double deposit = std::fabs(c.s.contracts) * c.s.margin.initial;
        body += "<h3>Margin-call risk over " + std::to_string(c.s.margin_days) + " sessions</h3><div class=\"tiles\">" +
                tile("Deposit", money(deposit) + " EGP", "initial margin for the position") +
                tile("Chance of a margin call", pct(m.p_call, 1), num(m.mean_calls, 2) + " calls on average") +
                tile("Cash to keep ready (95%)", money(m.called_q95) + " EGP", "mean " + money(m.called_mean) + " EGP") +
                tile("Cash to keep ready (99%)", money(m.called_q99) + " EGP", "liquidity at risk") +
                tile("Price limit binds", pct(m.p_locked, 1), "chance of at least one limit day") + "</div>";
    }

    const std::string intro =
        "Futures follow the EGX30 within EGX's &plusmn;" + pct(c.s.margin.price_limit, 0) +
        " daily price limit, so a bigger move settles over several sessions. Margin: initial " +
        money(c.s.margin.initial) + " EGP per contract (EGX bulletin), maintenance " + money(c.s.margin.maintenance) +
        " EGP (assumed). P&amp;L in USD includes the margin deposit losing value in a devaluation.";
    return section("stress", "Stress tests and margin", intro, body);
}

// ---------------------------------------------------------------- data

std::string data_section(Context& c) {
    const auto sum = c.db.summary();
    auto range = [](const std::string& a, const std::string& b) { return a.empty() ? "&ndash;" : a + " &ndash; " + b; };
    std::string idx_r = c.index.empty() ? "" : format_date(c.index.front().date);
    std::string idx_e = c.index.empty() ? "" : format_date(c.index.back().date);
    std::string fx_r = c.fx.empty() ? "" : format_date(c.fx.front().date);
    std::string fx_e = c.fx.empty() ? "" : format_date(c.fx.back().date);
    const auto fut = c.db.futures_between(kMinDate, kMaxDate);
    std::string fu_r = fut.empty() ? "" : format_date(fut.front().date);
    std::string fu_e = fut.empty() ? "" : format_date(fut.back().date);
    std::string body = "<div class=\"scroll\"><table class=\"compact\"><thead><tr><th>Table</th><th>Rows</th>"
                       "<th>Range</th></tr></thead><tbody>" +
                       ("<tr>" + td("EGX30 closes", "l") + td(money(static_cast<double>(sum.index_levels))) +
                        td(range(idx_r, idx_e), "l") + "</tr>") +
                       ("<tr>" + td("USD/EGP rates", "l") + td(money(static_cast<double>(sum.fx))) +
                        td(range(fx_r, fx_e), "l") + "</tr>") +
                       ("<tr>" + td("Futures settlements", "l") + td(money(static_cast<double>(sum.futures))) +
                        td(range(fu_r, fu_e), "l") + "</tr>") +
                       ("<tr>" + td("T-bill auctions", "l") + td(money(static_cast<double>(sum.tbills))) + td("", "l") +
                        "</tr>") +
                       ("<tr>" + td("CPI months", "l") + td(money(static_cast<double>(sum.cpi))) + td("", "l") + "</tr>") +
                       "</tbody></table></div>";
    return section("data", "Data", "", body);
}

const char* kCss = R"css(
:root{color-scheme:light;--page:#f9f9f7;--surface:#fcfcfb;--ink:#0b0b0b;--ink-2:#52514e;--ink-muted:#898781;
--grid:#e1e0d9;--axis:#c3c2b7;--border:#e1e0d9;--series-1:#2a78d6;--series-2:#eb6834;--series-3:#1baf7a;
--critical:#d03b3b;--up:#006300;--down:#d03b3b;--flag-bg:#fdf3d8;--flag-ink:#6b4a00}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){color-scheme:dark;--page:#0d0d0d;--surface:#1a1a19;
--ink:#f0efec;--ink-2:#c3c2b7;--ink-muted:#898781;--grid:#2c2c2a;--axis:#383835;--border:#2c2c2a;--series-1:#3987e5;
--series-2:#d95926;--series-3:#199e70;--critical:#d03b3b;--up:#0ca30c;--down:#e66767;--flag-bg:#3a2f12;--flag-ink:#fab219}}
:root[data-theme="dark"]{color-scheme:dark;--page:#0d0d0d;--surface:#1a1a19;--ink:#f0efec;--ink-2:#c3c2b7;
--ink-muted:#898781;--grid:#2c2c2a;--axis:#383835;--border:#2c2c2a;--series-1:#3987e5;--series-2:#d95926;
--series-3:#199e70;--critical:#d03b3b;--up:#0ca30c;--down:#e66767;--flag-bg:#3a2f12;--flag-ink:#fab219}
*{box-sizing:border-box}
body{margin:0;background:var(--page);color:var(--ink);font:14px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:1100px;margin:0 auto;padding:24px 16px 48px}
header h1{font-size:22px;margin:0 0 4px}header p{margin:0;color:var(--ink-2)}
nav{display:flex;flex-wrap:wrap;gap:6px 16px;margin:12px 0 8px}nav a{color:var(--ink-2);text-decoration:none}
nav a:hover{color:var(--ink);text-decoration:underline}
section{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:16px 18px;margin-top:16px}
h2{font-size:17px;margin:0 0 8px}h3{font-size:14px;margin:18px 0 8px;color:var(--ink-2)}
.intro,.note{color:var(--ink-2);margin:0 0 12px;max-width:80ch}.note{font-size:13px}
.notices{margin:16px 0 0;padding:10px 14px;border-radius:10px;background:var(--flag-bg);color:var(--flag-ink)}
.notices ul{margin:0;padding-left:18px}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:10px}
.tile{border:1px solid var(--border);border-radius:8px;padding:10px 12px}
.tile .label{color:var(--ink-2);font-size:12px}.tile .value{font-size:22px;font-weight:600;font-variant-numeric:tabular-nums}
.tile .note{font-size:12px;color:var(--ink-muted);margin:2px 0 0}
.delta{font-size:12px;font-variant-numeric:tabular-nums}.up{color:var(--up)}.down{color:var(--down)}
.scroll{overflow-x:auto}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums;font-size:13px}
th,td{padding:6px 8px;border-bottom:1px solid var(--border);text-align:right;white-space:nowrap}
th{color:var(--ink-2);font-weight:500}th:first-child,td.l{text-align:left}
td.muted{color:var(--ink-muted)}table.compact{width:auto}
.flag{display:inline-block;font-size:11px;padding:0 6px;border-radius:9px;background:var(--flag-bg);color:var(--flag-ink)}
figure.chart{margin:16px 0 0;position:relative}
figcaption strong{display:block;font-size:14px}figcaption span{color:var(--ink-2);font-size:12px}
.legend{display:flex;flex-wrap:wrap;gap:4px 14px;margin:6px 0 2px;font-size:12px;color:var(--ink-2)}
.legend i{display:inline-block;margin-right:6px;vertical-align:middle}
.legend i.line{width:16px;height:2px;border-radius:1px}.legend i.dot{width:8px;height:8px;border-radius:50%}
svg{width:100%;height:auto;display:block;overflow:visible}
svg .grid{stroke:var(--grid);stroke-width:1}svg .axis{stroke:var(--axis);stroke-width:1}
svg .tick{fill:var(--ink-muted);font-size:11px}
svg .mark-line{fill:none;stroke-width:2;stroke-linejoin:round;stroke-linecap:round}
svg .mark-dot{stroke:var(--surface);stroke-width:2}
svg .crosshair{stroke:var(--ink-muted);stroke-width:1;stroke-dasharray:3 3;visibility:hidden}
.tooltip{position:absolute;pointer-events:none;background:var(--surface);color:var(--ink);border:1px solid var(--border);
border-radius:6px;padding:6px 8px;font-size:12px;box-shadow:0 2px 8px rgba(0,0,0,.12);white-space:nowrap;z-index:2}
.tooltip b{font-weight:600}.tooltip i{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
.empty{color:var(--ink-muted);padding:24px 0}
footer{color:var(--ink-muted);font-size:12px;margin-top:20px}
)css";

const char* kScript = R"js(
document.querySelectorAll('figure.chart').forEach(function(fig){
  var svg=fig.querySelector('svg'),tip=fig.querySelector('.tooltip'),js=fig.querySelector('script');
  if(!svg||!tip||!js)return;var D=JSON.parse(js.textContent),cross=svg.querySelector('.crosshair');
  function nearest(p,x){var lo=0,hi=p.length-1;while(hi-lo>1){var m=(lo+hi)>>1;if(p[m][0]<x)lo=m;else hi=m;}
    return Math.abs(p[lo][0]-x)<=Math.abs(p[hi][0]-x)?p[lo]:p[hi];}
  function move(ev){var r=svg.getBoundingClientRect(),k=D.w/r.width,x=(ev.clientX-r.left)*k,y=(ev.clientY-r.top)*k;
    var rows=[],cx=null,dotHit=null;
    D.s.forEach(function(s){if(!s.p.length)return;
      if(s.d){s.p.forEach(function(q){var dd=Math.hypot(q[0]-x,q[1]-y);if(dd<14&&(!dotHit||dd<dotHit.d))dotHit={d:dd,s:s,q:q};});}
      else{var q=nearest(s.p,x);cx=q[0];rows.push([s,q]);}});
    if(dotHit){rows=[[dotHit.s,dotHit.q]];cx=dotHit.q[0];}
    if(!rows.length){leave();return;}
    cross.setAttribute('x1',cx);cross.setAttribute('x2',cx);cross.style.visibility='visible';
    tip.innerHTML=rows.map(function(rw){var t=String(rw[1][2]).replace(/[&<>]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;'}[c];});
      return '<div><i style="background:var('+rw[0].c+')"></i>'+(D.s.length>1?'<b>'+rw[0].n.replace(/[&<>]/g,'')+'</b> ':'')+t+'</div>';}).join('');
    tip.hidden=false;var fr=fig.getBoundingClientRect(),left=ev.clientX-fr.left+14;
    if(left+tip.offsetWidth>fr.width)left=ev.clientX-fr.left-tip.offsetWidth-14;
    tip.style.left=Math.max(0,left)+'px';tip.style.top=(ev.clientY-fr.top+12)+'px';}
  function leave(){tip.hidden=true;cross.style.visibility='hidden';}
  svg.addEventListener('pointermove',move);svg.addEventListener('pointerleave',leave);
});
)js";

}  // namespace

std::string build_dashboard(const MarketDb& db, const DashboardSettings& settings) {
    Context c{db, settings, {}, {}, {}, {}, {}, {}, 0.0, {}, 0.0, settings.contracts < 0, {}};
    c.index = db.index_between(kMinDate, kMaxDate);
    c.fx = db.fx_between(kMinDate, kMaxDate);
    c.pts = index_fx_between(db, kMinDate, kMaxDate);
    c.logs = daily_log_returns(c.pts);
    c.basis = basis_history(db, kMinDate, kMaxDate, settings.basis);
    c.fx_now = c.fx.empty() ? 0.0 : c.fx.back().rate;

    const auto fut = db.futures_between(kMinDate, kMaxDate);
    if (!fut.empty()) {
        const Date last = fut.back().date;
        for (const auto& f : fut)
            if (f.date == last) c.latest_futures.push_back(f);
        c.price = c.latest_futures.front().settle;
        c.priced_as = c.latest_futures.front().contract + " settlement " + format_date(last);
        if (!c.index.empty() && c.index.back().date < last)
            c.notices.push_back("The latest EGX30 close (" + format_date(c.index.back().date) +
                                ") is older than the latest futures bulletin (" + format_date(last) +
                                "): the basis uses a stale spot. Download the EGX30 history from Investing.com.");
    } else if (!c.index.empty()) {
        c.price = c.index.back().close;
        c.priced_as = "EGX30 close " + format_date(c.index.back().date) + ", no futures settlements yet";
    }
    if (db.summary().cpi == 0)
        c.notices.push_back("No CPI data, so the inflation-adjusted (real EGP) view is not shown. Add monthly CPI to "
                            "data/clean/cpi.csv (month,index) and reload.");

    std::string sections;
    sections += snapshot(c);
    sections += futures(c);
    sections += prices(c);
    sections += risk_section(c);
    sections += stress_section(c);
    sections += data_section(c);

    std::string asof = c.index.empty() ? "no data" : "data to " + format_date(c.index.back().date);
    if (!c.latest_futures.empty()) asof += ", futures to " + format_date(c.latest_futures.front().date);

    std::string out = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                      "<title>EGX30 futures dashboard</title>\n<style>" +
                      std::string(kCss) + "</style>\n</head>\n<body>\n<main>\n<header><h1>EGX30 futures dashboard</h1><p>" +
                      h(asof) + (settings.generated_at.empty() ? "" : " &middot; generated " + h(settings.generated_at)) +
                      "</p>\n<nav><a href=\"#snapshot\">Market</a><a href=\"#futures\">Futures</a>"
                      "<a href=\"#prices\">History</a><a href=\"#risk\">VaR</a><a href=\"#stress\">Stress</a>"
                      "<a href=\"#data\">Data</a></nav></header>\n";
    if (!c.notices.empty()) {
        out += "<div class=\"notices\" role=\"note\"><ul>";
        for (const auto& n : c.notices) out += "<li>" + h(n) + "</li>";
        out += "</ul></div>\n";
    }
    out += sections;
    out += "<footer>egx-quant. Educational project, not investment advice. Signals ignore short-selling limits, "
           "liquidity and execution costs beyond the stated frictions.</footer>\n</main>\n<script>" +
           std::string(kScript) + "</script>\n</body>\n</html>\n";
    return out;
}

}  // namespace egxq::report
