// egx_dashboard: write the HTML dashboard (one self-contained page) from the market database.

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "../common/cli.hpp"
#include "egxquant/report/dashboard.hpp"

namespace {

constexpr const char* kUsage = R"(Usage: egx_dashboard [options]

Writes one self-contained HTML page (no internet needed to view it) with the market
snapshot, futures basis, VaR in EGP and USD, the VaR backtest, stress replays, shock
grid and margin-call risk. Open it in a browser; tools\collect.ps1 refreshes it.

Options:
  --db <file>              Database (default data/egx.db)
  --out <file.html>        Output (default data/clean/dashboard.html)
  --contracts <n>          Position for the risk sections, signed (default 10)
  --div-yield <pct>        EGX30 dividend yield for fair value (default 3)
  --initial <egp>          Initial margin per contract (default 5900)
  --maintenance <egp>      Maintenance margin per contract (default 75% of initial)
  --paths <n>              Monte Carlo paths for VaR (default 50000)
  --seed <n>               Random seed (default 42)
  --help                   Show this message
)";

std::string now_text() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto a = egxq::cli::parse_args(argc, argv, 1, {"help"});
        if (a.flag("help")) {
            std::cout << kUsage;
            return EXIT_SUCCESS;
        }
        const std::string db_path = a.get("db", "data/egx.db");
        if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
        const egxq::MarketDb db{std::filesystem::path(db_path)};

        egxq::report::DashboardSettings s;
        s.contracts = a.number("contracts", s.contracts);
        s.basis.dividend_yield_pct = a.number("div-yield", s.basis.dividend_yield_pct);
        s.margin.initial = a.number("initial", s.margin.initial);
        s.margin.maintenance = a.number("maintenance", 0.75 * s.margin.initial);
        s.mc_paths = static_cast<std::size_t>(std::stoul(a.get("paths", "50000")));
        s.seed = static_cast<std::uint64_t>(std::stoull(a.get("seed", "42")));
        s.generated_at = now_text();

        const std::filesystem::path out = a.get("out", "data/clean/dashboard.html");
        if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path());
        const auto html = egxq::report::build_dashboard(db, s);
        std::ofstream f(out, std::ios::binary);
        if (!f) throw std::runtime_error("cannot write " + out.string());
        f << html;
        std::cout << "Wrote " << out.string() << " (" << html.size() / 1024 << " KB)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "egx_dashboard: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
