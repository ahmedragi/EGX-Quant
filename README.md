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

## Daily data collection (Windows)

EGX and Investing.com block automated downloads with bot protection, so saving the pages stays a manual click in your own browser. Everything after that is automated by the scripts in [tools/](tools/):

| Script | What it does |
|---|---|
| `open-bulletin.ps1` | Opens the EGX futures page (Sun–Thu); on Thursdays also the Investing.com EGX30 and USD/EGP history pages |
| `collect.ps1` | Finds new bulletins, EGX30/USD-EGP exports and T-bill workbooks in Downloads, then files, dates, imports and loads them, runs `egx_basis` and refreshes the dashboard |
| `install-tasks.ps1` | Registers both as Task Scheduler tasks in `\egx-quant\` (`-Remove` to undo) |

```powershell
powershell -ExecutionPolicy Bypass -File tools\install-tasks.ps1
```

**Your part each trading day:** when the page opens at 14:45 (after the 14:30 close), press **Ctrl+S**. On Thursdays, also click Download on the two Investing.com pages. Within 30 minutes `collect.ps1` imports everything.

**What the collector does to stay safe:**
- **Bulletin dates:** taken from the time you saved the page. A page saved during the session (10:00–14:30) is skipped, because those prices aren't settlements yet.
- **Holidays:** a bulletin identical to the previous trading day's is rejected, since on a holiday the page keeps showing the last trading day.
- **Each file once:** files are recorded by hash in `data/raw/ingested.tsv` and never processed twice. Your Downloads folder is left untouched.
- **Logging:** everything goes to `data/logs/collect-YYYY-MM.log`, including a note when the EGX30 close is behind the futures and needs refreshing.

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

## Historical VaR and CVaR

`egx_var` measures the risk of an EGX30 position from the closes in the database. Futures are delta-one, so index returns stand in for futures returns. Each view uses its own return series:
- **EGP:** nominal pounds.
- **USD:** EGX30 divided by USD/EGP, i.e. an unhedged foreign investor, with devaluations included.
- **Real EGP:** EGX30 deflated by CPI, i.e. purchasing power. Monthly CPI is placed mid-month and log-interpolated, and this view needs CPI loaded.

```bash
./build/apps/egx_var --contracts 10 --out data/clean/var_backtest.csv
```

```
[USD]  942 daily returns, 2023-01-03 to 2026-11-25
  volatility 32.4% p.a.   mean 0.069%/day   skew -7.69   excess kurtosis 146.7
  horizon  conf   hist VaR  hist CVaR     normal     sqrt-h        VaR USD       CVaR USD  samples
       1d 99.0%      4.93%      9.01%      4.70%          -            456            834  942
      10d 99.0%     26.04%     32.08%     14.40%     15.59%          2,408          2,967  933
  worst days:  2024-03-06 -39.52%  2024-01-31 -6.69%  ...
  backtest, 1-day VaR from the previous 250 days:
    99.0%: 11 exceptions in 692 days (expected 6.9)  Kupiec LR 2.06  p = 0.151  ok
```

For each horizon and confidence level the output shows:
- **Historical VaR:** the empirical loss quantile.
- **CVaR:** the mean loss at or beyond the VaR.
- **Normal VaR:** for comparison.
- **sqrt-h:** the 1-day VaR scaled by √h.
- **Amounts in the view's currency:** when you pass `--contracts` or `--notional`.

The output also lists the worst days. A rolling one-day-ahead backtest is checked with Kupiec's proportion-of-failures test.

In the USD view, the gap between the normal VaR and the historical CVaR is the devaluation jump on 2024-03-06. That gap is why Phase 4 models jumps.

Other options:
- `--short`
- `--window n` (last n returns only)
- `--from/--to`
- `--view egp,usd,real`
- `--confidence 95,99`
- `--horizon 1,10`

## Monte Carlo with devaluation jumps

`egx_mc` models the EGX30 (in EGP) and USD/EGP as a two-factor **Merton jump-diffusion**. Each factor has a normal diffusion plus Poisson-timed normal jumps, and the two are linked in two ways:
- **Correlation:** the diffusion parts of the two series are correlated.
- **Pass-through:** the EGX30 also moves by β times every USD/EGP jump, which is its reaction in pounds on a devaluation day.

How the model is fitted from the database history:
1. **Jumps:** a day counts as a jump when its move is further from the median than max(k × robust vol, floor). The robust vol comes from the median absolute deviation, so a single devaluation can't inflate it. The floor matters for a managed currency, where most days barely move.
2. **Pass-through:** fitted on the days USD/EGP jumped.
3. **Correlation:** fitted on the days with no jump in either series.

```bash
./build/apps/egx_mc --contracts 10
```

```
  USD/EGP jumps:  2023-01-04 +6.8%  2023-01-05 +2.8%  2023-01-11 +7.3%  2024-03-06 +60.4%  ...
