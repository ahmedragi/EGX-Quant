// egx_db: load market data CSVs into the SQLite database and inspect it.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "egxquant/data/importers.hpp"
#include "egxquant/data/market_csv.hpp"
#include "egxquant/data/market_db.hpp"

namespace {

constexpr const char* kUsage = R"(Usage:
  egx_db load <db-file> <csv-dir>     Load the CSVs found in <csv-dir> (upserts)
  egx_db info <db-file>               Row counts per table
  egx_db snapshot <db-file> <date>    Market state on a date (YYYY-MM-DD)
  egx_db import-investing <index|fx> <export.csv> <csv-dir>
                                      Merge an Investing.com "Historical Data" export
                                      into <csv-dir>/egx30.csv (index) or usdegp.csv (fx)
  egx_db import-tbills <auctions.xlsx> <csv-dir>
                                      Merge a T-bill auction workbook (one sheet per
                                      tenor) into <csv-dir>/tbills.csv
  egx_db import-futures <bulletin.html> <date> <csv-dir>
                                      Merge EGX30 settlements from a saved EGX
                                      Derivatives Market Watch page for trading day
                                      <date> into <csv-dir>/futures.csv

<csv-dir> may contain any of: egx30.csv futures.csv tbills.csv cpi.csv usdegp.csv

Example (real data):
  egx_db import-investing index "EGX 30 Historical Data.csv" data/clean
  egx_db load data/egx.db data/clean
  egx_db snapshot data/egx.db 2026-11-25

Example (synthetic data, keep it in a separate database):
  egx_synth --out data/raw/synthetic
  egx_db load data/synthetic.db data/raw/synthetic
)";

std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

template <typename Row>
void report_merge(const std::string& what, const std::vector<egxq::DatedPrice>& imported,
                  const egxq::MergeResult<Row>& m, const std::filesystem::path& out) {
    std::cout << "Imported " << imported.size() << " " << what << " rows (" << egxq::format_date(imported.front().date)
              << " to " << egxq::format_date(imported.back().date) << ")\n"
              << "  new dates      " << m.added << "\n"
              << "  changed values " << m.changed << "\n"
              << "  total rows     " << m.rows.size() << " (" << egxq::format_date(m.rows.front().date) << " to "
              << egxq::format_date(m.rows.back().date) << ")\n"
              << "Wrote " << out.string() << "\n";
}

int cmd_import_futures(const std::string& html_path, const std::string& date_str, const std::string& dir) {
    namespace fs = std::filesystem;
    const egxq::Date date = egxq::parse_date(date_str);
    const auto bulletin = egxq::read_egx_futures_bulletin_html(html_path, date);
    fs::create_directories(dir);
    const fs::path out = fs::path(dir) / egxq::files::kFutures;
    const auto existing = fs::exists(out) ? egxq::read_futures_csv(out) : std::vector<egxq::FuturesSettlement>{};
    const auto merged = egxq::merge_by_date(existing, bulletin.rows);
    egxq::write_futures_csv(out, merged.rows);

    std::cout << "EGX30 futures on " << date_str << ":\n";
    for (const auto& f : bulletin.rows)
        std::cout << "  " << f.contract << "  expiry " << egxq::format_date(f.expiry) << "  settle " << num(f.settle, 2)
                  << "  volume " << f.volume << "\n";
    if (bulletin.no_settlement) std::cout << "  skipped " << bulletin.no_settlement << " EGX30 rows with no settlement\n";
    if (bulletin.other_contracts) std::cout << "  ignored " << bulletin.other_contracts << " single-stock futures\n";
    std::cout << "  new rows       " << merged.added << "\n"
              << "  changed values " << merged.changed << "\n"
              << "  total rows     " << merged.rows.size() << "\n"
              << "Wrote " << out.string() << "\n";
    return EXIT_SUCCESS;
}

int cmd_import_tbills(const std::string& xlsx_path, const std::string& dir) {
    namespace fs = std::filesystem;
    const auto imp = egxq::read_tbill_auctions_xlsx(xlsx_path);
    fs::create_directories(dir);
    const fs::path out = fs::path(dir) / egxq::files::kTbills;
    const auto existing = fs::exists(out) ? egxq::read_tbills_csv(out) : std::vector<egxq::TbillYield>{};
    const auto merged = egxq::merge_by_date(existing, imp.rows);
    egxq::write_tbills_csv(out, merged.rows);

    std::cout << "Imported " << imp.rows.size() << " T-bill auctions (" << egxq::format_date(imp.rows.front().date)
              << " to " << egxq::format_date(imp.rows.back().date) << ")\n  by tenor:";
    for (int tenor : imp.tenors) {
        std::size_t n = 0;
        for (const auto& r : imp.rows) n += r.tenor_days == tenor ? 1 : 0;
        std::cout << " " << tenor << "d=" << n;
    }
    std::cout << "\n";
    if (imp.skipped) std::cout << "  skipped " << imp.skipped << " rows with no yield\n";
    std::cout << "  new rows       " << merged.added << "\n"
              << "  changed values " << merged.changed << "\n"
              << "  total rows     " << merged.rows.size() << "\n"
              << "Wrote " << out.string() << "\n";
    return EXIT_SUCCESS;
}

