#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "egxquant/data/market_data.hpp"

/// SQLite-backed store of EGX market data.
///
/// Dates are stored as ISO text, so text order is date order. Inserts are
/// upserts on the natural key (date, or date + contract, or date + tenor),
/// so re-loading a file is safe and a corrected file overwrites old rows.

namespace egxq {

/// The T-bill yield chosen as the risk-free rate for a horizon.
struct RateQuote {
    Date auction_date;
    int tenor_days;
    double yield_pct;  ///< Simple annual, percent
};

/// Row counts per table.
struct DbSummary {
    std::size_t index_levels = 0;
    std::size_t futures = 0;
    std::size_t tbills = 0;
    std::size_t cpi = 0;
    std::size_t fx = 0;
};

class MarketDb {
public:
    /// Open (creating if needed) a database file and make sure the schema exists.
    explicit MarketDb(const std::filesystem::path& path);

    /// A throwaway in-memory database.
    static MarketDb in_memory();

    ~MarketDb();
    MarketDb(MarketDb&&) noexcept;
    MarketDb& operator=(MarketDb&&) noexcept;
    MarketDb(const MarketDb&) = delete;
    MarketDb& operator=(const MarketDb&) = delete;

    // ---- writes (each call is one transaction) ----
    void insert(const std::vector<IndexLevel>& rows);
    void insert(const std::vector<FuturesSettlement>& rows);
    void insert(const std::vector<TbillYield>& rows);
    void insert(const std::vector<CpiPoint>& rows);
    void insert(const std::vector<FxRate>& rows);
    void insert(const MarketData& data);

    // ---- point lookups ----
    /// EGX30 close on exactly this date.
    std::optional<double> index_close(const Date& d) const;

    /// Most recent EGX30 close on or before this date.
    std::optional<IndexLevel> index_close_on_or_before(const Date& d) const;

    /// Most recent USD/EGP rate on or before this date.
    std::optional<FxRate> fx_on_or_before(const Date& d) const;

    /// CPI for a calendar month.
    std::optional<double> cpi(const std::chrono::year_month& ym) const;

    /// Risk-free rate for a horizon: for each tenor take its latest auction on
    /// or before `asof`, then pick the tenor closest to `horizon_days`
    /// (the shorter tenor on a tie).
    std::optional<RateQuote> tbill_for_horizon(const Date& asof, int horizon_days) const;

    /// All futures settlements on a date, ordered by expiry.
    std::vector<FuturesSettlement> futures_on(const Date& d) const;

    // ---- ranges ----
    /// EGX30 closes with from <= date <= to, ascending.
    std::vector<IndexLevel> index_between(const Date& from, const Date& to) const;

    /// Futures settlements with from <= date <= to, ordered by date then expiry.
    std::vector<FuturesSettlement> futures_between(const Date& from, const Date& to) const;

    DbSummary summary() const;

private:
    struct Impl;
    explicit MarketDb(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace egxq
