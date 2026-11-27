// egx_hedge: how many EGX30 futures to sell to hedge a stock portfolio, from
// its price history (minimum-variance hedge ratio) or a given beta.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../common/cli.hpp"
#include "egxquant/data/csv.hpp"
#include "egxquant/data/importers.hpp"
#include "egxquant/data/market_csv.hpp"
#include "egxquant/data/market_db.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "egxquant/risk/hedge.hpp"
#include "egxquant/risk/margin.hpp"
#include "egxquant/risk/stats.hpp"

namespace {

using namespace egxq::cli;

constexpr const char* kUsage = R"(Usage: egx_hedge --value <egp> (--portfolio <file.csv> | --beta <b>) [options]

How many EGX30 futures to sell to hedge a stock portfolio worth --value pounds.
With --portfolio, the hedge ratio is the portfolio's beta to the EGX30, fitted to
daily returns on the dates both series have (the minimum-variance hedge). The file
is an Investing.com history export (Date, Price, ...) or a CSV with date,value
(or date,close).

Options:
  --value <egp>            Portfolio value in EGP (required)
  --portfolio <file.csv>   Portfolio or stock price history
  --beta <b>               Or: use this beta
  --db <file>              Database (default data/egx.db)
  --from / --to <date>     Fitting window (default: all common dates)
  --futures-price <p>      Price to hedge at (default: latest settlement in the
                           database for --contract or the nearest expiry, else spot)
  --contract <code>        e.g. EGX30FMAR27
  --initial <egp>          Initial margin per contract (default 5900)
  --help                   Show this message

EGX30 futures settle in pounds, so the hedge removes index risk in EGP only: a
USD-based investor still carries the full USD/EGP risk on the hedged position.
The fitted beta uses index returns; once enough futures settlements are collected,
basis moves add some risk that this does not show.
)";

constexpr double kDaysPerYear = 250.0;

std::vector<egxq::IndexLevel> read_portfolio(const std::string& path) {
    const auto table = egxq::csv::read_file(path);
    if (table.find_column("Price") != egxq::csv::Table::npos) return egxq::to_index_levels(egxq::read_investing_history(path));
    const auto dc = table.column("date");
    const auto vc = table.find_column("value") != egxq::csv::Table::npos ? table.column("value") : table.column("close");
    std::vector<egxq::IndexLevel> out;
    for (const auto& row : table.rows) out.push_back({egxq::parse_date(row.fields.at(dc)), egxq::parse_number(row.fields.at(vc))});
    std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.date < y.date; });
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--help") {
                std::cout << kUsage;
                return EXIT_SUCCESS;
            }
        const auto a = parse_args(argc, argv, 1, {});
        if (!a.has("value")) throw std::invalid_argument("--value is required");
        if (a.has("portfolio") == a.has("beta")) throw std::invalid_argument("give --portfolio or --beta");
        const double value = std::stod(a.opts.at("value"));

        const std::string db_path = a.get("db", "data/egx.db");
        if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
        const egxq::MarketDb db{std::filesystem::path(db_path)};
        const auto from = egxq::parse_date(a.get("from", "1900-03-01"));
        const auto to = egxq::parse_date(a.get("to", "2999-12-31"));

        // Hedge ratio.
        double beta = a.number("beta", 0.0);
        std::optional<egxq::risk::HedgeFit> fit;
        if (a.has("portfolio")) {
            const auto port = read_portfolio(a.opts.at("portfolio"));
            const auto idx = db.index_between(from, to);
            std::map<egxq::Date, double> idx_by_day;
            for (const auto& l : idx) idx_by_day[l.date] = l.close;
            std::vector<std::pair<double, double>> common;  // (portfolio, index) on shared dates
            egxq::Date first{}, last{};
            for (const auto& p : port) {
                const auto it = idx_by_day.find(p.date);
                if (it == idx_by_day.end() || p.date < from || p.date > to) continue;
                if (common.empty()) first = p.date;
                last = p.date;
                common.emplace_back(p.close, it->second);
            }
            std::vector<double> pl, il;
            for (std::size_t i = 1; i < common.size(); ++i) {
                pl.push_back(std::log(common[i].first / common[i - 1].first));
                il.push_back(std::log(common[i].second / common[i - 1].second));
            }
            if (pl.size() < 20) throw std::runtime_error("fewer than 20 common daily returns with the EGX30");
            fit = egxq::risk::fit_hedge(pl, il, kDaysPerYear);
            beta = fit->beta;
            std::cout << "Fitted on " << fit->n << " daily returns, " << egxq::format_date(first) << " to "
                      << egxq::format_date(last) << "\n";
            std::printf("  beta (hedge ratio)    %8.3f\n", fit->beta);
            std::printf("  correlation           %8.3f\n", fit->correlation);
            std::printf("  hedge effectiveness   %8s   (R^2: share of variance removed)\n", pct(fit->r_squared, 1).c_str());
            std::printf("  volatility            %8s unhedged -> %s hedged\n", pct(fit->portfolio_vol, 1).c_str(),
                        pct(fit->hedged_vol, 1).c_str());
        }

        // Price to hedge at.
        double price = a.number("futures-price", 0.0);
        std::string priced_as = "--futures-price";
        if (price <= 0.0) {
            const auto all = db.futures_between(from, to);
            std::optional<egxq::FuturesSettlement> pick;
            for (const auto& f : all) {
                if (a.has("contract") && f.contract != a.opts.at("contract")) continue;
                if (!pick || f.date > pick->date) pick = f;  // latest date; first listed = nearest expiry
            }
            if (pick) {
                price = pick->settle;
                priced_as = pick->contract + " settlement " + egxq::format_date(pick->date);
            } else if (a.has("contract")) {
                throw std::runtime_error("no settlement for " + a.opts.at("contract") + " in the database");
            } else if (const auto spot = db.index_close_on_or_before(to)) {
                price = spot->close;
                priced_as = "EGX30 close " + egxq::format_date(spot->date) + " (no futures settlements)";
            } else {
                throw std::runtime_error("no futures settlement or EGX30 close to price the hedge");
            }
        }

        const double exact = egxq::risk::hedge_contracts(value, beta, price, egxq::egx30::kMultiplier);
        const double rounded = std::round(exact);
        egxq::risk::MarginRules rules;
        rules.initial = a.number("initial", rules.initial);

        std::cout << "\nHedge for " << money(value) << " EGP at " << num(price, 2) << " (" << priced_as << ")\n";
        std::printf("  beta                  %8.3f%s\n", beta, fit ? "" : "   (given)");
        std::printf("  contracts to sell     %8.2f  -> %s\n", exact, money(rounded).c_str());
        std::printf("  hedged notional       %8s EGP\n", money(rounded * price * egxq::egx30::kMultiplier).c_str());
        std::printf("  initial margin        %8s EGP (%s per contract)\n", money(std::fabs(rounded) * rules.initial).c_str(),
                    money(rules.initial).c_str());

        if (fit) {
            const double z = egxq::stats::normal_quantile(0.99);
            const double day = 1.0 / std::sqrt(kDaysPerYear);
            std::printf("  1-day 99%% VaR (normal) %7s EGP unhedged -> %s EGP hedged\n",
                        money(value * fit->portfolio_vol * day * z).c_str(),
                        money(value * fit->hedged_vol * day * z).c_str());
        }
        std::cout << "\nThe hedge covers EGX30 moves in pounds. USD/EGP risk is not hedged: for a dollar-based\n"
                     "investor a devaluation still cuts the hedged position's value by the size of the move.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "egx_hedge: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