int cmd_import_investing(const std::string& kind, const std::string& export_path, const std::string& dir) {
    namespace fs = std::filesystem;
    const auto prices = egxq::read_investing_history(export_path);
    if (prices.empty()) throw std::runtime_error(export_path + ": no data rows");
    fs::create_directories(dir);

    if (kind == "index") {
        const fs::path out = fs::path(dir) / egxq::files::kIndex;
        const auto existing = fs::exists(out) ? egxq::read_index_csv(out) : std::vector<egxq::IndexLevel>{};
        const auto merged = egxq::merge_by_date(existing, egxq::to_index_levels(prices));
        egxq::write_index_csv(out, merged.rows);
        report_merge("EGX30", prices, merged, out);
    } else if (kind == "fx") {
        const fs::path out = fs::path(dir) / egxq::files::kFx;
        const auto existing = fs::exists(out) ? egxq::read_fx_csv(out) : std::vector<egxq::FxRate>{};
        const auto merged = egxq::merge_by_date(existing, egxq::to_fx_rates(prices));
        egxq::write_fx_csv(out, merged.rows);
        report_merge("USD/EGP", prices, merged, out);
    } else {
        throw std::invalid_argument("import-investing: kind must be 'index' or 'fx', got '" + kind + "'");
    }
    return EXIT_SUCCESS;
}

void print_summary(const egxq::DbSummary& s) {
    std::cout << "  index_levels         " << s.index_levels << "\n"
              << "  futures_settlements  " << s.futures << "\n"
              << "  tbill_auctions       " << s.tbills << "\n"
              << "  cpi                  " << s.cpi << "\n"
              << "  fx_usdegp            " << s.fx << "\n";
}

int cmd_load(const std::string& db_path, const std::string& dir) {
    const egxq::MarketData data = egxq::read_market_dir(dir);
    egxq::MarketDb db{std::filesystem::path(db_path)};
    db.insert(data);
    std::cout << "Loaded " << dir << " into " << db_path << "\n"
              << "Read: " << data.index_levels.size() << " index, " << data.futures.size() << " futures, "
              << data.tbills.size() << " t-bill, " << data.cpi.size() << " cpi, " << data.fx.size()
              << " fx rows\nDatabase now holds:\n";
    print_summary(db.summary());
    return EXIT_SUCCESS;
}

int cmd_info(const std::string& db_path) {
    if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
    const egxq::MarketDb db{std::filesystem::path(db_path)};
    std::cout << db_path << "\n";
    print_summary(db.summary());
    return EXIT_SUCCESS;
}

int cmd_snapshot(const std::string& db_path, const std::string& date_str) {
    if (!std::filesystem::exists(db_path)) throw std::runtime_error("no such database: " + db_path);
    const egxq::MarketDb db{std::filesystem::path(db_path)};
    const egxq::Date d = egxq::parse_date(date_str);

    std::cout << "Market snapshot for " << date_str << "\n";

    if (const auto px = db.index_close_on_or_before(d))
        std::cout << "  EGX30 close        " << num(px->close, 2) << "  (" << egxq::format_date(px->date) << ")\n";
    else
        std::cout << "  EGX30 close        n/a\n";

    if (const auto fx = db.fx_on_or_before(d))
        std::cout << "  USD/EGP            " << num(fx->rate, 4) << "  (" << egxq::format_date(fx->date) << ")\n";
    else
        std::cout << "  USD/EGP            n/a\n";

    if (const auto c = db.cpi(d.year() / d.month()))
        std::cout << "  CPI (month)        " << num(*c, 2) << "\n";

    for (int tenor : {91, 182, 273, 364}) {
        if (const auto q = db.tbill_for_horizon(d, tenor); q && q->tenor_days == tenor)
            std::cout << "  T-bill " << tenor << (tenor < 100 ? "d " : "d") << "       " << num(q->yield_pct, 3)
                      << " %  (auction " << egxq::format_date(q->auction_date) << ")\n";
    }

    const auto futs = db.futures_on(d);
    if (futs.empty()) {
        std::cout << "  Futures            none settled on this date\n";
    } else {
        std::cout << "  Futures:\n";
        for (const auto& f : futs)
            std::cout << "    " << f.contract << "  expiry " << egxq::format_date(f.expiry) << "  settle "
                      << num(f.settle, 2) << "  vol " << f.volume << "  OI " << f.open_interest << "\n";
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "--help" || cmd == "help") {
            std::cout << kUsage;
            return EXIT_SUCCESS;
        }
        if (cmd == "load" && argc == 4) return cmd_load(argv[2], argv[3]);
        if (cmd == "info" && argc == 3) return cmd_info(argv[2]);
        if (cmd == "snapshot" && argc == 4) return cmd_snapshot(argv[2], argv[3]);
        if (cmd == "import-investing" && argc == 5) return cmd_import_investing(argv[2], argv[3], argv[4]);
        if (cmd == "import-tbills" && argc == 4) return cmd_import_tbills(argv[2], argv[3]);
        if (cmd == "import-futures" && argc == 5) return cmd_import_futures(argv[2], argv[3], argv[4]);
        std::cerr << kUsage;
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "egx_db: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
