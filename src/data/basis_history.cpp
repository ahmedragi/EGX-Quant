#include "egxquant/data/basis_history.hpp"

#include <algorithm>
#include <cmath>

#include "egxquant/core/rates.hpp"
#include "egxquant/futures/cost_of_carry.hpp"

namespace egxq {

BasisHistory basis_history(const MarketDb& db, const Date& from, const Date& to, const BasisSettings& s) {
    BasisHistory out;
    const double q = annual_to_continuous(s.dividend_yield_pct / 100.0);

    for (const auto& f : db.futures_between(from, to)) {
        const auto spot = db.index_close_on_or_before(f.date);
        if (!spot) {
            ++out.skipped_no_spot;
            continue;
        }
        const int days = days_between(f.date, f.expiry);
        const auto rate = db.tbill_for_horizon(f.date, std::max(days, 1));
        const auto rate91 = db.tbill_for_horizon(f.date, 91);
        if (!rate || !rate91) {
            ++out.skipped_no_rate;
            continue;
        }

        const double T = year_fraction(days);
        // Convert the simple yield over the contract's own horizon (at least one day).
        const double horizon = year_fraction(std::max(days, 1));
        const double r = simple_to_continuous(rate->yield_pct / 100.0, horizon);

        const Frictions fr{
            .borrow_rate = simple_to_continuous((rate->yield_pct + s.borrow_spread_pct) / 100.0, horizon),
            .lend_rate = simple_to_continuous((rate->yield_pct - s.lend_spread_pct) / 100.0, horizon),
            .spot_cost = s.spot_cost_pct / 100.0,
            .futures_cost = s.futures_cost_pct / 100.0,
        };

        BasisRow row{
            .futures = f,
            .days_to_expiry = days,
            .spot = *spot,
            .spot_is_stale = spot->date != f.date,
            .rate = *rate,
            .rate_continuous = r,
            // EGX's fallback settlement price when there is too little trading
            // (written out for single-stock futures in the contract specs):
            // F = S * e^(r*T), r = 3-month (61-91 day) T-bill yield, no dividend
            // term. The yield is used as quoted, as the formula states. Our T-bill
            // data are primary auction yields, not the secondary-market yield the
            // exchange uses, so this is an approximation.
            .exchange_formula = spot->close * std::exp(rate91->yield_pct / 100.0 * T),
            .report = analyze_basis(CarryInputs{spot->close, r, q, T}, f.settle, fr),
            .implied_rate_pct = 0.0,
        };
        if (T > 0.0) {
            const double r_impl = implied_rate(f.settle, spot->close, q, T);
            row.implied_rate_pct = continuous_to_simple(r_impl, T) * 100.0;
        }
        out.rows.push_back(row);
    }
    return out;
}

}  // namespace egxq
