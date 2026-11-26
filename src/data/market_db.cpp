#include "egxquant/data/market_db.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace egxq {

namespace {

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS index_levels (
    date  TEXT PRIMARY KEY,
    close REAL NOT NULL CHECK (close > 0)
);
CREATE TABLE IF NOT EXISTS futures_settlements (
    date          TEXT    NOT NULL,
    contract      TEXT    NOT NULL,
    expiry        TEXT    NOT NULL,
    settle        REAL    NOT NULL CHECK (settle > 0),
    volume        INTEGER NOT NULL DEFAULT 0,
    open_interest INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (date, contract)
);
CREATE INDEX IF NOT EXISTS futures_by_expiry ON futures_settlements (expiry);
CREATE TABLE IF NOT EXISTS tbill_auctions (
    date       TEXT    NOT NULL,
    tenor_days INTEGER NOT NULL CHECK (tenor_days > 0),
    yield_pct  REAL    NOT NULL,
    PRIMARY KEY (date, tenor_days)
);
CREATE INDEX IF NOT EXISTS tbills_by_tenor ON tbill_auctions (tenor_days, date);
CREATE TABLE IF NOT EXISTS cpi (
    month       TEXT PRIMARY KEY,
    index_value REAL NOT NULL CHECK (index_value > 0)
);
CREATE TABLE IF NOT EXISTS fx_usdegp (
    date TEXT PRIMARY KEY,
    rate REAL NOT NULL CHECK (rate > 0)
);
)sql";

[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    throw std::runtime_error("sqlite: " + what + ": " + (db ? sqlite3_errmsg(db) : "out of memory"));
}

/// RAII prepared statement with 1-based binding and 0-based column reads.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) fail(db, "prepare");
    }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int i, const std::string& v) {
        check(sqlite3_bind_text(stmt_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT));
        return *this;
    }
    Stmt& bind(int i, double v) {
        check(sqlite3_bind_double(stmt_, i, v));
        return *this;
    }
    Stmt& bind(int i, std::int64_t v) {
        check(sqlite3_bind_int64(stmt_, i, v));
        return *this;
    }
    Stmt& bind(int i, int v) {
        check(sqlite3_bind_int(stmt_, i, v));
        return *this;
    }

    /// Advance; true while a row is available.
    bool step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        fail(db_, "step");
    }

    /// Run to completion and reset for reuse with new bindings.
    void exec() {
        while (step()) {
        }
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    std::string text(int col) const {
        const auto* p = sqlite3_column_text(stmt_, col);
        return p ? reinterpret_cast<const char*>(p) : std::string{};
    }
    double real(int col) const { return sqlite3_column_double(stmt_, col); }
    std::int64_t int64(int col) const { return sqlite3_column_int64(stmt_, col); }
    Date date(int col) const { return parse_date(text(col)); }

private:
    void check(int rc) const {
        if (rc != SQLITE_OK) fail(db_, "bind");
    }
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error(std::string("sqlite: ") + msg);
    }
}

/// Commits on success, rolls back if an exception escapes.
class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) { exec_sql(db_, "BEGIN"); }
    ~Transaction() {
        if (!done_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit() {
        exec_sql(db_, "COMMIT");
        done_ = true;
    }

private:
    sqlite3* db_;
    bool done_ = false;
};

FuturesSettlement read_futures_row(const Stmt& s) {
    return FuturesSettlement{s.date(0), s.text(1), s.date(2), s.real(3), s.int64(4), s.int64(5)};
}

constexpr const char* kFuturesCols = "date, contract, expiry, settle, volume, open_interest";

}  // namespace

struct MarketDb::Impl {
    sqlite3* db = nullptr;

    explicit Impl(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "out of memory";
            sqlite3_close(db);
            throw std::runtime_error("sqlite: cannot open '" + path + "': " + msg);
        }
        exec_sql(db, kSchema);
    }
    ~Impl() { sqlite3_close(db); }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    std::size_t count(const char* table) const {
        Stmt s(db, (std::string("SELECT COUNT(*) FROM ") + table).c_str());
        s.step();
        return static_cast<std::size_t>(s.int64(0));
    }
};

