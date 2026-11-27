#include "egxquant/data/synthetic.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>

#include "egxquant/core/rates.hpp"
#include "egxquant/futures/contract_spec.hpp"
#include "egxquant/futures/cost_of_carry.hpp"
#include "egxquant/risk/random.hpp"

namespace egxq {

using namespace std::chrono;

namespace {

using risk::Rng;  // portable: the same stream on every compiler

double round_to(double v, int decimals) {
    const double f = std::pow(10.0, decimals);
    return std::round(v * f) / f;
}

/// T-bill yield premium over the policy rate, in percentage points, by tenor.
double tenor_spread(int tenor_days) { return 0.25 + 0.75 * static_cast<double>(tenor_days) / 364.0; }

}  // namespace

bool is_egx_weekday(const Date& d) {
    const weekday wd{sys_days{d}};
    return wd != Friday && wd != Saturday;
}


MarketData generate_synthetic(const SyntheticConfig& cfg) {
    if (!cfg.start.ok() || !cfg.end.ok()) throw std::invalid_argument("generate_synthetic: invalid date");
    if (sys_days{cfg.end} < sys_days{cfg.start})
        throw std::invalid_argument("generate_synthetic: end is before start");

    Rng rng(cfg.seed);
    MarketData out;

    constexpr double dt = 1.0 / 250.0;  // trading-day year fraction for the diffusions
    const double drift = (cfg.index_drift - 0.5 * cfg.index_vol * cfg.index_vol) * dt;
    const double diffusion = cfg.index_vol * std::sqrt(dt);

    double spot = cfg.index_start;
    double fx = cfg.fx_start;
    double policy = cfg.policy_rate_start;
    bool hike_pending = false;  // set by a devaluation, honoured at the next meeting
    sys_days next_meeting = sys_days{cfg.start} + days{42};

    // Latest auction yield per tenor, seeded so futures can price before the first auction.
    std::map<int, double> latest_yield;
    for (int tenor : {91, 182, 273, 364}) latest_yield[tenor] = policy + tenor_spread(tenor);

    // Devaluation sizes by month, for CPI pass-through.
    std::map<year_month, double> devaluations;

    bool first_day = true;
    for (sys_days day = sys_days{cfg.start}; day <= sys_days{cfg.end}; day += days{1}) {
        const Date d{day};
        if (!is_egx_weekday(d)) continue;

        // ---- EGX30 and USD/EGP ----
        if (!first_day) {
            spot *= std::exp(drift + diffusion * rng.normal());
            fx *= std::exp(cfg.fx_daily_vol * rng.normal());
            if (rng.uniform() < cfg.fx_jump_prob) {
                const double jump = cfg.fx_jump_mean * (0.5 + rng.uniform());  // 0.5x to 1.5x the mean
                fx *= 1.0 + jump;
                devaluations[d.year() / d.month()] += jump;
                hike_pending = true;
            }
        }
        first_day = false;
        out.index_levels.push_back({d, round_to(spot, 2)});
        out.fx.push_back({d, round_to(fx, 4)});

        // ---- CBE policy meeting ----
        if (day >= next_meeting) {
            if (hike_pending) {
                policy += 2.0;
                hike_pending = false;
            } else {
                const double u = rng.uniform();
                if (u < 0.35) policy -= 1.0;
                else if (u > 0.85) policy += 1.0;
            }
            if (policy < 5.0) policy = 5.0;
            next_meeting += days{42};
        }

        // ---- T-bill auctions ----
        const weekday wd{day};
        std::vector<int> tenors;
        if (wd == Thursday) tenors = {91, 273};
        else if (wd == Sunday) tenors = {182, 364};
        for (int tenor : tenors) {
            const double y = round_to(policy + tenor_spread(tenor) + 0.15 * rng.normal(), 3);
            latest_yield[tenor] = y;
            out.tbills.push_back({d, tenor, y});
        }

        // ---- Futures: the two listed quarterly contracts ----
        if (sys_days{d} >= sys_days{cfg.futures_start}) {
            const auto listed = egx30::listed_contracts(d);
            for (std::size_t k = 0; k < listed.size(); ++k) {
                const year_month ym = listed[k];
                const Date expiry = egx30::expiry_date(ym);
                const int days_left = days_between(d, expiry);
                const double t = year_fraction(days_left);
                const double horizon = year_fraction(days_left < 1 ? 1 : days_left);
                const double r = simple_to_continuous(latest_yield[91] / 100.0, horizon);
                const double fair = fair_value(CarryInputs{spot, r, cfg.dividend_yield, t});
                const double settle = round_to(fair * (1.0 + cfg.futures_noise * rng.normal()), 2);
                const double vol_base = k == 0 ? 800.0 : 100.0;
                const auto volume = static_cast<std::int64_t>(vol_base * (0.25 + rng.uniform()));
                const auto oi = static_cast<std::int64_t>(vol_base * 4.0 * (0.5 + rng.uniform()));
                out.futures.push_back({d, egx30::contract_code(ym), expiry, settle, volume, oi});
            }
        }
    }

    // ---- CPI: monthly, with 40% devaluation pass-through spread over 3 months ----
    double cpi = cfg.cpi_start;
    const year_month first_month = cfg.start.year() / cfg.start.month();
    const year_month last_month = cfg.end.year() / cfg.end.month();
    for (year_month ym = first_month; ym <= last_month; ym += months{1}) {
        if (ym != first_month) {
            double inflation = 0.015 + 0.004 * rng.normal();
            for (int lag = 1; lag <= 3; ++lag) {
                const auto it = devaluations.find(ym - months{lag});
                if (it != devaluations.end()) inflation += 0.4 * it->second / 3.0;
            }
            cpi *= 1.0 + inflation;
        }
        out.cpi.push_back({ym, round_to(cpi, 2)});
    }

    return out;
}

}  // namespace egxq
