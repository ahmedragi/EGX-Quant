# Data

Raw and cleaned files go in `data/raw/` and `data/clean/`, and the database in `data/egx.db`. All of these are git-ignored.

## Sources

| Dataset | Source | Frequency |
|---|---|---|
| EGX30 index level | egx.com.eg, investing.com historical | Daily |
| EGX30 futures settlement prices | egx.com.eg derivatives market bulletins | Daily |
| T-bill auction yields (91/182/273/364-day) | Central Bank of Egypt (cbe.org.eg) | Weekly auctions |
| Policy / overnight rates | Central Bank of Egypt | On MPC decisions |
| CPI / inflation | CAPMAS, Central Bank of Egypt | Monthly |
| USD/EGP | Central Bank of Egypt official rates | Daily |

Check each site's terms of use before automating downloads.

Futures history is short (trading began March 2026), so keep a daily log of settlements yourself in `futures.csv` format.

## CSV formats

`egx_db load <db> <dir>` reads whichever of these files exist in `<dir>`. Each needs a header row. Columns are matched by name in any order (case-insensitive), and extra columns are ignored. Dates are `YYYY-MM-DD`. Numbers may use thousands separators (`"41,500.25"`).

| File | Columns | Notes |
|---|---|---|
| `egx30.csv` | `date,close` | Index points |
| `futures.csv` | `date,contract,expiry,settle[,volume][,open_interest]` | One row per contract per day; `date` must not be after `expiry` |
| `tbills.csv` | `date,tenor_days,yield_pct` | Auction date, tenor in days, **simple** annual yield in percent (`22.5` = 22.5%) |
| `cpi.csv` | `month,index` | `month` as `YYYY-MM` |
| `usdegp.csv` | `date,rate` | EGP per 1 USD |

Rows are upserted on their natural key (date; date + contract; date + tenor; month), so re-loading a corrected file replaces the old values. A file with a bad row is rejected as a whole, and the error names the file and line.

## Importing Investing.com exports

Investing.com "Historical Data" downloads (EGX 30, USD/EGP) can be merged straight into the clean CSVs:

```bash
egx_db import-investing index "data/raw/investing/EGX 30 Historical Data.csv" data/clean
egx_db import-investing fx    "data/raw/investing/USD_EGP Historical Data.csv" data/clean
egx_db load data/egx.db data/clean
```

- Only the Date and Price (close) columns are used.
- US `MM/DD/YYYY` dates are read; `DD/MM/YYYY` is detected when a day is above 12. `Nov 25, 2026` and ISO dates also work.
- Each import is **merged** by date into `data/clean/egx30.csv` (or `usdegp.csv`), so you can download history in several date ranges and import each one. The newest import wins on overlapping dates, and the command reports how many dates were new or changed.
- Keep the original downloads in `data/raw/investing/` (name them by date range), so `data/clean/` can always be rebuilt.

## Importing the T-bill auction workbook

The EGP T-bill auction workbook (`.xlsx`, one sheet per tenor: 91, 182, 273, 336, 357, 364 days) is read directly:

```bash
egx_db import-tbills "data/raw/cbe/EGP T-Bills Auction Historical.xlsx" data/clean
egx_db load data/egx.db data/clean
```

- On each sheet the header row is found by name. The importer needs `Tenor (days)`, `Issue Date` and `Weighted Avg. Yield (%)`; `Maturity Date` is optional but used as a check. Sheets without these columns are ignored.
- **The issue date is stored as the date.** The auction itself is a day or two earlier, but by the issue date the yield is certainly public, so backtests can't use it too early.
- Each row is checked: maturity minus issue must be within 7 days of the tenor, which catches shifted columns. A repeated (date, tenor) pair is an error. Rows with no yield (cancelled auctions) are skipped and counted.
- Imports merge into `data/clean/tbills.csv` on (date, tenor), so a newer workbook can be imported over an older one.

## Importing the EGX futures bulletin

EGX publishes only the current day's futures prices (Derivatives → Derivatives Market Watch), with no history archive. To build a history, save the page **every trading day after the close** (Ctrl+S, "Webpage, HTML only") into `data/raw/egx/bulletins/egx-futures-YYYY-MM-DD.html`, then:

```bash
egx_db import-futures data/raw/egx/bulletins/egx-futures-2026-11-26.html 2026-11-26 data/clean
egx_db load data/egx.db data/clean
```

- Works with the Arabic or English page; columns are found by header name.
- Only EGX30 index futures are imported. Single-stock futures are counted and ignored.
- Each contract's code and expiry come from its name, using the EGX specifications (`futures/contract_spec.hpp`).
- The page does not show its own date, so you give the trading date.
- **Open interest is not published there, so it is stored as 0.**
- **Checks:** each settlement must be within the 10% daily limit of the previous settlement, and value ÷ volume must be close to the settlement. This catches misplaced decimal points; the page sometimes *displays* `53747000` for 53,747.000, although the saved HTML has had the decimal point.

The contract specifications PDF is kept in `data/raw/egx/` for reference.

Keep real and synthetic data in separate databases (for example `data/egx.db` and `data/synthetic.db`).

## Synthetic data

`egx_synth` writes all five files from a simulation (EGX30 random walk, USD/EGP devaluation jumps, CBE rate moves, futures priced at cost of carry plus noise). Use it to develop against, never as a stand-in for real results. The same `--seed` always gives the same data.

```bash
egx_synth --out data/raw/synthetic --start 2024-01-01 --end 2026-11-25 --seed 42
```