MarketDb::MarketDb(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

// sqlite3_open takes UTF-8; u8string() keeps non-ASCII Windows paths intact.
MarketDb::MarketDb(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    impl_ = std::make_unique<Impl>(std::string(u8.begin(), u8.end()));
}

MarketDb MarketDb::in_memory() { return MarketDb(std::make_unique<Impl>(":memory:")); }

MarketDb::~MarketDb() = default;
MarketDb::MarketDb(MarketDb&&) noexcept = default;
MarketDb& MarketDb::operator=(MarketDb&&) noexcept = default;

// ---- writes ----------------------------------------------------------------

void MarketDb::insert(const std::vector<IndexLevel>& rows) {
    Transaction tx(impl_->db);
    Stmt s(impl_->db, "INSERT OR REPLACE INTO index_levels (date, close) VALUES (?1, ?2)");
    for (const auto& r : rows) s.bind(1, format_date(r.date)).bind(2, r.close).exec();
    tx.commit();
}

void MarketDb::insert(const std::vector<FuturesSettlement>& rows) {
    Transaction tx(impl_->db);
    Stmt s(impl_->db,
           "INSERT OR REPLACE INTO futures_settlements (date, contract, expiry, settle, volume, open_interest) "
           "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    for (const auto& r : rows)
        s.bind(1, format_date(r.date))
            .bind(2, r.contract)
            .bind(3, format_date(r.expiry))
            .bind(4, r.settle)
            .bind(5, r.volume)
            .bind(6, r.open_interest)
            .exec();
    tx.commit();
}

void MarketDb::insert(const std::vector<TbillYield>& rows) {
    Transaction tx(impl_->db);
    Stmt s(impl_->db, "INSERT OR REPLACE INTO tbill_auctions (date, tenor_days, yield_pct) VALUES (?1, ?2, ?3)");
    for (const auto& r : rows) s.bind(1, format_date(r.date)).bind(2, r.tenor_days).bind(3, r.yield_pct).exec();
    tx.commit();
}

void MarketDb::insert(const std::vector<CpiPoint>& rows) {
    Transaction tx(impl_->db);
    Stmt s(impl_->db, "INSERT OR REPLACE INTO cpi (month, index_value) VALUES (?1, ?2)");
    for (const auto& r : rows) s.bind(1, format_month(r.month)).bind(2, r.index).exec();
    tx.commit();
}

void MarketDb::insert(const std::vector<FxRate>& rows) {
    Transaction tx(impl_->db);
    Stmt s(impl_->db, "INSERT OR REPLACE INTO fx_usdegp (date, rate) VALUES (?1, ?2)");
    for (const auto& r : rows) s.bind(1, format_date(r.date)).bind(2, r.rate).exec();
    tx.commit();
}

void MarketDb::insert(const MarketData& d) {
    insert(d.index_levels);
    insert(d.futures);
    insert(d.tbills);
    insert(d.cpi);
    insert(d.fx);
}

// ---- lookups ---------------------------------------------------------------

std::optional<double> MarketDb::index_close(const Date& d) const {
    Stmt s(impl_->db, "SELECT close FROM index_levels WHERE date = ?1");
    s.bind(1, format_date(d));
    if (!s.step()) return std::nullopt;
    return s.real(0);
}

std::optional<IndexLevel> MarketDb::index_close_on_or_before(const Date& d) const {
    Stmt s(impl_->db, "SELECT date, close FROM index_levels WHERE date <= ?1 ORDER BY date DESC LIMIT 1");
    s.bind(1, format_date(d));
    if (!s.step()) return std::nullopt;
    return IndexLevel{s.date(0), s.real(1)};
}

std::optional<FxRate> MarketDb::fx_on_or_before(const Date& d) const {
    Stmt s(impl_->db, "SELECT date, rate FROM fx_usdegp WHERE date <= ?1 ORDER BY date DESC LIMIT 1");
    s.bind(1, format_date(d));
    if (!s.step()) return std::nullopt;
    return FxRate{s.date(0), s.real(1)};
}

std::optional<double> MarketDb::cpi(const std::chrono::year_month& ym) const {
    Stmt s(impl_->db, "SELECT index_value FROM cpi WHERE month = ?1");
    s.bind(1, format_month(ym));
    if (!s.step()) return std::nullopt;
    return s.real(0);
}

std::optional<RateQuote> MarketDb::tbill_for_horizon(const Date& asof, int horizon_days) const {
    // Latest auction per tenor on or before asof, then the tenor nearest the horizon.
    Stmt s(impl_->db, R"sql(
        SELECT t.date, t.tenor_days, t.yield_pct
        FROM tbill_auctions t
        JOIN (SELECT tenor_days, MAX(date) AS last_date
              FROM tbill_auctions WHERE date <= ?1 GROUP BY tenor_days) latest
          ON t.tenor_days = latest.tenor_days AND t.date = latest.last_date
        ORDER BY ABS(t.tenor_days - ?2), t.tenor_days
        LIMIT 1)sql");
    s.bind(1, format_date(asof)).bind(2, horizon_days);
    if (!s.step()) return std::nullopt;
    return RateQuote{s.date(0), static_cast<int>(s.int64(1)), s.real(2)};
}

std::vector<FuturesSettlement> MarketDb::futures_on(const Date& d) const {
    Stmt s(impl_->db, (std::string("SELECT ") + kFuturesCols +
                       " FROM futures_settlements WHERE date = ?1 ORDER BY expiry, contract")
                          .c_str());
    s.bind(1, format_date(d));
    std::vector<FuturesSettlement> out;
    while (s.step()) out.push_back(read_futures_row(s));
    return out;
}

std::vector<IndexLevel> MarketDb::index_between(const Date& from, const Date& to) const {
    Stmt s(impl_->db, "SELECT date, close FROM index_levels WHERE date BETWEEN ?1 AND ?2 ORDER BY date");
    s.bind(1, format_date(from)).bind(2, format_date(to));
    std::vector<IndexLevel> out;
    while (s.step()) out.push_back({s.date(0), s.real(1)});
    return out;
}

std::vector<FuturesSettlement> MarketDb::futures_between(const Date& from, const Date& to) const {
    Stmt s(impl_->db, (std::string("SELECT ") + kFuturesCols +
                       " FROM futures_settlements WHERE date BETWEEN ?1 AND ?2 ORDER BY date, expiry, contract")
                          .c_str());
    s.bind(1, format_date(from)).bind(2, format_date(to));
    std::vector<FuturesSettlement> out;
    while (s.step()) out.push_back(read_futures_row(s));
    return out;
}

std::vector<FxRate> MarketDb::fx_between(const Date& from, const Date& to) const {
    Stmt s(impl_->db, "SELECT date, rate FROM fx_usdegp WHERE date BETWEEN ?1 AND ?2 ORDER BY date");
    s.bind(1, format_date(from)).bind(2, format_date(to));
    std::vector<FxRate> out;
    while (s.step()) out.push_back({s.date(0), s.real(1)});
    return out;
}

std::vector<CpiPoint> MarketDb::cpi_between(const std::chrono::year_month& from,
                                            const std::chrono::year_month& to) const {
    Stmt s(impl_->db, "SELECT month, index_value FROM cpi WHERE month BETWEEN ?1 AND ?2 ORDER BY month");
    s.bind(1, format_month(from)).bind(2, format_month(to));
    std::vector<CpiPoint> out;
    while (s.step()) out.push_back({parse_month(s.text(0)), s.real(1)});
    return out;
}

DbSummary MarketDb::summary() const {
    return DbSummary{impl_->count("index_levels"), impl_->count("futures_settlements"),
                     impl_->count("tbill_auctions"), impl_->count("cpi"), impl_->count("fx_usdegp")};
}

}  // namespace egxq
