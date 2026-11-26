#pragma once

#include <cstddef>
#include <vector>

#include "egxquant/data/market_db.hpp"
#include "egxquant/futures/basis.hpp"

/// Basis history: every futures settlement in the database compared with its
/// cost-of-carry fair value on that day.

namespace egxq {

struct BasisSettings {
    double dividend_yield_pct = 3.0;  ///< EGX30 dividend yield, annual %
    double borrow_spread_pct = 1.0;   ///< Borrowing rate = T-bill + spread
    double lend_spread_pct = 1.0;     ///< Lending rate = T-bill - spread
    double spot_cost_pct = 0.30;      ///< Round-trip cost of trading the index basket
    double futures_cost_pct = 0.02;   ///< Round-trip futures cost (EGX fee: 0.01% per side)
};

struct BasisRow {
    FuturesSettlement futures;
    int days_to_expiry;
    IndexLevel spot;          ///< Latest EGX30 close on or before the settlement date
    bool spot_is_stale;       ///< Spot is from an earlier day than the settlement
    RateQuote rate;           ///< T-bill used for financing: tenor nearest to the horizon
    double rate_continuous;   ///< `rate` converted over the horizon
    double exchange_formula;  ///< EGX fallback settlement: S * exp(r91 * T), no dividends (see .cpp)
    BasisReport report;       ///< Fair value, mispricing, no-arbitrage band and signal
    double implied_rate_pct;  ///< Simple annual rate that makes fair value equal the settlement
};

struct BasisHistory {
    std::vector<BasisRow> rows;
    std::size_t skipped_no_spot = 0;  ///< No EGX30 close on or before the date
    std::size_t skipped_no_rate = 0;  ///< No T-bill auction on or before the date
};

/// Analyse every futures settlement with from <= date <= to.
BasisHistory basis_history(const MarketDb& db, const Date& from, const Date& to, const BasisSettings& settings = {});

}  // namespace egxq
