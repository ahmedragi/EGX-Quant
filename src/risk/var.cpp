#include "egxquant/risk/var.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "egxquant/risk/stats.hpp"

namespace egxq::risk {

namespace {

void check_confidence(double c) {
    if (!(c > 0.0 && c < 1.0)) throw std::invalid_argument("confidence must be in (0, 1)");
}

/// x * ln(y), taken as 0 when x == 0 (the limit used by likelihood ratios).
double xlogy(double x, double y) { return x == 0.0 ? 0.0 : x * std::log(y); }

}  // namespace

std::vector<double> log_returns(std::span<const double> prices) {
    std::vector<double> out;
    if (prices.size() < 2) return out;
    out.reserve(prices.size() - 1);
    for (std::size_t i = 0; i < prices.size(); ++i) {
        if (!(prices[i] > 0.0)) throw std::invalid_argument("log_returns: prices must be positive");
        if (i > 0) out.push_back(std::log(prices[i] / prices[i - 1]));
    }
    return out;
}

std::vector<double> horizon_log_returns(std::span<const double> daily_log, std::size_t h) {
    if (h == 0) throw std::invalid_argument("horizon must be at least 1 day");
    std::vector<double> out;
    if (daily_log.size() < h) return out;
    out.reserve(daily_log.size() - h + 1);
    double window = 0.0;
    for (std::size_t i = 0; i < h; ++i) window += daily_log[i];
    out.push_back(window);
    for (std::size_t i = h; i < daily_log.size(); ++i) {
        // Running sum; rounding drift is around 1e-15 at these series lengths.
        window += daily_log[i] - daily_log[i - h];
        out.push_back(window);
    }
    return out;
}

std::vector<double> to_simple_returns(std::span<const double> log_returns) {
    std::vector<double> out;
    out.reserve(log_returns.size());
    for (double r : log_returns) out.push_back(std::expm1(r));
    return out;
}

TailRisk historical_var(std::span<const double> returns, double confidence) {
    check_confidence(confidence);
    if (returns.empty()) throw std::invalid_argument("historical_var: no returns");

    std::vector<double> losses;
    losses.reserve(returns.size());
    for (double r : returns) losses.push_back(-r);
    std::sort(losses.begin(), losses.end());

    const double n = static_cast<double>(losses.size());
    // The small guard keeps n * c from rounding up past an integer (e.g. 20 * 0.95).
    const auto rank = static_cast<std::size_t>(std::ceil(n * confidence - 1e-9));
    const std::size_t k = std::min(losses.size() - 1, rank == 0 ? 0 : rank - 1);

    TailRisk t;
    t.sample_size = losses.size();
    t.var = losses[k];
    t.tail_count = losses.size() - k;
    double sum = 0.0;
    for (std::size_t i = k; i < losses.size(); ++i) sum += losses[i];
    t.cvar = sum / static_cast<double>(t.tail_count);
    return t;
}

TailRisk normal_var(double mean, double stdev, double confidence) {
    check_confidence(confidence);
    if (stdev < 0.0) throw std::invalid_argument("normal_var: negative stdev");
    const double z = stats::normal_quantile(confidence);
    TailRisk t;
    t.var = -mean + stdev * z;
    t.cvar = -mean + stdev * stats::normal_pdf(z) / (1.0 - confidence);
    return t;
}

KupiecResult kupiec_pof(std::size_t observations, std::size_t exceptions, double confidence) {
    check_confidence(confidence);
    if (exceptions > observations) throw std::invalid_argument("kupiec_pof: more exceptions than observations");
    KupiecResult k;
    k.observations = observations;
    k.exceptions = exceptions;
    k.expected_rate = 1.0 - confidence;
    if (observations == 0) return k;

    const double n = static_cast<double>(observations);
    const double x = static_cast<double>(exceptions);
    const double p = k.expected_rate;
    const double pi = x / n;
    k.observed_rate = pi;
    const double log_null = xlogy(n - x, 1.0 - p) + xlogy(x, p);
    const double log_alt = xlogy(n - x, 1.0 - pi) + xlogy(x, pi);
    k.lr = std::max(0.0, -2.0 * (log_null - log_alt));
    k.p_value = stats::chi2_1_sf(k.lr);
    return k;
}

Backtest rolling_backtest(std::span<const double> returns, std::size_t window, double confidence) {
    check_confidence(confidence);
    if (window == 0) throw std::invalid_argument("rolling_backtest: window must be positive");
    Backtest bt;
    if (returns.size() <= window) {
        bt.kupiec = kupiec_pof(0, 0, confidence);
        return bt;
    }
    std::size_t exceptions = 0;
    bt.days.reserve(returns.size() - window);
    for (std::size_t t = window; t < returns.size(); ++t) {
        const auto est = historical_var(returns.subspan(t - window, window), confidence);
        BacktestDay day{t, returns[t], est.var, est.cvar, -returns[t] > est.var};
        if (day.exception) ++exceptions;
        bt.days.push_back(day);
    }
    bt.kupiec = kupiec_pof(bt.days.size(), exceptions, confidence);
    return bt;
}

}  // namespace egxq::risk
