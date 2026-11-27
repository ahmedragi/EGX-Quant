#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "egxquant/data/basis_history.hpp"
#include "egxquant/data/market_db.hpp"
#include "egxquant/risk/margin.hpp"

/// The HTML dashboard: one self-contained page (inline CSS, SVG and a small
/// hover script, nothing loaded from the network) built from the market
/// database. It brings together the market snapshot, futures basis, VaR in EGP
/// and USD (historical, GBM and jump-diffusion), the VaR backtest, stress
/// replays, the shock grid and margin-call risk for one position.

namespace egxq::report {

struct DashboardSettings {
    double contracts = 10.0;  ///< Signed position in EGX30 futures
    std::size_t mc_paths = 50000;  ///< Jump-diffusion VaR paths per horizon
    std::size_t margin_paths = 10000;
    std::size_t margin_days = 21;
    std::uint64_t seed = 42;
    BasisSettings basis;
    risk::MarginRules margin;
    std::string generated_at;  ///< Shown in the header (left empty in tests for stable output)
};

std::string build_dashboard(const MarketDb& db, const DashboardSettings& settings);

}  // namespace egxq::report
