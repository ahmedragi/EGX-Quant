#include "egxquant/risk/jump_diffusion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "egxquant/risk/random.hpp"
#include "egxquant/risk/stats.hpp"

namespace egxq::risk {

namespace {

void check(const JumpDiffusion& j) {
    if (j.sigma < 0.0 || j.jump_rate < 0.0 || j.jump_stdev < 0.0)
        throw std::invalid_argument("jump-diffusion: sigma, jump rate and jump stdev must be >= 0");
}

void check(const EgxFxModel& m, double years) {
    check(m.index);
    check(m.fx);
    if (m.rho < -1.0 || m.rho > 1.0) throw std::invalid_argument("jump-diffusion: rho must be in [-1, 1]");
    if (!(years > 0.0)) throw std::invalid_argument("jump-diffusion: horizon must be positive");
}

/// Poisson probabilities P(N = 0..n) until the remaining mass is below tol.
std::vector<double> poisson_pmf(double mean, double tol = 1e-12) {
    std::vector<double> p{std::exp(-mean)};
    double cdf = p[0];
    while (1.0 - cdf > tol && p.size() < 10000) {
        p.push_back(p.back() * mean / static_cast<double>(p.size()));
        cdf += p.back();
    }
    return p;
}

/// E[e^R ; R <= q] for R ~ N(mu, s^2), and its complement on R > q.
double exp_below(double mu, double s, double q) {
    if (s == 0.0) return mu <= q ? std::exp(mu) : 0.0;
    return std::exp(mu + 0.5 * s * s) * stats::normal_cdf((q - mu - s * s) / s);
}
double exp_above(double mu, double s, double q) {
    if (s == 0.0) return mu > q ? std::exp(mu) : 0.0;
    return std::exp(mu + 0.5 * s * s) * stats::normal_cdf((mu + s * s - q) / s);
}

}  // namespace

double index_diffusion_drift(const EgxFxModel& m) {
    return m.index.drift - m.index.jump_rate * m.index.jump_mean - m.passthrough * m.fx.jump_rate * m.fx.jump_mean;
}

double fx_diffusion_drift(const EgxFxModel& m) { return m.fx.drift - m.fx.jump_rate * m.fx.jump_mean; }

namespace {

/// Draws the log returns of one period of a fixed length.
class Stepper {
public:
    Stepper(const EgxFxModel& m, double years)
        : m_(m),
          sq_(std::sqrt(years)),
          a_s_(index_diffusion_drift(m) * years),
          a_x_(fx_diffusion_drift(m) * years),
          lam_s_(m.index.jump_rate * years),
          lam_x_(m.fx.jump_rate * years),
          rho_c_(std::sqrt(1.0 - m.rho * m.rho)) {}

