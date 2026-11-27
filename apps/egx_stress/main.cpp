// egx_stress: replay past Egyptian shocks on today's position, shock grids,
// and Monte Carlo margin-call risk for EGX30 futures.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "../common/cli.hpp"
#include "egxquant/data/market_db.hpp"
#include "egxquant/data/risk_series.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "egxquant/risk/calibration.hpp"
#include "egxquant/risk/margin.hpp"
#include "egxquant/risk/stress.hpp"

namespace {

using namespace egxq::cli;
using egxq::risk::MarginRules;
using egxq::risk::StressPosition;

constexpr const char* kUsage = R"(Usage: egx_stress <replay|grid|margin> [options]

  replay   Run today's position through past shocks (all built-in scenarios, one
           with --scenario, or any window with --from/--to)
  grid     Instant EGX30 x USD/EGP shocks, settled within the daily price limit
  margin   Monte Carlo margin-call risk over --days, from the jump-diffusion model

Position (today's prices from the database):
  --contracts <n>          Futures, signed: -10 = short 10 (default 10 if no basket)
  --basket <egp>           Value of an EGX30 stock basket held alongside
  --hedge                  Short beta * basket / price contracts against the basket
  --beta <b>               Basket beta for --hedge (default 1)
  --futures-price <p>      Entry price (default: latest settlement, else EGX30 close)
  --excess <egp>           Extra cash in the margin account (default 0)
Margin rules:
  --initial <egp>          Initial margin per contract (default 5900, EGX bulletin)
  --maintenance <egp>      Maintenance margin per contract (default 75% of initial:
                           an assumption, EGX does not publish it in the bulletin)
  --limit <pct>            Daily price limit (default 10, EGX specs)
replay:
  --scenario <name>        One of the scenarios listed by --list
  --from / --to <date>     Or a custom window
  --daily                  Day-by-day table (one scenario)
  --list                   List the built-in scenarios
grid:
  --index-shocks <list>    EGX30 moves in % (default -30,-20,-10,0,10)
  --fx-shocks <list>       USD/EGP moves in % (default 0,20,50)
margin:
  --days <n>               Horizon in trading days (default 21)
  --paths <n>              Simulated paths (default 20000)
  --seed <n>               Random seed (default 42)
  --db <file>              Database (default data/egx.db)
)";

struct Scenario {
    const char* name;
    const char* from;
    const char* to;
    const char* what;
};

constexpr Scenario kScenarios[] = {
    {"float-2016", "2016-10-20", "2016-12-30", "CBE floats the pound, 3 Nov 2016"},
    {"deval-2022-03", "2022-03-14", "2022-04-29", "Devaluation of 21 Mar 2022"},
    {"deval-2022-10", "2022-10-20", "2023-01-31", "Flexible rate from 27 Oct 2022, slide into Jan 2023"},
    {"float-2024", "2024-02-25", "2024-04-30", "Float of 6 Mar 2024"},
};

struct Setup {
    StressPosition pos;
    MarginRules rules;
    std::string priced_as;
    egxq::Date fx_date;
};

Setup make_setup(const egxq::MarketDb& db, const Args& a) {
    Setup s;
    const auto spot = db.index_close_on_or_before(egxq::parse_date("2999-12-31"));
    const auto fx = db.fx_on_or_before(egxq::parse_date("2999-12-31"));
    if (!spot || !fx) throw std::runtime_error("the database needs EGX30 closes and USD/EGP rates");
    s.pos.fx = fx->rate;
    s.fx_date = fx->date;

    double price = a.number("futures-price", 0.0);
    s.priced_as = "--futures-price";
    if (price <= 0.0) {
        std::optional<egxq::FuturesSettlement> pick;
        for (const auto& f : db.futures_between(egxq::parse_date("1900-03-01"), egxq::parse_date("2999-12-31")))
            if (!pick || f.date > pick->date) pick = f;
        if (pick) {
            price = pick->settle;
            s.priced_as = pick->contract + " settlement " + egxq::format_date(pick->date);
        } else {
            price = spot->close;
            s.priced_as = "EGX30 close " + egxq::format_date(spot->date);
        }
    }
    s.pos.futures_price = price;
    s.pos.basket_egp = a.number("basket", 0.0);
    s.pos.excess_cash = a.number("excess", 0.0);
    if (a.flag("hedge")) {
        if (s.pos.basket_egp <= 0.0) throw std::invalid_argument("--hedge needs --basket");
        s.pos.contracts = -std::round(a.number("beta", 1.0) * s.pos.basket_egp / (price * egxq::egx30::kMultiplier));
    } else {
        s.pos.contracts = a.number("contracts", s.pos.basket_egp > 0.0 ? 0.0 : 10.0);
    }
    s.rules.initial = a.number("initial", s.rules.initial);
    s.rules.maintenance = a.number("maintenance", 0.75 * s.rules.initial);
    s.rules.price_limit = a.number("limit", 10.0) / 100.0;
    return s;
}

