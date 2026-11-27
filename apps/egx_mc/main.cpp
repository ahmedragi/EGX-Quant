// egx_mc: Monte Carlo VaR for an EGX30 position under a jump-diffusion model
// of the EGX30 and USD/EGP, fitted to history, in EGP and USD terms.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "egxquant/data/market_db.hpp"
#include "egxquant/data/risk_series.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "egxquant/risk/calibration.hpp"
#include "egxquant/risk/jump_diffusion.hpp"
#include "egxquant/risk/var.hpp"

namespace {

using namespace egxq::risk;

constexpr const char* kUsage = R"(Usage: egx_mc [options]

Monte Carlo VaR and CVaR of an EGX30 position, with the EGX30 and USD/EGP each
modelled as a Merton jump-diffusion fitted to the database history. Devaluations
show up as USD/EGP jumps; the EGX30's reaction to them in pounds is the
"pass-through". Results are compared with history and with a no-jump model (GBM).

Options:
  --db <file>              Database (default data/egx.db)
  --from <YYYY-MM-DD>      First day of the fitting sample (default: all)
  --to <YYYY-MM-DD>        Last day of the fitting sample (default: all)
  --horizon <list>         Horizons in trading days (default 1,10,21)
  --confidence <list>      Confidence levels in % (default 95,99)
  --paths <n>              Simulated paths per horizon (default 200000)
  --seed <n>               Random seed (default 42); same seed, same results
  --contracts <n>          Position size in contracts (1 EGP per point)
  --notional <egp>         Or: position value in EGP
  --short                  Short position
Jump detection (a day is a jump when |return - median| > max(k * robust vol, floor)):
  --jump-sigmas <k>        k for both series (default 4)
  --index-min-jump <pct>   Floor for the EGX30 (default 2)
  --fx-min-jump <pct>      Floor for USD/EGP (default 2)
Scenario overrides (replace the fitted values):
  --fx-jump-rate <n>       Devaluations per year
  --fx-jump-size <pct>     Mean devaluation, % rise in USD/EGP (e.g. 30)
  --fx-jump-stdev <pct>    Spread of the devaluation size, in log %
  --passthrough <b>        EGX30 log move per unit USD/EGP log jump
  --help                   Show this message

Columns: "hist" is the historical VaR from overlapping returns, "GBM" the exact VaR
of a no-jump model with the same mean and volatility, "JD" the jump-diffusion VaR by
Monte Carlo (with its standard error) and "exact" by the model's closed-form
distribution, which checks the simulation.
)";

constexpr double kDaysPerYear = 250.0;

std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string pct(double v, int decimals = 2) { return num(v * 100.0, decimals) + "%"; }

std::string signed_pct(double v, int decimals = 1) { return (v >= 0 ? "+" : "") + pct(v, decimals); }

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

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

void print_factor(const char* name, const FactorFit& f) {
    const auto& m = f.model;
    std::printf("  %-12s %8s %7s %9.2f %10s %9s %6zu   %s\n", name, signed_pct(m.drift).c_str(),
                pct(m.sigma, 1).c_str(), m.jump_rate, signed_pct(m.jump_mean).c_str(), pct(m.jump_stdev, 1).c_str(),
                f.jump_days.size(), pct(f.threshold, 1).c_str());
}