    HorizonDraw draw(Rng& rng) const {
        const double z1 = rng.normal();
        const double z2 = rng.normal();
        const unsigned n_s = rng.poisson(lam_s_);
        const unsigned n_x = rng.poisson(lam_x_);
        double j_s = 0.0, j_x = 0.0;
        // The sum of n normal jumps is Normal(n * mean, n * stdev^2).
        if (n_s > 0) {
            const double n = static_cast<double>(n_s);
            j_s = n * m_.index.jump_mean + std::sqrt(n) * m_.index.jump_stdev * rng.normal();
        }
        if (n_x > 0) {
            const double n = static_cast<double>(n_x);
            j_x = n * m_.fx.jump_mean + std::sqrt(n) * m_.fx.jump_stdev * rng.normal();
        }
        const double r_s = a_s_ + m_.index.sigma * sq_ * z1 + j_s + m_.passthrough * j_x;
        const double r_x = a_x_ + m_.fx.sigma * sq_ * (m_.rho * z1 + rho_c_ * z2) + j_x;
        return {r_s, r_s - r_x};
    }

private:
    const EgxFxModel& m_;
    double sq_, a_s_, a_x_, lam_s_, lam_x_, rho_c_;
};

}  // namespace

std::vector<HorizonDraw> simulate_horizon(const EgxFxModel& m, double years, std::size_t paths, std::uint64_t seed) {
    check(m, years);
    Rng rng(seed);
    const Stepper step(m, years);
    std::vector<HorizonDraw> out;
    out.reserve(paths);
    for (std::size_t i = 0; i < paths; ++i) out.push_back(step.draw(rng));
    return out;
}

std::vector<std::vector<HorizonDraw>> simulate_daily_paths(const EgxFxModel& m, std::size_t days, std::size_t paths,
                                                           std::uint64_t seed, double days_per_year) {
    if (!(days_per_year > 0.0)) throw std::invalid_argument("simulate_daily_paths: days_per_year must be positive");
    check(m, 1.0 / days_per_year);
    Rng rng(seed);
    const Stepper step(m, 1.0 / days_per_year);
    std::vector<std::vector<HorizonDraw>> out(paths);
    for (auto& path : out) {
        path.reserve(days);
        for (std::size_t d = 0; d < days; ++d) path.push_back(step.draw(rng));
    }
    return out;
}

std::vector<MixtureComponent> horizon_mixture(const EgxFxModel& m, View view, double years) {
    check(m, years);
    const double a_s = index_diffusion_drift(m) * years;
    const double a_x = fx_diffusion_drift(m) * years;
    const double ss = m.index.sigma, sx = m.fx.sigma;

    // Coefficient of the FX jumps in the view, and the diffusion part.
    double fx_jump_coef = m.passthrough;
    double base_mean = a_s;
    double base_var = ss * ss * years;
    if (view == View::Usd) {
        fx_jump_coef = m.passthrough - 1.0;
        base_mean = a_s - a_x;
        base_var = (ss * ss + sx * sx - 2.0 * m.rho * ss * sx) * years;
    }

    const auto p_s = poisson_pmf(m.index.jump_rate * years);
    const auto p_x = poisson_pmf(m.fx.jump_rate * years);
    std::vector<MixtureComponent> mix;
    mix.reserve(p_s.size() * p_x.size());
    for (std::size_t i = 0; i < p_s.size(); ++i)
        for (std::size_t j = 0; j < p_x.size(); ++j) {
            const double w = p_s[i] * p_x[j];
            if (w < 1e-15) continue;
            const double ni = static_cast<double>(i), nj = static_cast<double>(j);
            const double mean = base_mean + ni * m.index.jump_mean + nj * fx_jump_coef * m.fx.jump_mean;
            const double var = base_var + ni * m.index.jump_stdev * m.index.jump_stdev +
                               nj * fx_jump_coef * fx_jump_coef * m.fx.jump_stdev * m.fx.jump_stdev;
            mix.push_back({w, mean, std::sqrt(std::max(0.0, var))});
        }
    return mix;
}

double mixture_cdf(const std::vector<MixtureComponent>& mix, double x) {
    double total = 0.0, cdf = 0.0;
    for (const auto& c : mix) {
        total += c.weight;
        if (c.stdev == 0.0)
            cdf += x >= c.mean ? c.weight : 0.0;
        else
            cdf += c.weight * stats::normal_cdf((x - c.mean) / c.stdev);
    }
    return total > 0.0 ? cdf / total : 0.0;
}

double mixture_quantile(const std::vector<MixtureComponent>& mix, double p) {
    if (mix.empty()) throw std::invalid_argument("mixture_quantile: empty mixture");
    if (!(p > 0.0 && p < 1.0)) throw std::invalid_argument("mixture_quantile: p must be in (0, 1)");
    double lo = mix.front().mean, hi = lo;
    for (const auto& c : mix) {
        lo = std::min(lo, c.mean - 40.0 * c.stdev - 1.0);
        hi = std::max(hi, c.mean + 40.0 * c.stdev + 1.0);
    }
    for (int i = 0; i < 200 && hi - lo > 1e-13; ++i) {
        const double mid = 0.5 * (lo + hi);
        (mixture_cdf(mix, mid) < p ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

TailRisk exact_tail(const std::vector<MixtureComponent>& mix, double confidence, bool is_short) {
    if (!(confidence > 0.0 && confidence < 1.0)) throw std::invalid_argument("confidence must be in (0, 1)");
    double total = 0.0;
    for (const auto& c : mix) total += c.weight;
    TailRisk t;
    if (!is_short) {
        // Loss = 1 - e^R, worst when R is low: tail R <= q(1 - c).
        const double q = mixture_quantile(mix, 1.0 - confidence);
        double e = 0.0;
        for (const auto& c : mix) e += c.weight * exp_below(c.mean, c.stdev, q);
        t.var = -std::expm1(q);
        t.cvar = 1.0 - (e / total) / (1.0 - confidence);
    } else {
        // Loss = e^R - 1, worst when R is high: tail R > q(c).
        const double q = mixture_quantile(mix, confidence);
        double e = 0.0;
        for (const auto& c : mix) e += c.weight * exp_above(c.mean, c.stdev, q);
        t.var = std::expm1(q);
        t.cvar = (e / total) / (1.0 - confidence) - 1.0;
    }
    return t;
}

McTail mc_tail(const std::vector<HorizonDraw>& draws, View view, double confidence, bool is_short,
               std::size_t batches) {
    if (draws.empty()) throw std::invalid_argument("mc_tail: no draws");
    std::vector<double> rets;
    rets.reserve(draws.size());
    for (const auto& d : draws) {
        const double r = view == View::Egp ? d.egp : d.usd;
        rets.push_back(is_short ? -std::expm1(r) : std::expm1(r));
    }
    McTail out;
    out.tail = historical_var(rets, confidence);

    if (batches >= 2 && draws.size() >= batches * 20) {
        const std::size_t size = draws.size() / batches;
        std::vector<double> vars;
        for (std::size_t b = 0; b < batches; ++b)
            vars.push_back(historical_var(std::span<const double>(rets).subspan(b * size, size), confidence).var);
        const auto m = stats::moments(vars);
        out.var_stderr = m.stdev / std::sqrt(static_cast<double>(batches));
    }
    return out;
}

}  // namespace egxq::risk
