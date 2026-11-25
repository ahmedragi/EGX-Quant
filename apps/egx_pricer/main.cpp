// egx_pricer: price an EGX30 futures contract with cost-of-carry and
// check an observed market price against the no-arbitrage band.
//
// Rates on the command line are *simple annual* percentages as quoted in
// Egypt (e.g. --rate 22.5 for a 22.5% T-bill yield). They are converted to
// continuous compounding internally over the contract's horizon.

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "egxquant/core/daycount.hpp"
#include "egxquant/core/rates.hpp"
#include "egxquant/futures/basis.hpp"
#include "egxquant/futures/cost_of_carry.hpp"

namespace {

constexpr const char* kUsage = R"(Usage: egx_pricer [options]

Required:
  --spot <points>         EGX30 spot level
  --rate <pct>            Risk-free rate, simple annual % (e.g. CBE T-bill yield)
  --days <n>              Calendar days to expiry
     or --valuation <YYYY-MM-DD> --expiry <YYYY-MM-DD>

Optional:
  --div-yield <pct>       Index dividend yield, annual % (default 0)
  --market <points>       Observed futures price, enables basis/arbitrage analysis
  --borrow-spread <pct>   Borrowing rate = rate + spread (default 0)
  --lend-spread <pct>     Lending rate = rate - spread (default 0)
  --spot-cost <pct>       Round-trip spot cost % (default 0.30)
  --fut-cost <pct>        Round-trip futures cost % (default 0.05)
  --multiplier <egp>      EGP per index point (default 1)
  --json                  Print JSON instead of a table
  --help                  Show this message

Example:
  egx_pricer --spot 41500 --rate 22 --div-yield 3 --days 91 --market 43200
)";

using Args = std::map<std::string, std::string>;

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0) throw std::invalid_argument("unexpected argument: " + key);
        key = key.substr(2);
        if (key == "json" || key == "help") { args[key] = "1"; continue; }
        if (i + 1 >= argc) throw std::invalid_argument("missing value for --" + key);
        args[key] = argv[++i];
    }
    return args;
}

double get_num(const Args& a, const std::string& k, std::optional<double> def = std::nullopt) {
    auto it = a.find(k);
    if (it == a.end()) {
        if (def) return *def;
        throw std::invalid_argument("missing required --" + k);
    }
    std::size_t pos = 0;
    const double v = std::stod(it->second, &pos);
    if (pos != it->second.size()) throw std::invalid_argument("bad number for --" + k + ": " + it->second);
    return v;
}

int resolve_days(const Args& a) {
    if (a.count("days")) return static_cast<int>(get_num(a, "days"));
    if (a.count("valuation") && a.count("expiry"))
        return egxq::days_between(egxq::parse_date(a.at("valuation")), egxq::parse_date(a.at("expiry")));
    throw std::invalid_argument("provide --days or both --valuation and --expiry");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args a = parse_args(argc, argv);
        if (a.count("help") || argc == 1) { std::cout << kUsage; return EXIT_SUCCESS; }

        const double spot       = get_num(a, "spot");
        const double rate_pct   = get_num(a, "rate");
        const double q_pct      = get_num(a, "div-yield", 0.0);
        const double multiplier = get_num(a, "multiplier", 1.0);
        const int days          = resolve_days(a);
        if (days <= 0) throw std::invalid_argument("days to expiry must be > 0");

        const double T = egxq::year_fraction(days);
        const double r = egxq::simple_to_continuous(rate_pct / 100.0, T);
        const double q = egxq::annual_to_continuous(q_pct / 100.0);

        const egxq::CarryInputs in{spot, r, q, T};
        const double fv = egxq::fair_value(in);
        const bool json = a.count("json") > 0;

        std::optional<egxq::BasisReport> rep;
        if (a.count("market")) {
            const egxq::Frictions fr{
                .borrow_rate  = egxq::simple_to_continuous((rate_pct + get_num(a, "borrow-spread", 0.0)) / 100.0, T),
                .lend_rate    = egxq::simple_to_continuous((rate_pct - get_num(a, "lend-spread", 0.0)) / 100.0, T),
                .spot_cost    = get_num(a, "spot-cost", 0.30) / 100.0,
                .futures_cost = get_num(a, "fut-cost", 0.05) / 100.0,
            };
            rep = egxq::analyze_basis(in, get_num(a, "market"), fr);
        }

        std::cout << std::fixed;
        if (json) {
            std::cout << std::setprecision(6) << "{\n"
                      << "  \"spot\": " << spot << ",\n"
                      << "  \"days\": " << days << ",\n"
                      << "  \"t_years\": " << T << ",\n"
                      << "  \"rate_continuous\": " << r << ",\n"
                      << "  \"div_yield_continuous\": " << q << ",\n"
                      << "  \"fair_value\": " << fv << ",\n"
                      << "  \"notional_egp\": " << egxq::contract_notional(fv, multiplier);
            if (rep) {
                std::cout << ",\n"
                          << "  \"market_price\": " << rep->market_price << ",\n"
                          << "  \"mispricing\": " << rep->mispricing << ",\n"
                          << "  \"mispricing_pct\": " << rep->mispricing_pct << ",\n"
                          << "  \"implied_rate_continuous\": " << egxq::implied_rate(rep->market_price, spot, q, T) << ",\n"
                          << "  \"lower_bound\": " << rep->bounds.lower << ",\n"
                          << "  \"upper_bound\": " << rep->bounds.upper << ",\n"
                          << "  \"signal\": \"" << egxq::to_string(rep->signal) << "\",\n"
                          << "  \"edge\": " << rep->edge;
            }
            std::cout << "\n}\n";
            return EXIT_SUCCESS;
        }

        auto row = [](const std::string& label, double v, int prec = 2, const std::string& unit = "") {
            std::cout << "  " << std::left << std::setw(28) << label
                      << std::right << std::setw(14) << std::setprecision(prec) << v << " " << unit << "\n";
        };

        std::cout << "\nEGX30 futures - cost of carry\n";
        std::cout << "  ----------------------------------------------\n";
        row("Spot", spot, 2, "pts");
        row("Days to expiry", days, 0);
        row("Rate (simple, quoted)", rate_pct, 3, "%");
        row("Rate (continuous)", r * 100.0, 3, "%");
        row("Dividend yield (cont.)", q * 100.0, 3, "%");
        row("Fair value", fv, 2, "pts");
        row("Theoretical basis", fv - spot, 2, "pts");
        row("Contract notional", egxq::contract_notional(fv, multiplier), 2, "EGP");

        if (rep) {
            std::cout << "\nMarket check\n";
            std::cout << "  ----------------------------------------------\n";
            row("Market price", rep->market_price, 2, "pts");
            row("Mispricing", rep->mispricing, 2, "pts");
            row("Mispricing", rep->mispricing_pct * 100.0, 3, "%");
            row("Implied rate (cont.)", egxq::implied_rate(rep->market_price, spot, q, T) * 100.0, 3, "%");
            row("No-arb lower bound", rep->bounds.lower, 2, "pts");
            row("No-arb upper bound", rep->bounds.upper, 2, "pts");
            std::cout << "  " << std::left << std::setw(28) << "Signal"
                      << std::right << std::setw(14) << egxq::to_string(rep->signal) << "\n";
            if (rep->signal != egxq::Signal::None) row("Edge beyond bound", rep->edge, 2, "pts");
            if (rep->signal == egxq::Signal::ReverseCashAndCarry)
                std::cout << "  note: requires shorting the index basket; check EGX short-selling rules.\n";
        }
        std::cout << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
