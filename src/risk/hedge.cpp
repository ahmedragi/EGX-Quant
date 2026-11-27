#include "egxquant/risk/hedge.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace egxq::risk {

HedgeFit fit_hedge(std::span<const double> portfolio_log, std::span<const double> hedge_log, double days_per_year) {
    if (portfolio_log.size() != hedge_log.size()) throw std::invalid_argument("fit_hedge: series lengths differ");
    if (portfolio_log.size() < 3) throw std::invalid_argument("fit_hedge: need at least 3 paired returns");

    const auto n = static_cast<double>(portfolio_log.size());
    double mp = 0.0, mh = 0.0;
    for (std::size_t i = 0; i < portfolio_log.size(); ++i) {
        mp += portfolio_log[i];
        mh += hedge_log[i];
    }
    mp /= n;
    mh /= n;
    double spp = 0.0, shh = 0.0, sph = 0.0;
    for (std::size_t i = 0; i < portfolio_log.size(); ++i) {
        const double dp = portfolio_log[i] - mp, dh = hedge_log[i] - mh;
        spp += dp * dp;
        shh += dh * dh;
        sph += dp * dh;
    }
    if (shh <= 0.0) throw std::invalid_argument("fit_hedge: the hedge series does not move");

    HedgeFit f;
    f.n = portfolio_log.size();
    f.beta = sph / shh;
    f.alpha = mp - f.beta * mh;
    f.correlation = spp > 0.0 ? sph / std::sqrt(spp * shh) : 0.0;
    f.r_squared = f.correlation * f.correlation;
    const double root = std::sqrt(days_per_year);
    f.portfolio_vol = std::sqrt(spp / (n - 1.0)) * root;
    f.hedge_vol = std::sqrt(shh / (n - 1.0)) * root;
    // var(p - beta h) = spp - 2 beta sph + beta^2 shh = spp - sph^2 / shh
    f.hedged_vol = std::sqrt(std::max(0.0, spp - f.beta * sph) / (n - 1.0)) * root;
    return f;
}

double hedge_contracts(double portfolio_value, double beta, double futures_price, double multiplier) {
    if (!(futures_price > 0.0) || !(multiplier > 0.0))
        throw std::invalid_argument("hedge_contracts: futures price and multiplier must be positive");
    return beta * portfolio_value / (futures_price * multiplier);
}

}  // namespace egxq::risk
