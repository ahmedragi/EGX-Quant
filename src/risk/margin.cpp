#include "egxquant/risk/margin.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace egxq::risk {

namespace {

double quantile_sorted(const std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    const auto rank = static_cast<std::size_t>(std::ceil(static_cast<double>(v.size()) * p - 1e-9));
    return v[std::min(v.size() - 1, rank == 0 ? 0 : rank - 1)];
}

}  // namespace

std::vector<double> limit_settlements(double start, std::span<const double> log_returns, double limit,
                                      std::vector<bool>* locked) {
    if (!(start > 0.0)) throw std::invalid_argument("limit_settlements: start price must be positive");
    if (!(limit > 0.0)) throw std::invalid_argument("limit_settlements: limit must be positive");
    std::vector<double> out;
    out.reserve(log_returns.size());
    if (locked) locked->assign(log_returns.size(), false);
    double fair_log = std::log(start);
    double prev = start;
    for (std::size_t i = 0; i < log_returns.size(); ++i) {
        fair_log += log_returns[i];
        const double fair = std::exp(fair_log);
        const double lo = prev * (1.0 - limit), hi = prev * (1.0 + limit);
        double s = fair;
        // A move of exactly the limit is allowed; the tolerance absorbs exp/log rounding.
        const double tol = 1e-12 * prev;
        if (fair < lo - tol || fair > hi + tol) {
            s = std::clamp(fair, lo, hi);
            if (locked) (*locked)[i] = true;
        }
        out.push_back(s);
        prev = s;
    }
    return out;
}

MarginRun run_margin(std::span<const double> settlements, double contracts, const MarginRules& rules,
                     double multiplier, double excess_cash) {
    if (settlements.empty()) throw std::invalid_argument("run_margin: no settlement prices");
    if (rules.maintenance > rules.initial) throw std::invalid_argument("run_margin: maintenance above initial");
    const double n = std::fabs(contracts);
    const double initial = n * rules.initial + excess_cash;
    const double maintenance = n * rules.maintenance;

    MarginRun run;
    double balance = initial;
    run.min_balance = balance;
    for (std::size_t t = 1; t < settlements.size(); ++t) {
        MarginDay d;
        d.settle = settlements[t];
        d.variation = contracts * multiplier * (settlements[t] - settlements[t - 1]);
        balance += d.variation;
        d.balance_before_call = balance;
        run.min_balance = std::min(run.min_balance, balance);
        if (balance < maintenance) {
            d.call = n * rules.initial - balance;
            balance += d.call;
            ++run.calls;
            run.total_called += d.call;
            run.max_call = std::max(run.max_call, d.call);
        }
        d.balance = balance;
        run.pnl += d.variation;
        run.days.push_back(d);
    }
    return run;
}

MarginRisk margin_monte_carlo(const EgxFxModel& model, std::size_t days, std::size_t paths, std::uint64_t seed,
                              double contracts, double futures_price, const MarginRules& rules, double multiplier,
                              double excess_cash, double days_per_year) {
    if (days == 0 || paths == 0) throw std::invalid_argument("margin_monte_carlo: need days and paths");
    const auto sims = simulate_daily_paths(model, days, paths, seed, days_per_year);

    MarginRisk r;
    r.paths = paths;
    r.days = days;
    std::vector<double> called;
    called.reserve(paths);
    std::vector<double> logs(days);
    std::vector<double> prices(days + 1);
    std::vector<bool> locked;
    std::size_t with_call = 0, with_deficit = 0, with_lock = 0, calls = 0;
    for (const auto& path : sims) {
        for (std::size_t t = 0; t < days; ++t) logs[t] = path[t].egp;
        const auto s = limit_settlements(futures_price, logs, rules.price_limit, &locked);
        prices[0] = futures_price;
        std::copy(s.begin(), s.end(), prices.begin() + 1);
        const auto run = run_margin(prices, contracts, rules, multiplier, excess_cash);
        if (run.calls > 0) ++with_call;
        if (run.min_balance < 0.0) ++with_deficit;
        if (std::find(locked.begin(), locked.end(), true) != locked.end()) ++with_lock;
        calls += run.calls;
        called.push_back(run.total_called);
    }
    const auto np = static_cast<double>(paths);
    r.p_call = static_cast<double>(with_call) / np;
    r.p_deficit = static_cast<double>(with_deficit) / np;
    r.p_locked = static_cast<double>(with_lock) / np;
    r.mean_calls = static_cast<double>(calls) / np;
    double sum = 0.0;
    for (double c : called) sum += c;
    r.called_mean = sum / np;
    std::sort(called.begin(), called.end());
    r.called_q95 = quantile_sorted(called, 0.95);
    r.called_q99 = quantile_sorted(called, 0.99);
    return r;
}

}  // namespace egxq::risk
