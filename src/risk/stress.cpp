#include "egxquant/risk/stress.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace egxq::risk {

StressResult stress_replay(std::span<const double> index_log, std::span<const double> fx_log,
                           const StressPosition& pos, const MarginRules& rules, double multiplier) {
    if (index_log.size() != fx_log.size()) throw std::invalid_argument("stress_replay: series lengths differ");
    if (!(pos.fx > 0.0)) throw std::invalid_argument("stress_replay: starting USD/EGP must be positive");
    const bool has_futures = pos.contracts != 0.0;
    if (has_futures && !(pos.futures_price > 0.0))
        throw std::invalid_argument("stress_replay: futures price must be positive");

    StressResult r;
    std::vector<bool> locked;
    std::vector<double> prices{has_futures ? pos.futures_price : 1.0};
    if (has_futures) {
        const auto s = limit_settlements(pos.futures_price, index_log, rules.price_limit, &locked);
        prices.insert(prices.end(), s.begin(), s.end());
        r.margin = run_margin(prices, pos.contracts, rules, multiplier, pos.excess_cash);
    }

    const double deposit = has_futures ? std::fabs(pos.contracts) * rules.initial + pos.excess_cash : 0.0;
    r.start_wealth_egp = pos.basket_egp + deposit;
    r.start_wealth_usd = r.start_wealth_egp / pos.fx;
    const double basket_usd0 = pos.basket_egp / pos.fx;

    double cum_idx = 0.0, cum_fx = 0.0, futures_pnl = 0.0;
    for (std::size_t t = 0; t < index_log.size(); ++t) {
        cum_idx += index_log[t];
        cum_fx += fx_log[t];
        StressDay d;
        d.index_level = std::exp(cum_idx);
        d.fx = pos.fx * std::exp(cum_fx);
        if (has_futures) {
            const auto& m = r.margin.days[t];
            d.settle = m.settle;
            d.limit_locked = locked[t];
            d.margin_call = m.call;
            futures_pnl += m.variation;
            if (d.limit_locked) ++r.locked_days;
        }
        const double basket = pos.basket_egp * d.index_level;
        const double wealth = basket + deposit + futures_pnl;
        d.pnl_egp = wealth - r.start_wealth_egp;
        d.pnl_usd = wealth / d.fx - r.start_wealth_usd;
        d.unhedged_pnl_egp = basket - pos.basket_egp;
        d.unhedged_pnl_usd = basket / d.fx - basket_usd0;
        r.worst_pnl_egp = std::min(r.worst_pnl_egp, d.pnl_egp);
        r.worst_pnl_usd = std::min(r.worst_pnl_usd, d.pnl_usd);
        r.days.push_back(d);
    }
    return r;
}

}  // namespace egxq::risk