void print_position(const Setup& s) {
    const auto& p = s.pos;
    std::cout << "Position:";
    if (p.basket_egp > 0.0) std::cout << " basket " << money(p.basket_egp) << " EGP;";
    std::cout << " " << (p.contracts < 0 ? "short " : "long ") << money(std::fabs(p.contracts)) << " futures at "
              << num(p.futures_price, 2) << " (" << s.priced_as << "); USD/EGP " << num(p.fx, 4) << " ("
              << egxq::format_date(s.fx_date) << ")\n";
    std::cout << "Margin: initial " << money(s.rules.initial) << ", maintenance " << money(s.rules.maintenance)
              << " per contract, limit +-" << pct(s.rules.price_limit, 0) << "\n";
}

int cmd_replay(const egxq::MarketDb& db, const Args& a) {
    if (a.flag("list")) {
        for (const auto& sc : kScenarios) std::printf("  %-14s %s to %s  %s\n", sc.name, sc.from, sc.to, sc.what);
        return EXIT_SUCCESS;
    }
    std::vector<Scenario> run;
    std::string custom_from, custom_to;
    if (a.has("from") || a.has("to")) {
        custom_from = a.get("from", "1900-03-01");
        custom_to = a.get("to", "2999-12-31");
        run.push_back({"custom", custom_from.c_str(), custom_to.c_str(), "custom window"});
    } else if (a.has("scenario")) {
        for (const auto& sc : kScenarios)
            if (a.opts.at("scenario") == sc.name) run.push_back(sc);
        if (run.empty()) throw std::invalid_argument("unknown scenario '" + a.opts.at("scenario") + "' (see --list)");
    } else {
        run.assign(std::begin(kScenarios), std::end(kScenarios));
    }

    const auto setup = make_setup(db, a);
    print_position(setup);
    std::printf("\n%-14s %-22s %4s %8s %8s %6s %5s %10s %12s %12s %10s %10s %10s\n", "scenario", "period", "days",
                "EGX30", "USD/EGP", "locked", "calls", "called", "worst EGP", "end EGP", "worst USD", "end USD",
                "basket USD");
    std::vector<std::string> missing;
    for (const auto& sc : run) {
        const auto from = egxq::parse_date(sc.from), to = egxq::parse_date(sc.to);
        const auto pts = egxq::index_fx_between(db, from, to);
        if (pts.size() < 2) {
            missing.push_back(sc.name);
            std::printf("%-14s %s..%s  no data\n", sc.name, sc.from, sc.to);
            continue;
        }
        const auto logs = egxq::daily_log_returns(pts);
        const auto r = egxq::risk::stress_replay(logs.index, logs.fx, setup.pos, setup.rules, egxq::egx30::kMultiplier);
        const auto& end = r.days.back();
        const std::string period = egxq::format_date(pts.front().date) + ".." + egxq::format_date(pts.back().date);
        std::printf("%-14s %-22s %4zu %8s %8s %6zu %5zu %10s %12s %12s %10s %10s %10s\n", sc.name, period.c_str(),
                    r.days.size(), signed_pct(end.index_level - 1.0).c_str(),
                    signed_pct(end.fx / setup.pos.fx - 1.0).c_str(), r.locked_days, r.margin.calls,
                    money(r.margin.total_called).c_str(), money(r.worst_pnl_egp).c_str(), money(end.pnl_egp).c_str(),
                    money(r.worst_pnl_usd).c_str(), money(end.pnl_usd).c_str(), money(end.unhedged_pnl_usd).c_str());
        if (std::chrono::sys_days{pts.front().date} > std::chrono::sys_days{from} + std::chrono::days{7})
            std::printf("%-14s (data starts %s, after the scenario start %s)\n", "",
                        egxq::format_date(pts.front().date).c_str(), sc.from);

        if (a.flag("daily") && run.size() == 1) {
            std::printf("\n%-10s %9s %9s %10s %6s %10s %12s %10s\n", "date", "EGX30", "USD/EGP", "settle", "lock",
                        "call", "P&L EGP", "P&L USD");
            for (std::size_t t = 0; t < r.days.size(); ++t) {
                const auto& d = r.days[t];
                std::printf("%-10s %9s %9.4f %10.2f %6s %10s %12s %10s\n", egxq::format_date(logs.dates[t]).c_str(),
                            signed_pct(d.index_level - 1.0).c_str(), d.fx, d.settle, d.limit_locked ? "LIMIT" : "",
                            d.margin_call > 0 ? money(d.margin_call).c_str() : "", money(d.pnl_egp).c_str(),
                            money(d.pnl_usd).c_str());
            }
        }
    }
    std::cout << "\nEGX30 and USD/EGP are cumulative over the window. P&L is the whole position (basket, margin\n"
                 "deposit and futures); \"basket USD\" is the basket alone, unhedged. Futures follow the index\n"
                 "within the daily limit; carry is ignored.\n";
    if (!missing.empty())
        std::cout << "No data for: " << [&] {
            std::string s;
            for (const auto& m : missing) s += (s.empty() ? "" : ", ") + m;
            return s;
        }() << ". Download a longer EGX30 and USD/EGP history from Investing.com; the importer merges it.\n";
    return EXIT_SUCCESS;
}

