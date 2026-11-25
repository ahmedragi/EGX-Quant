# egx-quant

A C++ pricing and risk engine for Egypt's EGX30 index futures, which started trading on the Egyptian Exchange in March 2026.

The project prices EGX30 futures from first principles, checks observed prices against a no-arbitrage band, and (in later phases) measures portfolio risk in nominal EGP, real EGP and USD terms, including devaluation shocks.

## Why Egypt is an interesting case

- **High rates, big basis.** With policy and T-bill rates far above those in developed markets, the theoretical futures premium over spot is large, so small rate or dividend assumptions move fair value by hundreds of points.
- **Rate conventions matter.** Egyptian T-bill yields are quoted as simple rates. At 20%+ the gap between simple and continuous compounding is several tenths of a percent, so the engine converts explicitly.
- **Thin new market.** Futures volume is still low, which makes mispricing detection and basis tracking genuinely useful rather than a textbook exercise.
- **Currency risk dominates for foreign investors.** EGP devaluations are jumps, not smooth diffusions, which motivates the jump-diffusion risk model planned for Phase 4.

## The model

Cost of carry, continuous compounding:

```
F = S · e^((r − q)·T)
```

| Symbol | Meaning | Egyptian source |
|---|---|---|
| S | EGX30 spot level | EGX daily data |
| r | Risk-free rate | CBE T-bill auction yields (converted from simple) |
| q | Dividend yield of the index | Constituent dividends / index provider |
| T | Time to expiry (ACT/365) | Contract calendar |

With market frictions (borrow/lend spread, transaction costs) the fair price becomes a band:

```
upper = S·(1 + c_s)·e^((r_borrow − q)·T)·(1 + c_f)   → above: cash-and-carry
lower = S·(1 − c_s)·e^((r_lend   − q)·T)·(1 − c_f)   → below: reverse cash-and-carry
```

The engine also backs out the **implied rate** and **implied dividend yield** from a market price, which tells you what the market is "assuming" about funding costs.

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022). GoogleTest is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Usage

```bash
./build/apps/egx_pricer --spot 41500 --rate 22 --div-yield 3 --days 91 \
                        --market 43200 --borrow-spread 1 --lend-spread 1
```

```
EGX30 futures - cost of carry
  ----------------------------------------------
  Spot                              41500.00 pts
  Days to expiry                          91
  Rate (simple, quoted)               22.000 %
  Rate (continuous)                   21.418 %
  Dividend yield (cont.)               2.956 %
  Fair value                        43454.83 pts
  Theoretical basis                  1954.83 pts
  ...
  Signal                      REVERSE_CASH_AND_CARRY
```

Use `--valuation 2026-11-25 --expiry 2027-02-24` instead of `--days`, and `--json` for machine-readable output. Run with `--help` for all options.

> The numbers above are illustrative inputs, not live market data.

## Layout

```
include/egxquant/
  core/       rates.hpp (compounding conversions), daycount.hpp (dates, ACT/365)
  futures/    cost_of_carry.hpp (fair value, implied rate), basis.hpp (bounds, signals)
src/          implementations
apps/         egx_pricer CLI
tests/        GoogleTest suite (reference values computed independently)
data/         where raw and cleaned market data will live (not committed)
```

## Roadmap

- [x] **Phase 2 – Futures pricer:** cost of carry, discrete dividends, implied rate, no-arbitrage band, CLI
- [ ] **Phase 1 – Data pipeline:** EGX30 history, futures settlements, CBE yields, CPI, USD/EGP → SQLite
- [ ] **Basis history:** run the pricer across every trading day and chart fair value vs. market
- [ ] **Phase 3 – Historical VaR/CVaR:** nominal and inflation-adjusted
- [ ] **Phase 4 – Monte Carlo:** GBM + Merton jump-diffusion for EGP devaluation, USD-view VaR
- [ ] **Phase 5 – Hedging & stress:** hedge ratio, margin simulation, 2016 and 2022–24 shock replays
- [ ] **Phase 6 – Dashboard**

## Disclaimer

Educational project. Not investment advice. Signals ignore execution constraints such as short-selling availability, liquidity and margin requirements on the EGX.
