#include "egxquant/data/market_csv.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include "egxquant/data/csv.hpp"

namespace egxq {

namespace fs = std::filesystem;

// ---- formatting helpers shared with market_data.hpp -----------------------

std::string format_date(const Date& d) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(d.year()),
                  static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
    return buf;
}

std::string format_month(const std::chrono::year_month& ym) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u", static_cast<int>(ym.year()),
                  static_cast<unsigned>(ym.month()));
    return buf;
}

std::chrono::year_month parse_month(std::string_view s) {
    // Reuse the date parser by pinning the day to the 1st.
    if (s.size() != 7 || s[4] != '-')
        throw std::invalid_argument("parse_month: expected YYYY-MM, got '" + std::string(s) + "'");
    const Date d = parse_date(std::string(s) + "-01");
    return d.year() / d.month();
}

double parse_number(std::string_view s) {
    std::string clean;
    clean.reserve(s.size());
    for (char c : s)
        if (c != ',' && c != ' ' && c != '\t') clean += c;
    if (clean.empty()) throw std::invalid_argument("empty number");
    // strtod is used instead of from_chars because floating-point from_chars
    // is missing from some standard libraries (older Apple libc++).
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(clean.c_str(), &end);
    if (end != clean.c_str() + clean.size() || errno == ERANGE || !std::isfinite(v))
        throw std::invalid_argument("bad number '" + std::string(s) + "'");
    return v;
}

namespace {

std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

/// Run fn(row) for each data row, prefixing any error with file:line.
template <typename Fn>
void for_each_row(const fs::path& path, const csv::Table& t, Fn&& fn) {
    for (const auto& row : t.rows) {
        try {
            if (row.fields.size() < t.header.size())
                throw std::invalid_argument("expected " + std::to_string(t.header.size()) +
                                            " fields, got " + std::to_string(row.fields.size()));
            fn(row.fields);
        } catch (const std::exception& e) {
            throw std::runtime_error(path.string() + ":" + std::to_string(row.line) + ": " + e.what());
        }
    }
}

csv::Table read_table(const fs::path& path, std::initializer_list<const char*> required) {
    csv::Table t = csv::read_file(path);
    for (const char* col : required)
        if (t.find_column(col) == csv::Table::npos)
            throw std::runtime_error(path.string() + ": missing column '" + col + "'");
    return t;
}

std::int64_t parse_count(std::string_view s) {
    if (s.empty()) return 0;
    const double v = parse_number(s);
    if (v < 0 || v != std::floor(v)) throw std::invalid_argument("expected a whole number, got '" + std::string(s) + "'");
    return static_cast<std::int64_t>(v);
}

std::ofstream open_out(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path.string() + "'");
    return out;
}

}  // namespace

// ---- readers ---------------------------------------------------------------

std::vector<IndexLevel> read_index_csv(const fs::path& path) {
    const auto t = read_table(path, {"date", "close"});
    const auto c_date = t.column("date"), c_close = t.column("close");
    std::vector<IndexLevel> out;
    out.reserve(t.rows.size());
    for_each_row(path, t, [&](const auto& f) {
        const double close = parse_number(f[c_close]);
        if (close <= 0) throw std::invalid_argument("close must be positive");
        out.push_back({parse_date(f[c_date]), close});
    });
    return out;
}

std::vector<FuturesSettlement> read_futures_csv(const fs::path& path) {
    const auto t = read_table(path, {"date", "contract", "expiry", "settle"});
    const auto c_date = t.column("date"), c_contract = t.column("contract"), c_expiry = t.column("expiry"),
               c_settle = t.column("settle");
    const auto c_vol = t.find_column("volume"), c_oi = t.find_column("open_interest");
    std::vector<FuturesSettlement> out;
    out.reserve(t.rows.size());
    for_each_row(path, t, [&](const auto& f) {
        FuturesSettlement s;
        s.date = parse_date(f[c_date]);
        s.contract = f[c_contract];
        if (s.contract.empty()) throw std::invalid_argument("empty contract code");
        s.expiry = parse_date(f[c_expiry]);
        s.settle = parse_number(f[c_settle]);
        if (s.settle <= 0) throw std::invalid_argument("settle must be positive");
        if (days_between(s.date, s.expiry) < 0) throw std::invalid_argument("settlement date is after expiry");
        if (c_vol != csv::Table::npos) s.volume = parse_count(f[c_vol]);
        if (c_oi != csv::Table::npos) s.open_interest = parse_count(f[c_oi]);
        out.push_back(std::move(s));
    });
    return out;
}