int cmd_grid(const egxq::MarketDb& db, const Args& a) {
    const auto setup = make_setup(db, a);
    print_position(setup);
    std::vector<double> idx_shocks, fx_shocks;
    for (const auto& s : split(a.get("index-shocks", "-30,-20,-10,0,10"))) idx_shocks.push_back(std::stod(s) / 100.0);
    for (const auto& s : split(a.get("fx-shocks", "0,20,50"))) fx_shocks.push_back(std::stod(s) / 100.0);

    std::printf("\n%7s %8s %6s %6s %10s %12s %10s %11s\n", "EGX30", "USD/EGP", "days", "calls", "called", "P&L EGP",
                "P&L USD", "basket USD");
    for (double fs : fx_shocks)
        for (double is : idx_shocks) {
            if (is <= -1.0 || fs <= -1.0) throw std::invalid_argument("shocks must be above -100%");
            const double li = std::log1p(is);
            const double lim = std::log1p(setup.rules.price_limit);
            const auto days = static_cast<std::size_t>(std::ceil(std::fabs(li) / lim - 1e-12)) + 1;
            std::vector<double> il(days, 0.0), fl(days, 0.0);
            il[0] = li;
            fl[0] = std::log1p(fs);
            const auto r = egxq::risk::stress_replay(il, fl, setup.pos, setup.rules, egxq::egx30::kMultiplier);
            const auto& end = r.days.back();
            std::printf("%7s %8s %6zu %6zu %10s %12s %10s %11s\n", signed_pct(is, 0).c_str(), signed_pct(fs, 0).c_str(),
                        r.locked_days + 1, r.margin.calls, money(r.margin.total_called).c_str(),
                        money(end.pnl_egp).c_str(), money(end.pnl_usd).c_str(), money(end.unhedged_pnl_usd).c_str());
        }
    std::cout << "\n\"days\" is how many sessions the futures need to reach the shocked level within the daily limit.\n";
    return EXIT_SUCCESS;
}

int cmd_margin(const egxq::MarketDb& db, const Args& a) {
    const auto setup = make_setup(db, a);
    print_position(setup);
    if (setup.pos.contracts == 0.0) throw std::invalid_argument("no futures in the position");
    const auto pts = egxq::index_fx_between(db, egxq::parse_date("1900-03-01"), egxq::parse_date("2999-12-31"));
    if (pts.size() < 60) throw std::runtime_error("need at least 60 EGX30 closes with USD/EGP rates");
    const auto logs = egxq::daily_log_returns(pts);
    const auto fit = egxq::risk::fit_egx_fx(logs.index, logs.fx, 250.0, {}, {});

    const auto days = static_cast<std::size_t>(std::stoul(a.get("days", "21")));
    const auto paths = static_cast<std::size_t>(std::stoul(a.get("paths", "20000")));
    const auto seed = static_cast<std::uint64_t>(std::stoull(a.get("seed", "42")));
    const auto r = egxq::risk::margin_monte_carlo(fit.model, days, paths, seed, setup.pos.contracts,
                                                   setup.pos.futures_price, setup.rules, egxq::egx30::kMultiplier,
                                                   setup.pos.excess_cash);
    const double deposit = std::fabs(setup.pos.contracts) * setup.rules.initial + setup.pos.excess_cash;
    std::cout << "\nMargin-call risk over " << days << " trading days, " << money(static_cast<double>(paths))
              << " paths (jump-diffusion fitted to " << logs.index.size() << " days of history)\n";
    std::printf("  deposit                         %12s EGP\n", money(deposit).c_str());
    std::printf("  P(at least one margin call)     %12s\n", pct(r.p_call, 1).c_str());
    std::printf("  expected number of calls        %12.2f\n", r.mean_calls);
    std::printf("  cash called: mean               %12s EGP\n", money(r.called_mean).c_str());
    std::printf("               95th percentile    %12s EGP\n", money(r.called_q95).c_str());
    std::printf("               99th percentile    %12s EGP\n", money(r.called_q99).c_str());
    std::printf("  P(one day's loss > the balance) %12s\n", pct(r.p_deficit, 2).c_str());
    std::printf("  P(price limit binds)            %12s\n", pct(r.p_locked, 2).c_str());
    std::cout << "\nThe percentiles are the cash to keep ready beside the deposit (liquidity at risk).\n";
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            std::cout << kUsage;
            return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
        }
        const std::string cmd = argv[1];
        const auto a = parse_args(argc, argv, 2, {"hedge", "daily", "list", "help"});
        if (a.flag("help")) {
            std::cout << kUsage;
            return EXIT_SUCCESS;
        }
        const std::string db_path = a.get("db", "data/egx.db");
        if (cmd != "replay" && cmd != "grid" && cmd != "margin") throw std::invalid_argument("unknown command " + cmd);
        if (cmd == "replay" && a.flag("list")) return cmd_replay(egxq::MarketDb::in_memory(), a);
        if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
        const egxq::MarketDb db{std::filesystem::path(db_path)};
        if (cmd == "replay") return cmd_replay(db, a);
        if (cmd == "grid") return cmd_grid(db, a);
        return cmd_margin(db, a);
    } catch (const std::exception& e) {
        std::cerr << "egx_stress: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
