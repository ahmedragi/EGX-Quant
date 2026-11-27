#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "egxquant/data/market_data.hpp"
#include "egxquant/data/market_db.hpp"

/// EGX30 level series in the currency views used for risk:
///  - nominal EGP: the closes as published;
///  - USD: close / USD-EGP rate, what an unhedged foreign investor holds;
///  - real EGP: close deflated by CPI, in pounds of the latest CPI month.

namespace egxq {

enum class RiskView { NominalEgp, Usd, RealEgp };

std::string_view to_string(RiskView v);  ///< "EGP", "USD", "real EGP"

/// "egp" / "nominal", "usd", "real" (case-insensitive).
std::optional<RiskView> parse_risk_view(std::string_view s);

struct ViewSeries {
    RiskView view = RiskView::NominalEgp;
    std::vector<IndexLevel> levels;  ///< Dates are the EGX30 close dates
    std::size_t dropped = 0;  ///< Closes left out for lack of FX or CPI data
};

/// Convert EGP closes to USD with the latest rate on or before each date.
/// A close is dropped when that rate is more than `max_stale_days` calendar
/// days old or there is none. `fx` must be sorted by date.
ViewSeries in_usd(const std::vector<IndexLevel>& egp, const std::vector<FxRate>& fx, int max_stale_days = 7);

/// Deflate EGP closes by CPI. Each month's CPI is placed on the 15th (it is a
/// monthly average) and log-interpolated between months; closes outside the
/// first and last 15th are dropped. Levels are in pounds of the last CPI month.
/// `cpi` must be sorted by month.
ViewSeries deflated(const std::vector<IndexLevel>& egp, const std::vector<CpiPoint>& cpi);

/// An EGX30 close with the USD/EGP rate in force that day.
struct IndexFxPoint {
    Date date;
    double index = 0.0;
    double fx = 0.0;
};

/// Pair each EGX30 close with the latest USD/EGP rate on or before its date,
/// dropping closes whose rate is missing or older than `max_stale_days`.
std::vector<IndexFxPoint> align_index_fx(const std::vector<IndexLevel>& egp, const std::vector<FxRate>& fx,
                                         int max_stale_days = 7);

/// The aligned EGX30 / USD-EGP series between two dates, from the database.
std::vector<IndexFxPoint> index_fx_between(const MarketDb& db, const Date& from, const Date& to);

/// Daily log returns of an aligned series; dates[i] is the day return i ends on.
struct DailyLogReturns {
    std::vector<Date> dates;
    std::vector<double> index;
    std::vector<double> fx;
};
DailyLogReturns daily_log_returns(const std::vector<IndexFxPoint>& points);

/// The EGX30 series between two dates in one view, read from the database.
ViewSeries index_view(const MarketDb& db, const Date& from, const Date& to, RiskView view);

}  // namespace egxq
