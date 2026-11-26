// egx_basis: compare every EGX30 futures settlement in the database with its
// cost-of-carry fair value, and write the basis history as CSV.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "egxquant/data/basis_history.hpp"
#include "egxquant/data/csv.hpp"

namespace {

constexpr const char* kUsage = R"(Usage: egx_basis [options]

Compares each futures settlement with its fair value F = S * exp((r - q) * T), where
S is the EGX30 close, r the T-bill yield nearest the contract horizon and q the
dividend yield. Also shows the exchange's fallback settlement formula S * exp(r91 * T).

Options:
  --db <file>             Database (default data/egx.db)
  --from <YYYY-MM-DD>     First settlement date (default: all)
  --to <YYYY-MM-DD>       Last settlement date (default: all)
  --out <file.csv>        Also write the full history as CSV
  --div-yield <pct>       EGX30 dividend yield, annual % (default 3)
  --borrow-spread <pct>   Borrowing rate = T-bill + spread (default 1)
  --lend-spread <pct>     Lending rate = T-bill - spread (default 1)
  --spot-cost <pct>       Round-trip index basket cost % (default 0.30)
  --fut-cost <pct>        Round-trip futures cost % (default 0.02)
  --help                  Show this message

Flags in the output:
  stale     no EGX30 close on the settlement date; the previous close was used
  no-trade  zero volume, so the settlement is likely the exchange's formula price
)";

std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string flags(const egxq::BasisRow& r) {
    std::string f;
    if (r.spot_is_stale) f += "stale";
    if (r.futures.volume == 0) f += f.empty() ? "no-trade" : " no-trade";
    return f;
}

void write_csv(const std::string& path, const egxq::BasisHistory& h) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    egxq::csv::write_row(out, {"date", "contract", "expiry", "days", "spot", "spot_date", "rate_tenor", "rate_pct",
                               "fair_value", "exchange_formula", "settle", "volume", "basis", "mispricing",
                               "mispricing_pct", "implied_rate_pct", "lower_bound", "upper_bound", "signal", "flags"});
    for (const auto& r : h.rows)
        egxq::csv::write_row(out, {egxq::format_date(r.futures.date), r.futures.contract,
                                   egxq::format_date(r.futures.expiry), std::to_string(r.days_to_expiry),
                                   num(r.spot.close, 2), egxq::format_date(r.spot.date),
                                   std::to_string(r.rate.tenor_days), num(r.rate.yield_pct, 3),
                                   num(r.report.fair_value, 2), num(r.exchange_formula, 2), num(r.futures.settle, 2),
                                   std::to_string(r.futures.volume), num(r.report.basis, 2),
                                   num(r.report.mispricing, 2), num(r.report.mispricing_pct * 100.0, 3),
                                   num(r.implied_rate_pct, 3), num(r.report.bounds.lower, 2),
                                   num(r.report.bounds.upper, 2), std::string(egxq::to_string(r.report.signal)),
                                   flags(r)});
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> a;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << kUsage;
                return EXIT_SUCCESS;
            }
            if (key.rfind("--", 0) != 0 || i + 1 >= argc) throw std::invalid_argument("bad argument " + key);
            a[key.substr(2)] = argv[++i];
        }
        auto get = [&](const char* k, const std::string& def) { return a.count(k) ? a.at(k) : def; };
        auto pct = [&](const char* k, double def) { return a.count(k) ? std::stod(a.at(k)) : def; };

        const std::string db_path = get("db", "data/egx.db");
        if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
        const egxq::MarketDb db{std::filesystem::path(db_path)};

        egxq::BasisSettings s;
        s.dividend_yield_pct = pct("div-yield", s.dividend_yield_pct);
        s.borrow_spread_pct = pct("borrow-spread", s.borrow_spread_pct);
        s.lend_spread_pct = pct("lend-spread", s.lend_spread_pct);
        s.spot_cost_pct = pct("spot-cost", s.spot_cost_pct);
        s.futures_cost_pct = pct("fut-cost", s.futures_cost_pct);

        const auto from = egxq::parse_date(get("from", "1900-03-01"));
        const auto to = egxq::parse_date(get("to", "2999-12-31"));
        const auto h = egxq::basis_history(db, from, to, s);

        if (h.rows.empty()) {
            std::cout << "No futures settlements to analyse in " << db_path
                      << ". Import a bulletin with: egx_db import-futures <page.html> <date> data/clean\n";
        } else {
            std::printf("%-10s  %-11s %4s %10s %11s %10s %10s %10s %8s %9s  %-22s %s\n", "date", "contract", "days",
                        "spot", "rate", "fair", "exch.form", "settle", "misp.%", "impl.r%", "signal", "flags");
            for (const auto& r : h.rows)
                std::printf("%-10s  %-11s %4d %10.2f %4dd %5.2f%% %10.2f %10.2f %10.2f %8.3f %9.3f  %-22s %s\n",
                            egxq::format_date(r.futures.date).c_str(), r.futures.contract.c_str(), r.days_to_expiry,
                            r.spot.close, r.rate.tenor_days, r.rate.yield_pct, r.report.fair_value,
                            r.exchange_formula, r.futures.settle, r.report.mispricing_pct * 100.0, r.implied_rate_pct,
                            std::string(egxq::to_string(r.report.signal)).c_str(), flags(r).c_str());
        }
        if (h.skipped_no_spot) std::cout << "Skipped " << h.skipped_no_spot << " settlements with no EGX30 close\n";
        if (h.skipped_no_rate) std::cout << "Skipped " << h.skipped_no_rate << " settlements with no T-bill yield\n";
        if (a.count("out")) {
            write_csv(a.at("out"), h);
            std::cout << "Wrote " << a.at("out") << "\n";
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "egx_basis: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
