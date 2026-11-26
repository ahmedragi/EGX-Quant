// egx_var: historical VaR and CVaR for an EGX30 position in nominal EGP, USD
// and real (CPI-deflated) EGP, with a rolling backtest of the 1-day VaR.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "egxquant/data/csv.hpp"
#include "egxquant/data/market_db.hpp"
#include "egxquant/data/risk_series.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "egxquant/risk/stats.hpp"
#include "egxquant/risk/var.hpp"

namespace {

constexpr const char* kUsage = R"(Usage: egx_var [options]

Historical Value at Risk and CVaR (expected shortfall) of an EGX30 position, from
the EGX30 closes in the database. Futures are delta-one, so index returns stand in
for futures returns. Each view is computed from that view's own return series:
  EGP    nominal pounds
  USD    EGX30 / USD-EGP: an unhedged foreign investor, devaluations included
  real   EGX30 deflated by CPI: purchasing power (needs CPI in the database)

Options:
  --db <file>              Database (default data/egx.db)
  --from <YYYY-MM-DD>      First close used (default: all)
  --to <YYYY-MM-DD>        Last close used (default: all)
  --view <list>            egp,usd,real (default all three)
  --confidence <list>      Confidence levels in % (default 95,99)
  --horizon <list>         Horizons in trading days (default 1,10)
  --window <n>             Use only the last n daily returns (default: all)
  --contracts <n>          Position size in contracts (1 EGP per point)
  --notional <egp>         Or: position value in EGP
  --short                  Short position (losses come from rises)
  --backtest-window <n>    Rolling window for the 1-day backtest (default 250)
  --out <file.csv>         Write the rolling backtest as CSV
  --help                   Show this message

Historical VaR is the empirical loss quantile; CVaR is the mean loss at or beyond it.
h-day figures use overlapping h-day returns (they are not independent samples);
"sqrt-h" scales the 1-day VaR by sqrt(h) for comparison. "normal" assumes
normally distributed returns with the sample mean and volatility.
)";

constexpr double kTradingDaysPerYear = 250.0;

std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

/// 1234567.8 -> "1,234,568"
std::string money(double v) {
    const bool neg = v < 0;
    auto digits = std::to_string(static_cast<long long>(std::llround(std::fabs(v))));
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return neg ? "-" + out : out;
}

std::string pct(double v, int decimals = 2) { return num(v * 100.0, decimals) + "%"; }

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

struct Settings {
    std::vector<egxq::RiskView> views;
    std::vector<double> confidences;  // fractions
    std::vector<std::size_t> horizons;
    std::optional<std::size_t> window;
    std::size_t backtest_window = 250;
    bool is_short = false;
    std::optional<double> notional_egp;
};

struct ViewResult {
    egxq::RiskView view;
    std::vector<egxq::IndexLevel> levels;  // levels behind the returns used
    std::vector<double> daily_log;
    std::vector<double> daily_simple;  // position returns (sign-flipped if short)
};

