#include "egxquant/data/risk_series.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>

#include "egxquant/core/daycount.hpp"

namespace egxq {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

Date mid_month(const std::chrono::year_month& ym) { return ym / std::chrono::day{15}; }

}  // namespace

std::string_view to_string(RiskView v) {
    switch (v) {
        case RiskView::NominalEgp: return "EGP";
        case RiskView::Usd: return "USD";
        case RiskView::RealEgp: return "real EGP";
    }
    return "?";
}

std::optional<RiskView> parse_risk_view(std::string_view s) {
    const auto v = lower(s);
    if (v == "egp" || v == "nominal") return RiskView::NominalEgp;
    if (v == "usd") return RiskView::Usd;
    if (v == "real") return RiskView::RealEgp;
    return std::nullopt;
}

std::vector<IndexFxPoint> align_index_fx(const std::vector<IndexLevel>& egp, const std::vector<FxRate>& fx,
                                         int max_stale_days) {
    std::vector<IndexFxPoint> out;
    out.reserve(egp.size());
    std::size_t j = 0;  // fx[j] is the first rate after the current date
    for (const auto& lvl : egp) {
        const auto day = std::chrono::sys_days{lvl.date};
        while (j < fx.size() && std::chrono::sys_days{fx[j].date} <= day) ++j;
        if (j == 0 || days_between(fx[j - 1].date, lvl.date) > max_stale_days || !(fx[j - 1].rate > 0.0)) continue;
        out.push_back({lvl.date, lvl.close, fx[j - 1].rate});
    }
    return out;
}

std::vector<IndexFxPoint> index_fx_between(const MarketDb& db, const Date& from, const Date& to) {
    const Date fx_from{std::chrono::sys_days{from} - std::chrono::days{31}};
    return align_index_fx(db.index_between(from, to), db.fx_between(fx_from, to));
}

DailyLogReturns daily_log_returns(const std::vector<IndexFxPoint>& points) {
    DailyLogReturns r;
    for (std::size_t i = 1; i < points.size(); ++i) {
        r.dates.push_back(points[i].date);
        r.index.push_back(std::log(points[i].index / points[i - 1].index));
        r.fx.push_back(std::log(points[i].fx / points[i - 1].fx));
    }
    return r;
}

ViewSeries in_usd(const std::vector<IndexLevel>& egp, const std::vector<FxRate>& fx, int max_stale_days) {
    ViewSeries out;
    out.view = RiskView::Usd;
    for (const auto& p : align_index_fx(egp, fx, max_stale_days)) out.levels.push_back({p.date, p.index / p.fx});
    out.dropped = egp.size() - out.levels.size();
    return out;
}

ViewSeries deflated(const std::vector<IndexLevel>& egp, const std::vector<CpiPoint>& cpi) {
    ViewSeries out;
    out.view = RiskView::RealEgp;
    if (cpi.empty()) {
        out.dropped = egp.size();
        return out;
    }
    const double ref = std::log(cpi.back().index);
    std::size_t j = 0;  // cpi[j] is the first month whose 15th is after the current date
    for (const auto& lvl : egp) {
        const auto day = std::chrono::sys_days{lvl.date};
        while (j < cpi.size() && std::chrono::sys_days{mid_month(cpi[j].month)} <= day) ++j;
        double log_cpi = 0.0;
        if (j == 0) {
            ++out.dropped;  // before the first CPI stamp
            continue;
        }
        const Date left = mid_month(cpi[j - 1].month);
        if (std::chrono::sys_days{left} == day) {
            log_cpi = std::log(cpi[j - 1].index);
        } else if (j == cpi.size()) {
            ++out.dropped;  // after the last CPI stamp
            continue;
        } else {
            const Date right = mid_month(cpi[j].month);
            const double w = static_cast<double>(days_between(left, lvl.date)) /
                             static_cast<double>(days_between(left, right));
            log_cpi = (1.0 - w) * std::log(cpi[j - 1].index) + w * std::log(cpi[j].index);
        }
        out.levels.push_back({lvl.date, lvl.close * std::exp(ref - log_cpi)});
    }
    return out;
}

ViewSeries index_view(const MarketDb& db, const Date& from, const Date& to, RiskView view) {
    auto egp = db.index_between(from, to);
    switch (view) {
        case RiskView::NominalEgp: {
            ViewSeries out;
            out.levels = std::move(egp);
            return out;
        }
        case RiskView::Usd: {
            const Date fx_from{std::chrono::sys_days{from} - std::chrono::days{31}};
            return in_usd(egp, db.fx_between(fx_from, to));
        }
        case RiskView::RealEgp: {
            const auto m_from = std::chrono::year_month{from.year(), from.month()} - std::chrono::months{1};
            const auto m_to = std::chrono::year_month{to.year(), to.month()} + std::chrono::months{1};
            return deflated(egp, db.cpi_between(m_from, m_to));
        }
    }
    return {};
}

}  // namespace egxq
