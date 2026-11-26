// egx_synth: write synthetic EGX market data CSVs for developing the pipeline
// before real data is available. The output is NOT real market data.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "egxquant/data/market_csv.hpp"
#include "egxquant/data/synthetic.hpp"

namespace {

constexpr const char* kUsage = R"(Usage: egx_synth [options]

Writes egx30.csv, futures.csv, tbills.csv, cpi.csv and usdegp.csv.
The data is SYNTHETIC (a random simulation), not real prices.

Options:
  --out <dir>              Output directory (default data/raw/synthetic)
  --start <YYYY-MM-DD>     First date (default 2024-01-01)
  --end <YYYY-MM-DD>       Last date (default 2026-11-25)
  --futures-start <date>   First futures trading date (default 2026-03-01)
  --seed <n>               Random seed (default 42); same seed, same data
  --help                   Show this message
)";

}  // namespace

int main(int argc, char** argv) {
    try {
        egxq::SyntheticConfig cfg;
        std::string out_dir = "data/raw/synthetic";

        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << kUsage;
                return EXIT_SUCCESS;
            }
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
            const std::string val = argv[++i];
            if (key == "--out") out_dir = val;
            else if (key == "--start") cfg.start = egxq::parse_date(val);
            else if (key == "--end") cfg.end = egxq::parse_date(val);
            else if (key == "--futures-start") cfg.futures_start = egxq::parse_date(val);
            else if (key == "--seed") cfg.seed = std::stoull(val);
            else throw std::invalid_argument("unknown option " + key);
        }

        const egxq::MarketData data = egxq::generate_synthetic(cfg);
        egxq::write_market_dir(out_dir, data);

        std::cout << "Wrote synthetic data to " << out_dir << "\n"
                  << "  egx30.csv    " << data.index_levels.size() << " rows\n"
                  << "  futures.csv  " << data.futures.size() << " rows\n"
                  << "  tbills.csv   " << data.tbills.size() << " rows\n"
                  << "  cpi.csv      " << data.cpi.size() << " rows\n"
                  << "  usdegp.csv   " << data.fx.size() << " rows\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "egx_synth: " << e.what() << "\n\n" << kUsage;
        return EXIT_FAILURE;
    }
}