Chance of at least one devaluation jump:  1d 0.6%  10d 6.2%  21d 12.5%

[USD]
  horizon  conf     hist      GBM       JD     +/-    exact   JD CVaR      VaR USD     CVaR USD
      10d 99.0%   26.04%   15.11%   26.27%   0.27%   26.19%    33.50%        2,429        3,098
```

Each row shows several VaR estimates:
- **hist:** historical VaR.
- **GBM:** a no-jump model with the same mean and volatility.
- **JD:** the jump-diffusion VaR by Monte Carlo, followed by its standard error.
- **exact:** the same VaR from the model's closed-form distribution. A Poisson-weighted mixture of normals, it checks the simulation.

The GBM understates 10-day 99% USD risk by about 11 points, and the jump model recovers it.

Simulation samples the horizon return directly, without time steps. It's deterministic for a given `--seed`, and identical across platforms because the random numbers come from the project's own generator.

The scenario flags `--fx-jump-rate`, `--fx-jump-size` (% rise in USD/EGP), `--fx-jump-stdev` and `--passthrough` replace the fitted values:

```bash
./build/apps/egx_mc --fx-jump-rate 0.5 --fx-jump-size 40 --horizon 21   # a 40% devaluation every two years
```

In these scenarios the total drift stays at its historical value, and the diffusion drift absorbs the change in expected jumps.

**Fitted jump sizes are normal, so they can also come out negative (an appreciation).** That's because the 2023–2026 sample includes one appreciation day (2026-04-08). If you want devaluation-only scenarios, set the size and spread yourself.

## Hedging, margin and stress tests

### Hedge ratio

`egx_hedge` works out how many futures to sell against a stock portfolio. With `--portfolio`, it fits the minimum-variance hedge ratio, which is the portfolio's beta to the EGX30 on their common dates, and the hedge effectiveness (R²). The portfolio file can be an Investing.com history export of a stock, or a `date,value` CSV. You can also pass `--beta` yourself.

```bash
./build/apps/egx_hedge --portfolio "data/raw/investing/COMI Historical Data.csv" --value 2000000
./build/apps/egx_hedge --beta 1.2 --value 2000000 --contract EGX30FMAR27
```

```
Hedge for 2,000,000 EGP at 53747.00 (EGX30FMAR27 settlement 2026-11-26)
  contracts to sell        44.65  -> 45
  initial margin         265,500 EGP (5,900 per contract)