/// Historical h-day position returns from daily log returns.
std::vector<double> hist_returns(const std::vector<double>& daily_log, std::size_t h, bool is_short) {
    std::vector<double> out;
    for (double r : horizon_log_returns(daily_log, h)) out.push_back(is_short ? -std::expm1(r) : std::expm1(r));
    return out;
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
        auto number = [&](const char* k, double def) { return a.count(k) ? std::stod(a.at(k)) : def; };

        std::vector<double> confidences;
        for (const auto& c : split(get("confidence", "95,99"))) {
            const double v = std::stod(c);
            if (!(v > 50.0 && v < 100.0)) throw std::invalid_argument("confidence must be between 50 and 100");
            confidences.push_back(v / 100.0);
        }
        std::vector<std::size_t> horizons;
        for (const auto& h : split(get("horizon", "1,10,21"))) {
            const int v = std::stoi(h);
            if (v < 1) throw std::invalid_argument("horizon must be at least 1 day");
            horizons.push_back(static_cast<std::size_t>(v));
        }
        const auto paths = static_cast<std::size_t>(std::stoul(get("paths", "200000")));
        if (paths < 1000) throw std::invalid_argument("use at least 1000 paths");
        const auto seed = static_cast<std::uint64_t>(std::stoull(get("seed", "42")));

        const std::string db_path = get("db", "data/egx.db");
        if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
        const egxq::MarketDb db{std::filesystem::path(db_path)};
        const auto from = egxq::parse_date(get("from", "1900-03-01"));
        const auto to = egxq::parse_date(get("to", "2999-12-31"));

        const auto pts = egxq::index_fx_between(db, from, to);
        if (pts.size() < 60) throw std::runtime_error("need at least 60 EGX30 closes with USD/EGP rates");
        std::vector<double> idx_log, fx_log, usd_log;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            idx_log.push_back(std::log(pts[i].index / pts[i - 1].index));
            fx_log.push_back(std::log(pts[i].fx / pts[i - 1].fx));
            usd_log.push_back(idx_log.back() - fx_log.back());
        }

        const double k = number("jump-sigmas", 4.0);
        const JumpRule index_rule{k, number("index-min-jump", 2.0) / 100.0};
        const JumpRule fx_rule{k, number("fx-min-jump", 2.0) / 100.0};
        const auto fit = fit_egx_fx(idx_log, fx_log, kDaysPerYear, index_rule, fx_rule);
        auto model = fit.model;
        const auto gbm = gbm_equivalent(idx_log, fx_log, kDaysPerYear);

        // Scenario overrides.
        model.fx.jump_rate = number("fx-jump-rate", model.fx.jump_rate);
        if (a.count("fx-jump-size")) model.fx.jump_mean = std::log1p(std::stod(a.at("fx-jump-size")) / 100.0);
        model.fx.jump_stdev = number("fx-jump-stdev", model.fx.jump_stdev * 100.0) / 100.0;
        model.passthrough = number("passthrough", model.passthrough);
        const bool overridden =
            a.count("fx-jump-rate") || a.count("fx-jump-size") || a.count("fx-jump-stdev") || a.count("passthrough");

        // Position.
        const auto& last = pts.back();
        std::optional<double> notional;
        if (a.count("contracts") && a.count("notional"))
            throw std::invalid_argument("give --contracts or --notional, not both");
        if (a.count("contracts")) notional = std::stod(a.at("contracts")) * last.index * egxq::egx30::kMultiplier;
        if (a.count("notional")) notional = std::stod(a.at("notional"));

        std::cout << "EGX30 Monte Carlo VaR, " << (is_short ? "short" : "long") << " position, " << money(static_cast<double>(paths))
                  << " paths per horizon, seed " << seed << "\n";
        if (notional)
            std::cout << "Position " << money(*notional) << " EGP = " << money(*notional / last.fx) << " USD at "
                      << num(last.fx, 4) << " (" << egxq::format_date(last.date) << ")\n";

        std::cout << "\nFitted to " << idx_log.size() << " daily returns, " << egxq::format_date(pts[1].date) << " to "
                  << egxq::format_date(last.date) << " (log terms, per year):\n";
        std::printf("  %-12s %8s %7s %9s %10s %9s %6s   %s\n", "", "drift", "vol", "jumps/yr", "jump mean", "jump sd",
                    "jumps", "threshold");
        print_factor("EGX30 (EGP)", fit.index);
        print_factor("USD/EGP", fit.fx);
        std::printf("  diffusion correlation %.2f, pass-through %.2f (EGX30 move per unit devaluation)\n",
                    fit.model.rho, fit.model.passthrough);
        std::cout << "  USD/EGP jumps:";
        for (auto i : fit.fx.jump_days)
            std::cout << "  " << egxq::format_date(pts[i + 1].date) << " " << signed_pct(std::expm1(fx_log[i]));
        std::cout << "\n  EGX30 jumps:  ";
        for (auto i : fit.index.jump_days)
            std::cout << "  " << egxq::format_date(pts[i + 1].date) << " " << signed_pct(std::expm1(idx_log[i]));
        std::cout << "\n";
        if (overridden)
            std::printf("  scenario: %.2f devaluations/yr, mean %s, sd %s log, pass-through %.2f\n",
                        model.fx.jump_rate, signed_pct(std::expm1(model.fx.jump_mean)).c_str(),
                        pct(model.fx.jump_stdev, 1).c_str(), model.passthrough);

        std::cout << "\nChance of at least one devaluation jump:";
        for (auto h : horizons)
            std::cout << "  " << h << "d " << pct(-std::expm1(-model.fx.jump_rate * static_cast<double>(h) / kDaysPerYear), 1);
        std::cout << "\n";

        // Simulate once per horizon; both views come from the same draws.
        std::map<std::size_t, std::vector<HorizonDraw>> draws;
        for (auto h : horizons)
            draws[h] = simulate_horizon(model, static_cast<double>(h) / kDaysPerYear, paths, seed + h);

        for (View view : {View::Egp, View::Usd}) {
            const bool usd = view == View::Usd;
            const char* ccy = usd ? "USD" : "EGP";
            const std::optional<double> amount = notional ? std::optional<double>(usd ? *notional / last.fx : *notional)
                                                          : std::nullopt;
            std::cout << "\n[" << ccy << "]\n";
            std::printf("  %7s %5s %8s %8s %8s %7s %8s %9s", "horizon", "conf", "hist", "GBM", "JD", "+/-", "exact",
                        "JD CVaR");
            if (amount) std::printf(" %12s %12s", (std::string("VaR ") + ccy).c_str(), (std::string("CVaR ") + ccy).c_str());
            std::printf("\n");
            for (auto h : horizons) {
                const double years = static_cast<double>(h) / kDaysPerYear;
                const auto hist = hist_returns(usd ? usd_log : idx_log, h, is_short);
                const auto mix = horizon_mixture(model, view, years);
                const auto gbm_mix = horizon_mixture(gbm, view, years);
                for (double c : confidences) {
                    const auto mc = mc_tail(draws[h], view, c, is_short);
                    const auto ex = exact_tail(mix, c, is_short);
                    const auto g = exact_tail(gbm_mix, c, is_short);
                    const std::string hv = hist.empty() ? "-" : pct(historical_var(hist, c).var);
                    std::printf("  %6zud %4.1f%% %8s %8s %8s %7s %8s %9s", h, c * 100.0, hv.c_str(), pct(g.var).c_str(),
                                pct(mc.tail.var).c_str(), pct(mc.var_stderr).c_str(), pct(ex.var).c_str(),
                                pct(mc.tail.cvar).c_str());
                    if (amount)
                        std::printf(" %12s %12s", money(mc.tail.var * *amount).c_str(),
                                    money(mc.tail.cvar * *amount).c_str());
                    std::printf("\n");
                }
            }
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "egx_mc: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
