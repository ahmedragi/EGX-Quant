#pragma once

#include <cstddef>
#include <span>
#include <vector>

/// Historical and parametric Value at Risk and Expected Shortfall (CVaR).
///
/// Conventions:
///  - Returns are fractions (0.01 = 1%). A long position's loss is -return.
///  - VaR and CVaR are reported as positive loss fractions of the position value.
///  - Historical VaR at confidence c is the empirical quantile of the losses:
///    sort them ascending, VaR = L[k] with k = ceil(n * c) - 1 (the smallest
///    loss whose empirical CDF reaches c). CVaR is the mean of L[k..n-1], i.e.
///    the VaR observation and everything worse.

namespace egxq::risk {

/// Daily log returns ln(P[i] / P[i-1]). Throws on a non-positive price.
std::vector<double> log_returns(std::span<const double> prices);

/// Overlapping h-day log returns: sums of h consecutive daily log returns.
/// Returns an empty vector when there are fewer than h values.
std::vector<double> horizon_log_returns(std::span<const double> daily_log, std::size_t h);

/// exp(r) - 1 for each log return.
std::vector<double> to_simple_returns(std::span<const double> log_returns);

struct TailRisk {
    double var = 0.0;  ///< Loss fraction not exceeded with probability c
    double cvar = 0.0;  ///< Mean loss in the worst (1 - c) tail
    std::size_t tail_count = 0;  ///< Observations averaged for CVaR (0 for parametric)
    std::size_t sample_size = 0;
};

/// Historical VaR/CVaR of a long position from simple returns.
/// Throws if `returns` is empty or c is not in (0, 1).
TailRisk historical_var(std::span<const double> returns, double confidence);

/// Normal (variance-covariance) VaR/CVaR for returns ~ N(mean, stdev^2):
/// VaR = -mean + stdev * z_c, CVaR = -mean + stdev * phi(z_c) / (1 - c).
TailRisk normal_var(double mean, double stdev, double confidence);

/// Kupiec's proportion-of-failures test: is the exception rate consistent
/// with 1 - c? LR ~ chi-square(1) under the null.
struct KupiecResult {
    std::size_t observations = 0;
    std::size_t exceptions = 0;
    double expected_rate = 0.0;
    double observed_rate = 0.0;
    double lr = 0.0;
    double p_value = 1.0;
};
KupiecResult kupiec_pof(std::size_t observations, std::size_t exceptions, double confidence);

struct BacktestDay {
    std::size_t index = 0;  ///< Position in the returns series
    double ret = 0.0;  ///< Realised return that day
    double var = 0.0;  ///< VaR estimated from the previous `window` returns
    double cvar = 0.0;
    bool exception = false;  ///< Loss exceeded VaR
};

struct Backtest {
    std::vector<BacktestDay> days;
    KupiecResult kupiec;
};

/// Rolling one-step-ahead historical VaR: for each t >= window, estimate VaR from
/// returns[t - window, t) and compare with returns[t]. Empty if too few returns.
Backtest rolling_backtest(std::span<const double> returns, std::size_t window, double confidence);

}  // namespace egxq::risk