```

### Margin and price limits

Futures settle daily within the exchange's **±10% price limit** (EGX specs). A bigger move than that is realised over several sessions. The margin account works like this:
1. It starts with the initial margin per contract.
2. Each day, the change in settlement price is credited or debited as variation margin.
3. When the balance falls below the maintenance margin, a margin call tops it back up to the initial margin.

The initial margin comes from the EGX bulletin (5,900 EGP). The default maintenance margin is 75% of that, which is an assumption. Pass `--maintenance` once EGX's figure is known.

### Stress tests

`egx_stress` has three commands:

| Command | What it does |
|---|---|
| `replay` | Runs today's position through the daily EGX30 and USD/EGP moves of past shocks |
| `grid` | Applies instant EGX30 × USD/EGP shocks, settled within the price limit |
| `margin` | Gives Monte Carlo margin-call odds and the cash to keep ready (liquidity at risk), from the Phase 4 jump-diffusion |

The built-in replay scenarios (`replay --list`):
- the 2016 float;
- March 2022;
- October 2022 to January 2023;
- March 2024.

```bash
./build/apps/egx_stress replay --basket 1000000 --hedge     # basket plus short futures
./build/apps/egx_stress replay --scenario float-2024 --daily
./build/apps/egx_stress grid --contracts 10
./build/apps/egx_stress margin --contracts 10 --days 21
```

```
scenario       period                 days    EGX30  USD/EGP locked calls     called    worst EGP      end EGP  worst USD    end USD basket USD
float-2024     2024-02-25..2024-04-30   42   -12.2%   +54.9%      0     3    178,379       -2,708       -2,708     -7,642     -7,240     -7,922
```

What the replays show:
- **In pounds the hedge works.** A 1,000,000 EGP basket shorted with 19 contracts ends the 2024 float within 3,000 EGP of where it started.
- **The hedge costs cash.** The index rallied before it fell, so the short futures drew 178,000 EGP of margin calls.
- **In dollars the hedge barely helps.** It ends at −7,240 USD against −7,922 USD unhedged, because the devaluation hits the basket and the margin deposit alike.
- **The hedge can make dollar losses worse.** In the October 2022 to January 2023 replay, the EGX30 rose in pounds and partly made up for the devaluation. Shorting futures gave that gain away, so the hedged position lost more in dollars than the unhedged basket.

**The 2016 and 2022 windows need data from before 2023.** Download a longer EGX30 and USD/EGP history from Investing.com: `egx_db import-investing` merges it with what's already loaded.

## Dashboard

`egx_dashboard` writes **one self-contained HTML page** from the database: inline CSS, SVG charts and a small hover script, with nothing loaded from the network. Open it in any browser. `tools/collect.ps1` regenerates it each time new data is imported.

```bash
./build/apps/egx_dashboard                        # writes data/clean/dashboard.html
./build/apps/egx_dashboard --contracts -20 --div-yield 3.5
```

The page has these sections:

| Section | Contents |
|---|---|
| Notices | Stale data: spot older than the latest bulletin, missing CPI, missing stress windows |
| Market | EGX30, USD/EGP, EGX30 in USD, the 91-day T-bill and the nearest futures contract, with daily and monthly changes |
| Futures and basis | The latest settlements against fair value, the exchange formula, the implied rate, the no-arbitrage band and the signal; a chart of mispricing by contract over time |
| History | EGX30 in pounds and dollars, both indexed to 100; USD/EGP with the days the model counts as devaluation jumps |
| Value at Risk | Historical, normal and jump-diffusion VaR/CVaR in EGP and USD for the position; the rolling backtest chart with exceptions and Kupiec tests |
| Stress tests and margin | Scenario replays, the instant-shock grid, and margin-call odds with the cash to keep ready |
| Data | Row counts and date ranges per table |

The charts follow light or dark mode, show values on hover, and use a colour palette checked for colour-blind readers. The output is deterministic for a given `--seed`.

## Layout

```
include/egxquant/
  core/       rates.hpp (compounding conversions), daycount.hpp (dates, ACT/365)
  futures/    cost_of_carry.hpp (fair value, implied rate), basis.hpp (bounds, signals),
              contract_spec.hpp (EGX30 codes, expiries, listing cycle)
  risk/       stats.hpp (normal distribution, moments), var.hpp (historical/normal VaR, CVaR, Kupiec backtest),
              random.hpp (portable RNG), jump_diffusion.hpp (Merton model, simulation, exact mixture),
              calibration.hpp (jump detection and fitting), hedge.hpp (minimum-variance hedge),
              margin.hpp (price limits, margin account, margin Monte Carlo), stress.hpp (scenario replay),
              scenarios.hpp (built-in stress windows)
  report/     chart.hpp (SVG time-series charts), dashboard.hpp (the HTML dashboard)
  core/       format.hpp (number formatting)
  data/       market_csv.hpp (CSV formats), market_db.hpp (SQLite store), importers.hpp,
              xlsx.hpp, html_table.hpp, basis_history.hpp, risk_series.hpp (EGP/USD/real views), synthetic.hpp
src/          implementations (egxquant: pricing + risk core; egxquant_data: data pipeline + SQLite;
              egxquant_report: charts and dashboard)
apps/         egx_pricer, egx_synth, egx_db, egx_basis, egx_var, egx_mc, egx_hedge, egx_stress, egx_dashboard CLIs
              (common/cli.hpp: shared option parsing and formatting)
tools/        Windows scripts for daily data collection (Task Scheduler)
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
  - [x] Chart fair value vs. market (dashboard; fills in as daily bulletins are collected)
- [x] **Phase 3 – Historical VaR/CVaR:** nominal EGP, USD and real EGP views, normal VaR comparison, Kupiec backtest (`egx_var`)
  - [ ] Load monthly CPI so the real-EGP view runs
- [x] **Phase 4 – Monte Carlo:** two-factor Merton jump-diffusion (EGX30 + USD/EGP) fitted to history, devaluation scenarios, EGP and USD VaR checked against the exact distribution (`egx_mc`)
- [x] **Phase 5 – Hedging & stress:** minimum-variance hedge ratio (`egx_hedge`), margin account with the ±10% price limit, margin-call Monte Carlo, shock grid and scenario replays (`egx_stress`)
  - [ ] Load EGX30 and USD/EGP history from before 2023 to run the 2016 and 2022 replays
  - [ ] Confirm EGX's maintenance margin (75% of initial is assumed)
- [x] **Phase 6 – Dashboard:** self-contained HTML page from the database (`egx_dashboard`), refreshed by the daily collector

## Disclaimer

Educational project. Not investment advice. Signals ignore execution constraints such as short-selling availability, liquidity and margin requirements on the EGX.