std::vector<TbillYield> read_tbills_csv(const fs::path& path) {
    const auto t = read_table(path, {"date", "tenor_days", "yield_pct"});
    const auto c_date = t.column("date"), c_tenor = t.column("tenor_days"), c_yield = t.column("yield_pct");
    std::vector<TbillYield> out;
    out.reserve(t.rows.size());
    for_each_row(path, t, [&](const auto& f) {
        const auto tenor = parse_count(f[c_tenor]);
        if (tenor <= 0 || tenor > 3650) throw std::invalid_argument("tenor_days out of range");
        const double y = parse_number(f[c_yield]);
        if (y < 0 || y > 100) throw std::invalid_argument("yield_pct must be a percentage between 0 and 100");
        out.push_back({parse_date(f[c_date]), static_cast<int>(tenor), y});
    });
    return out;
}

std::vector<CpiPoint> read_cpi_csv(const fs::path& path) {
    const auto t = read_table(path, {"month", "index"});
    const auto c_month = t.column("month"), c_index = t.column("index");
    std::vector<CpiPoint> out;
    out.reserve(t.rows.size());
    for_each_row(path, t, [&](const auto& f) {
        const double v = parse_number(f[c_index]);
        if (v <= 0) throw std::invalid_argument("index must be positive");
        out.push_back({parse_month(f[c_month]), v});
    });
    return out;
}

std::vector<FxRate> read_fx_csv(const fs::path& path) {
    const auto t = read_table(path, {"date", "rate"});
    const auto c_date = t.column("date"), c_rate = t.column("rate");
    std::vector<FxRate> out;
    out.reserve(t.rows.size());
    for_each_row(path, t, [&](const auto& f) {
        const double r = parse_number(f[c_rate]);
        if (r <= 0) throw std::invalid_argument("rate must be positive");
        out.push_back({parse_date(f[c_date]), r});
    });
    return out;
}

// ---- writers ---------------------------------------------------------------

void write_index_csv(const fs::path& path, const std::vector<IndexLevel>& rows) {
    auto out = open_out(path);
    csv::write_row(out, {"date", "close"});
    for (const auto& r : rows) csv::write_row(out, {format_date(r.date), fmt(r.close, 2)});
}

void write_futures_csv(const fs::path& path, const std::vector<FuturesSettlement>& rows) {
    auto out = open_out(path);
    csv::write_row(out, {"date", "contract", "expiry", "settle", "volume", "open_interest"});
    for (const auto& r : rows)
        csv::write_row(out, {format_date(r.date), r.contract, format_date(r.expiry), fmt(r.settle, 2),
                             std::to_string(r.volume), std::to_string(r.open_interest)});
}

void write_tbills_csv(const fs::path& path, const std::vector<TbillYield>& rows) {
    auto out = open_out(path);
    csv::write_row(out, {"date", "tenor_days", "yield_pct"});
    for (const auto& r : rows)
        csv::write_row(out, {format_date(r.date), std::to_string(r.tenor_days), fmt(r.yield_pct, 3)});
}

void write_cpi_csv(const fs::path& path, const std::vector<CpiPoint>& rows) {
    auto out = open_out(path);
    csv::write_row(out, {"month", "index"});
    for (const auto& r : rows) csv::write_row(out, {format_month(r.month), fmt(r.index, 2)});
}

void write_fx_csv(const fs::path& path, const std::vector<FxRate>& rows) {
    auto out = open_out(path);
    csv::write_row(out, {"date", "rate"});
    for (const auto& r : rows) csv::write_row(out, {format_date(r.date), fmt(r.rate, 4)});
}

void write_market_dir(const fs::path& dir, const MarketData& data) {
    fs::create_directories(dir);
    write_index_csv(dir / files::kIndex, data.index_levels);
    write_futures_csv(dir / files::kFutures, data.futures);
    write_tbills_csv(dir / files::kTbills, data.tbills);
    write_cpi_csv(dir / files::kCpi, data.cpi);
    write_fx_csv(dir / files::kFx, data.fx);
}

MarketData read_market_dir(const fs::path& dir) {
    if (!fs::is_directory(dir)) throw std::runtime_error("not a directory: '" + dir.string() + "'");
    MarketData d;
    if (fs::exists(dir / files::kIndex)) d.index_levels = read_index_csv(dir / files::kIndex);
    if (fs::exists(dir / files::kFutures)) d.futures = read_futures_csv(dir / files::kFutures);
    if (fs::exists(dir / files::kTbills)) d.tbills = read_tbills_csv(dir / files::kTbills);
    if (fs::exists(dir / files::kCpi)) d.cpi = read_cpi_csv(dir / files::kCpi);
    if (fs::exists(dir / files::kFx)) d.fx = read_fx_csv(dir / files::kFx);
    return d;
}

}  // namespace egxq