ViewResult prepare(const egxq::ViewSeries& s, const Settings& cfg) {
    ViewResult r{s.view, s.levels, {}, {}};
    std::vector<double> px;
    px.reserve(s.levels.size());
    for (const auto& l : s.levels) px.push_back(l.close);
    r.daily_log = egxq::risk::log_returns(px);
    if (cfg.window && r.daily_log.size() > *cfg.window) {
        const auto drop = r.daily_log.size() - *cfg.window;
        r.daily_log.erase(r.daily_log.begin(), r.daily_log.begin() + static_cast<std::ptrdiff_t>(drop));
        r.levels.erase(r.levels.begin(), r.levels.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    if (cfg.is_short)
        for (double& x : r.daily_log) x = -x;
    // A short's simple return is -(exp(r) - 1), not exp(-r) - 1: flip after converting.
    r.daily_simple.reserve(r.daily_log.size());
    for (double x : r.daily_log) r.daily_simple.push_back(cfg.is_short ? -std::expm1(-x) : std::expm1(x));
    return r;
}

/// Simple position returns over h days.
std::vector<double> horizon_simple(const ViewResult& r, std::size_t h, bool is_short) {
    if (h == 1) return r.daily_simple;
    const auto logs = egxq::risk::horizon_log_returns(r.daily_log, h);
    std::vector<double> out;
    out.reserve(logs.size());
    for (double x : logs) out.push_back(is_short ? -std::expm1(-x) : std::expm1(x));
    return out;
}

void report(const ViewResult& r, const Settings& cfg, std::optional<double> notional_in_view, const char* ccy) {
    std::cout << "\n[" << egxq::to_string(r.view) << "]  ";
    if (r.daily_log.size() < 2) {
        std::cout << "not enough data\n";
        return;
    }
    const auto m = egxq::stats::moments(r.daily_simple);
    std::cout << r.daily_log.size() << " daily returns, " << egxq::format_date(r.levels[1].date) << " to "
              << egxq::format_date(r.levels.back().date) << "\n";
    std::printf("  volatility %s p.a.   mean %s/day   skew %.2f   excess kurtosis %.1f\n",
                pct(m.stdev * std::sqrt(kTradingDaysPerYear), 1).c_str(), pct(m.mean, 3).c_str(), m.skewness,
                m.excess_kurtosis);

    std::printf("  %7s %5s %10s %10s %10s %10s", "horizon", "conf", "hist VaR", "hist CVaR", "normal", "sqrt-h");
    if (notional_in_view) std::printf(" %14s %14s", (std::string("VaR ") + ccy).c_str(), (std::string("CVaR ") + ccy).c_str());
    std::printf("  %s\n", "samples");

    for (std::size_t h : cfg.horizons) {
        const auto rets = horizon_simple(r, h, cfg.is_short);
        if (rets.empty()) {
            std::printf("  %6zud  not enough data\n", h);
            continue;
        }
        for (double c : cfg.confidences) {
            const auto hist = egxq::risk::historical_var(rets, c);
            const auto hd = static_cast<double>(h);
            const auto normal = egxq::risk::normal_var(m.mean * hd, m.stdev * std::sqrt(hd), c);
            const std::string scaled =
                h == 1 ? "-" : pct(egxq::risk::historical_var(r.daily_simple, c).var * std::sqrt(hd));
            std::printf("  %6zud %4.1f%% %10s %10s %10s %10s", h, c * 100.0, pct(hist.var).c_str(),
                        pct(hist.cvar).c_str(), pct(normal.var).c_str(), scaled.c_str());
            if (notional_in_view)
                std::printf(" %14s %14s", money(hist.var * *notional_in_view).c_str(),
                            money(hist.cvar * *notional_in_view).c_str());
            std::printf("  %zu\n", rets.size());
        }
    }

    // Worst days for this position.
    std::vector<std::size_t> idx(r.daily_simple.size());
    std::iota(idx.begin(), idx.end(), std::size_t{0});
    const auto worst = std::min<std::size_t>(5, idx.size());
    std::partial_sort(idx.begin(), idx.begin() + static_cast<std::ptrdiff_t>(worst), idx.end(),
                      [&](std::size_t a, std::size_t b) { return r.daily_simple[a] < r.daily_simple[b]; });
    std::cout << "  worst days:";
    for (std::size_t i = 0; i < worst; ++i)
        std::cout << "  " << egxq::format_date(r.levels[idx[i] + 1].date) << " " << pct(r.daily_simple[idx[i]]);
    std::cout << "\n";

    // Rolling backtest of the 1-day historical VaR.
    std::cout << "  backtest, 1-day VaR from the previous " << cfg.backtest_window << " days:\n";
    for (double c : cfg.confidences) {
        const auto bt = egxq::risk::rolling_backtest(r.daily_simple, cfg.backtest_window, c);
        if (bt.days.empty()) {
            std::cout << "    not enough data (need more than " << cfg.backtest_window << " returns)\n";
            break;
        }
        const auto& k = bt.kupiec;
        std::printf("    %4.1f%%: %zu exceptions in %zu days (expected %.1f)  Kupiec LR %.2f  p = %.3f  %s\n",
                    c * 100.0, k.exceptions, k.observations, k.expected_rate * static_cast<double>(k.observations),
                    k.lr, k.p_value, k.p_value < 0.05 ? "REJECT: VaR mis-calibrated" : "ok");
    }
}

void write_backtests(const std::string& path, const std::vector<ViewResult>& results, const Settings& cfg) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    egxq::csv::write_row(out, {"date", "view", "confidence", "return_pct", "var_pct", "cvar_pct", "exception"});
    for (const auto& r : results)
        for (double c : cfg.confidences) {
            const auto bt = egxq::risk::rolling_backtest(r.daily_simple, cfg.backtest_window, c);
            for (const auto& d : bt.days)
                egxq::csv::write_row(out, {egxq::format_date(r.levels[d.index + 1].date),
                                           std::string(egxq::to_string(r.view)), num(c * 100.0, 1),
                                           num(d.ret * 100.0, 4), num(d.var * 100.0, 4), num(d.cvar * 100.0, 4),
                                           d.exception ? "1" : "0"});
        }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> a;
        bool is_short = false;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << kUsage;
                return EXIT_SUCCESS;
            }
            if (key == "--short") {
                is_short = true;
                continue;
            }
            if (key.rfind("--", 0) != 0 || i + 1 >= argc) throw std::invalid_argument("bad argument " + key);
            a[key.substr(2)] = argv[++i];
        }
        auto get = [&](const char* k, const std::string& def) { return a.count(k) ? a.at(k) : def; };

        Settings cfg;
        cfg.is_short = is_short;
        for (const auto& v : split(get("view", "egp,usd,real"))) {
            const auto view = egxq::parse_risk_view(v);
            if (!view) throw std::invalid_argument("unknown view '" + v + "'");
            cfg.views.push_back(*view);
        }
        for (const auto& c : split(get("confidence", "95,99"))) {
            const double v = std::stod(c);
            if (!(v > 50.0 && v < 100.0)) throw std::invalid_argument("confidence must be between 50 and 100");
            cfg.confidences.push_back(v / 100.0);
        }
        for (const auto& h : split(get("horizon", "1,10"))) {
            const int v = std::stoi(h);
            if (v < 1) throw std::invalid_argument("horizon must be at least 1 day");
            cfg.horizons.push_back(static_cast<std::size_t>(v));
        }
        if (a.count("window")) cfg.window = static_cast<std::size_t>(std::stoul(a.at("window")));
        if (a.count("backtest-window")) cfg.backtest_window = static_cast<std::size_t>(std::stoul(a.at("backtest-window")));
        if (cfg.backtest_window == 0) throw std::invalid_argument("backtest window must be positive");

        const std::string db_path = get("db", "data/egx.db");
        if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
        const egxq::MarketDb db{std::filesystem::path(db_path)};
        const auto from = egxq::parse_date(get("from", "1900-03-01"));
        const auto to = egxq::parse_date(get("to", "2999-12-31"));

        const auto last = db.index_close_on_or_before(to);
        if (!last) throw std::runtime_error("no EGX30 closes in " + db_path);

        std::cout << "EGX30 historical VaR, " << (is_short ? "short" : "long") << " position\n";
        if (a.count("contracts") && a.count("notional"))
            throw std::invalid_argument("give --contracts or --notional, not both");
        if (a.count("contracts")) {
            const double n = std::stod(a.at("contracts"));
            cfg.notional_egp = n * last->close * egxq::egx30::kMultiplier;
            std::cout << "Position: " << a.at("contracts") << " contracts x " << num(last->close, 2) << " (EGX30 close "
                      << egxq::format_date(last->date) << ") x 1 EGP/pt = " << money(*cfg.notional_egp) << " EGP\n";
        } else if (a.count("notional")) {
            cfg.notional_egp = std::stod(a.at("notional"));
            std::cout << "Position: " << money(*cfg.notional_egp) << " EGP\n";
        }

        std::vector<ViewResult> results;
        for (auto view : cfg.views) {
            const auto series = egxq::index_view(db, from, to, view);
            if (series.levels.size() < 3) {
                std::cout << "\n[" << egxq::to_string(view) << "]  ";
                if (view == egxq::RiskView::RealEgp)
                    std::cout << "no CPI data: add monthly CPI to data/clean/cpi.csv (month,index) and run egx_db load\n";
                else if (view == egxq::RiskView::Usd)
                    std::cout << "no USD/EGP rates matching the EGX30 dates\n";
                else
                    std::cout << "not enough EGX30 closes\n";
                continue;
            }
            auto r = prepare(series, cfg);
            std::optional<double> notional;
            const char* ccy = "EGP";
            if (cfg.notional_egp) {
                notional = cfg.notional_egp;
                if (view == egxq::RiskView::Usd) {
                    const auto fx = db.fx_on_or_before(to);
                    notional = fx ? std::optional<double>(*cfg.notional_egp / fx->rate) : std::nullopt;
                    ccy = "USD";
                }
            }
            report(r, cfg, notional, ccy);
            if (series.dropped)
                std::cout << "  (" << series.dropped << " EGX30 closes left out for lack of "
                          << (view == egxq::RiskView::Usd ? "a recent USD/EGP rate" : "CPI coverage") << ")\n";
            results.push_back(std::move(r));
        }

        if (a.count("out")) {
            write_backtests(a.at("out"), results, cfg);
            std::cout << "\nWrote " << a.at("out") << "\n";
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "egx_var: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
