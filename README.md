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

Requires CMake ≥ 3.20 and a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022 or later). GoogleTest, SQLite and miniz (for reading .xlsx) are downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On Windows with Visual Studio (a multi-config generator), pick the configuration at build and test time; executables land in `build/apps/Release/`:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
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

## Market data pipeline

Market data lives in a SQLite database built from five CSV files (formats in [data/README.md](data/README.md)). Until real data is collected, `egx_synth` writes **synthetic** CSVs in the same formats, so the whole pipeline can be built and tested now:

```bash
./build/apps/egx_synth --out data/raw/synthetic          # simulated data, NOT real prices
./build/apps/egx_db load data/egx.db data/raw/synthetic  # safe to re-run: rows are upserted
./build/apps/egx_db snapshot data/egx.db 2026-06-15
```

```
Market snapshot for 2026-06-15
  EGX30 close        41461.27  (2026-06-15)
  USD/EGP            38.1600  (2026-06-15)
  T-bill 91d        30.786 %  (auction 2026-06-11)
  ...
  Futures:
    EGX30-202606  expiry 2026-06-25  settle 41948.22  vol 497  OI 3841
```

For real data, Investing.com exports and the T-bill auction workbook are imported directly (details in [data/README.md](data/README.md)):

```bash
./build/apps/egx_db import-investing index "data/raw/investing/EGX 30 Historical Data.csv" data/clean
./build/apps/egx_db import-investing fx    "data/raw/investing/USD_EGP Historical Data.csv" data/clean
./build/apps/egx_db import-tbills "data/raw/cbe/EGP T-Bills Auction Historical.xlsx" data/clean
./build/apps/egx_db import-futures data/raw/egx/bulletins/egx-futures-2026-11-26.html 2026-11-26 data/clean
./build/apps/egx_db load data/egx.db data/clean
```

Other real files go in the same folder under the standard file names, then run `egx_db load` on it. In code, `MarketDb` gives point-in-time lookups (`index_close_on_or_before`, `fx_on_or_before`, `tbill_for_horizon`, `futures_on`) and date ranges.

## Basis history

`egx_basis` compares every futures settlement in the database with its fair value on that day:
- **Spot:** the EGX30 close.
- **Rate:** the T-bill tenor nearest the contract's days to expiry, converted from simple to continuous.
- **Dividends:** the dividend yield, 3% unless you pass `--div-yield`.

```bash
./build/apps/egx_basis --db data/egx.db --out data/clean/basis_history.csv
```

```
date        contract    days       spot        rate       fair  exch.form     settle   misp.%   impl.r%  signal          flags
2026-11-26  EGX30FDEC26   20   50538.66   91d 24.92%   51145.77   51233.40   51462.00    0.618    36.355  CASH_AND_CARRY  stale
```

Each row also shows:
- **The no-arbitrage band and signal**, using the same frictions as `egx_pricer`.
- **The implied rate.**
- **`exch.form`, the exchange's fallback settlement formula** S·e^(r·T): the 3-month T-bill yield used as quoted, with no dividend term. EGX may use it as the settlement when there's too little trading. On low-volume days, compare the settlement with this column before reading anything into the mispricing.
- **The `stale` flag**, which means there's no EGX30 close for the settlement date yet, so the previous close was used.

Contract terms (codes `EGX30FMMMYY`, third-Wednesday expiry, two quarterly contracts, 1 EGP per point) are in [contract_spec.hpp](include/egxquant/futures/contract_spec.hpp), taken from the EGX specifications document.

## Layout

```
include/egxquant/
  core/       rates.hpp (compounding conversions), daycount.hpp (dates, ACT/365)
  futures/    cost_of_carry.hpp (fair value, implied rate), basis.hpp (bounds, signals),
              contract_spec.hpp (EGX30 codes, expiries, listing cycle)
  data/       market_csv.hpp (CSV formats), market_db.hpp (SQLite store), importers.hpp,
              xlsx.hpp, html_table.hpp, basis_history.hpp, synthetic.hpp
src/          implementations (egxquant: pricing core; egxquant_data: data pipeline + SQLite)
apps/         egx_pricer, egx_synth, egx_db, egx_basis CLIs
tests/        GoogleTest suite (reference values computed independently), fixtures/
data/         raw and cleaned market data (not committed)
```

## Roadmap

- [x] **Phase 2 – Futures pricer:** cost of carry, discrete dividends, implied rate, no-arbitrage band, CLI
- [ ] **Phase 1 – Data pipeline:** EGX30 history, futures settlements, CBE yields, CPI, USD/EGP → SQLite
  - [x] CSV formats, SQLite store with point-in-time lookups, loader CLI, synthetic data generator
  - [x] Investing.com importer (EGX30, USD/EGP) with merge across downloads
  - [x] T-bill auction workbook importer (.xlsx, all tenors)
  - [x] Real data loaded for 2023-01 to 2026-11: EGX30 closes, USD/EGP, T-bill auction yields
  - [x] EGX futures bulletin importer (saved HTML, Arabic or English) and contract calendar from the EGX specs
  - [ ] Still to collect: daily futures bulletins (no archive exists; history starts 2026-11-26), CPI, dividend yield
- [x] **Basis history:** `egx_basis` compares every settlement with fair value, the no-arbitrage band and the exchange formula
  - [ ] Chart fair value vs. market once enough daily bulletins are collected
- [ ] **Phase 3 – Historical VaR/CVaR:** nominal and inflation-adjusted
- [ ] **Phase 4 – Monte Carlo:** GBM + Merton jump-diffusion for EGP devaluation, USD-view VaR
- [ ] **Phase 5 – Hedging & stress:** hedge ratio, margin simulation, 2016 and 2022–24 shock replays
- [ ] **Phase 6 – Dashboard**

## Disclaimer

Educational project. Not investment advice. Signals ignore execution constraints such as short-selling availability, liquidity and margin requirements on the EGX.
