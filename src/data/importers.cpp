#include "egxquant/data/importers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "egxquant/data/csv.hpp"
#include "egxquant/data/market_csv.hpp"
#include "egxquant/data/html_table.hpp"
#include "egxquant/data/xlsx.hpp"
#include "egxquant/futures/contract_spec.hpp"

namespace egxq {

namespace {

int to_int(std::string_view s, std::string_view whole) {
    if (s.empty() || s.size() > 4) throw std::invalid_argument("bad date '" + std::string(whole) + "'");
    int v = 0;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            throw std::invalid_argument("bad date '" + std::string(whole) + "'");
        v = v * 10 + (c - '0');
    }
    return v;
}

Date make_date(int y, int m, int d, std::string_view whole) {
    const Date out{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)},
                   std::chrono::day{static_cast<unsigned>(d)}};
    if (!out.ok()) throw std::invalid_argument("not a valid calendar date '" + std::string(whole) + "'");
    return out;
}

/// "Nov 25, 2026" (also "November 25, 2026").
Date parse_month_name_date(std::string_view s) {
    static constexpr std::array<const char*, 12> kMonths = {"jan", "feb", "mar", "apr", "may", "jun",
                                                            "jul", "aug", "sep", "oct", "nov", "dec"};
    const auto sp = s.find(' ');
    const auto comma = s.find(',');
    if (sp == std::string_view::npos || comma == std::string_view::npos || comma < sp || sp < 3)
        throw std::invalid_argument("bad date '" + std::string(s) + "'");
    std::string mon(s.substr(0, 3));
    for (auto& c : mon) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto it = std::find_if(kMonths.begin(), kMonths.end(), [&](const char* m) { return mon == m; });
    if (it == kMonths.end()) throw std::invalid_argument("bad month in date '" + std::string(s) + "'");
    const int month = static_cast<int>(it - kMonths.begin()) + 1;
    const int day = to_int(s.substr(sp + 1, comma - sp - 1), s);
    std::string_view rest = s.substr(comma + 1);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    return make_date(to_int(rest, s), month, day, s);
}

struct SlashDate {
    int a, b, year;
};

SlashDate split_slash(std::string_view s) {
    const auto p1 = s.find('/');
    const auto p2 = s.find('/', p1 == std::string_view::npos ? 0 : p1 + 1);
    if (p1 == std::string_view::npos || p2 == std::string_view::npos)
        throw std::invalid_argument("bad date '" + std::string(s) + "'");
    const int year = to_int(s.substr(p2 + 1), s);
    if (year < 1900) throw std::invalid_argument("expected a 4-digit year in '" + std::string(s) + "'");
    return {to_int(s.substr(0, p1), s), to_int(s.substr(p1 + 1, p2 - p1 - 1), s), year};
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

template <typename Row, typename Same>
MergeResult<Row> merge_impl(const std::vector<Row>& base, const std::vector<Row>& update, Same same) {
    std::map<std::string, Row> by_date;
    for (const auto& r : base) by_date.insert_or_assign(format_date(r.date), r);
    MergeResult<Row> out;
    for (const auto& r : update) {
        const auto key = format_date(r.date);
        const auto it = by_date.find(key);
        if (it == by_date.end()) ++out.added;
        else if (!same(it->second, r)) ++out.changed;
        by_date.insert_or_assign(key, r);
    }
    out.rows.reserve(by_date.size());
    for (auto& [key, row] : by_date) out.rows.push_back(row);
    return out;
}

}  // namespace

std::vector<DatedPrice> read_investing_history(const std::filesystem::path& path) {
    const csv::Table t = csv::read_file(path);
    const auto c_date = t.find_column("date");
    const auto c_price = t.find_column("price");
    if (c_date == csv::Table::npos || c_price == csv::Table::npos)
        throw std::runtime_error(path.string() +
                                 ": not an Investing.com historical export (needs \"Date\" and \"Price\" columns)");

    // First pass: decide MM/DD vs DD/MM for slash dates across the whole file.
    bool first_over_12 = false, second_over_12 = false;
    for (const auto& row : t.rows) {
        if (c_date >= row.fields.size()) continue;
        const std::string s = trim(row.fields[c_date]);
        if (s.find('/') == std::string::npos) continue;
        try {
            const auto sd = split_slash(s);
            first_over_12 = first_over_12 || sd.a > 12;
            second_over_12 = second_over_12 || sd.b > 12;
        } catch (const std::exception&) {
            // reported with its line number in the second pass
        }
    }
    if (first_over_12 && second_over_12)
        throw std::runtime_error(path.string() + ": dates mix MM/DD and DD/MM order");
    const bool day_first = first_over_12;

    std::vector<DatedPrice> out;
    out.reserve(t.rows.size());
    for (const auto& row : t.rows) {
        try {
            if (row.fields.size() <= std::max(c_date, c_price)) throw std::invalid_argument("too few fields");
            const std::string s = trim(row.fields[c_date]);
            Date d{};
            if (s.find('/') != std::string::npos) {
                const auto sd = split_slash(s);
                d = day_first ? make_date(sd.year, sd.b, sd.a, s) : make_date(sd.year, sd.a, sd.b, s);
            } else if (s.size() == 10 && s[4] == '-') {
                d = parse_date(s);
            } else {
                d = parse_month_name_date(s);
            }
            const double price = parse_number(row.fields[c_price]);
            if (price <= 0) throw std::invalid_argument("price must be positive");
            out.push_back({d, price});
        } catch (const std::exception& e) {
            throw std::runtime_error(path.string() + ":" + std::to_string(row.line) + ": " + e.what());
        }
    }

    std::sort(out.begin(), out.end(), [](const DatedPrice& x, const DatedPrice& y) {
        return std::chrono::sys_days{x.date} < std::chrono::sys_days{y.date};
    });
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i].date == out[i - 1].date)
            throw std::runtime_error(path.string() + ": date " + format_date(out[i].date) + " appears twice");
    return out;
}

std::vector<IndexLevel> to_index_levels(const std::vector<DatedPrice>& prices) {
    std::vector<IndexLevel> out;
    out.reserve(prices.size());
    for (const auto& p : prices) out.push_back({p.date, p.price});
    return out;
}

std::vector<FxRate> to_fx_rates(const std::vector<DatedPrice>& prices) {
    std::vector<FxRate> out;
    out.reserve(prices.size());
    for (const auto& p : prices) out.push_back({p.date, p.price});
    return out;
}

MergeResult<IndexLevel> merge_by_date(const std::vector<IndexLevel>& base, const std::vector<IndexLevel>& update) {
    return merge_impl(base, update, [](const IndexLevel& a, const IndexLevel& b) { return a.close == b.close; });
}

MergeResult<FxRate> merge_by_date(const std::vector<FxRate>& base, const std::vector<FxRate>& update) {
    return merge_impl(base, update, [](const FxRate& a, const FxRate& b) { return a.rate == b.rate; });
}

namespace {

std::string tbill_key(const TbillYield& t) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%05d", t.tenor_days);
    return format_date(t.date) + "|" + buf;
}

std::string lower_trim(std::string_view s) {
    std::string out = trim(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

/// Excel serial number or dd/MM/yyyy text.
Date sheet_date(const std::string& cell) {
    const std::string s = trim(cell);
    if (s.find('/') != std::string::npos) {
        const auto sd = split_slash(s);
        return make_date(sd.year, sd.b, sd.a, s);
    }
    if (s.size() == 10 && s[4] == '-') return parse_date(s);
    return xlsx::excel_serial_to_date(parse_number(s));
}

const std::string& cell_at(const xlsx::SheetRow& row, std::size_t col) {
    static const std::string empty;
    return col < row.cells.size() ? row.cells[col] : empty;
}

}  // namespace

MergeResult<TbillYield> merge_by_date(const std::vector<TbillYield>& base, const std::vector<TbillYield>& update) {
    std::map<std::string, TbillYield> by_key;
    for (const auto& r : base) by_key.insert_or_assign(tbill_key(r), r);
    MergeResult<TbillYield> out;
    for (const auto& r : update) {
        const auto key = tbill_key(r);
        const auto it = by_key.find(key);
        if (it == by_key.end()) ++out.added;
        else if (it->second.yield_pct != r.yield_pct) ++out.changed;
        by_key.insert_or_assign(key, r);
    }
    for (auto& [key, row] : by_key) out.rows.push_back(row);
    return out;
}

TbillImport read_tbill_auctions_xlsx(const std::filesystem::path& path) {
    const auto sheets = xlsx::read_workbook(path);
    TbillImport out;
    std::map<std::string, std::string> seen;  // key -> "sheet!row" for duplicate reporting

    for (const auto& sheet : sheets) {
        // Locate the header row.
        std::size_t header_idx = 0, c_tenor = csv::Table::npos, c_issue = csv::Table::npos,
                    c_maturity = csv::Table::npos, c_yield = csv::Table::npos;
        bool found = false;
        for (std::size_t r = 0; r < sheet.rows.size() && !found; ++r) {
            c_tenor = c_issue = c_maturity = c_yield = csv::Table::npos;
            const auto& cells = sheet.rows[r].cells;
            for (std::size_t c = 0; c < cells.size(); ++c) {
                const std::string h = lower_trim(cells[c]);
                if (h.rfind("tenor", 0) == 0) c_tenor = c;
                else if (h == "issue date") c_issue = c;
                else if (h == "maturity date") c_maturity = c;
                else if (h.rfind("weighted avg", 0) == 0 || h.rfind("weighted average", 0) == 0) c_yield = c;
            }
            if (c_tenor != csv::Table::npos && c_issue != csv::Table::npos && c_yield != csv::Table::npos) {
                header_idx = r;
                found = true;
            }
        }
        if (!found) continue;

        for (std::size_t r = header_idx + 1; r < sheet.rows.size(); ++r) {
            const auto& row = sheet.rows[r];
            const std::string where = path.filename().string() + " [" + sheet.name + "] row " + std::to_string(row.number);
            try {
                const std::string tenor_s = trim(cell_at(row, c_tenor));
                const std::string issue_s = trim(cell_at(row, c_issue));
                const std::string yield_s = trim(cell_at(row, c_yield));
                if (tenor_s.empty() && issue_s.empty()) continue;  // blank row
                if (yield_s.empty() || yield_s == "-") {
                    ++out.skipped;
                    continue;
                }
                const double tenor_d = parse_number(tenor_s);
                if (tenor_d <= 0 || tenor_d > 3650 || tenor_d != std::floor(tenor_d))
                    throw std::invalid_argument("bad tenor '" + tenor_s + "'");
                const int tenor = static_cast<int>(tenor_d);
                const Date issue = sheet_date(issue_s);
                if (c_maturity != csv::Table::npos && !trim(cell_at(row, c_maturity)).empty()) {
                    const int span = days_between(issue, sheet_date(cell_at(row, c_maturity)));
                    if (span < tenor - 7 || span > tenor + 7)
                        throw std::invalid_argument("maturity is " + std::to_string(span) + " days after issue, but tenor is " +
                                                    std::to_string(tenor));
                }
                const double y = parse_number(yield_s);
                if (y <= 0 || y >= 100) throw std::invalid_argument("yield out of range: " + yield_s);

                TbillYield t{issue, tenor, y};
                const auto [it, inserted] = seen.emplace(tbill_key(t), where);
                if (!inserted)
                    throw std::invalid_argument("duplicate " + std::to_string(tenor) + "-day auction on " +
                                                format_date(issue) + " (also at " + it->second + ")");
                out.rows.push_back(t);
            } catch (const std::exception& e) {
                throw std::runtime_error(where + ": " + e.what());
            }
        }
    }
    if (out.rows.empty())
        throw std::runtime_error(path.string() +
                                 ": no T-bill auction rows found (need 'Tenor', 'Issue Date' and "
                                 "'Weighted Avg. Yield' columns)");

    std::sort(out.rows.begin(), out.rows.end(), [](const TbillYield& a, const TbillYield& b) {
        return tbill_key(a) < tbill_key(b);
    });
    for (const auto& r : out.rows)
        if (out.tenors.empty() || std::find(out.tenors.begin(), out.tenors.end(), r.tenor_days) == out.tenors.end())
            out.tenors.push_back(r.tenor_days);
    std::sort(out.tenors.begin(), out.tenors.end());
    return out;
}

// ---- futures ---------------------------------------------------------------

namespace {

std::string futures_key(const FuturesSettlement& f) {
    return format_date(f.date) + "|" + format_date(f.expiry) + "|" + f.contract;
}

// Bulletin header keywords. Arabic strings are spelled as UTF-8 bytes so the
// source compiles the same under any compiler code page.
constexpr const char* kArPrevious = "\xd8\xa7\xd9\x84\xd8\xb3\xd8\xa7\xd8\xa8\xd9\x82";  // "previous"
constexpr const char* kArSettle =
    "\xd8\xa7\xd9\x84\xd8\xa5\xd9\x82\xd9\x81\xd8\xa7\xd9\x84\x20\xd9\x88\xd8\xa7\xd9\x84\xd8\xaa\xd8\xb3\xd9\x88"
    "\xd9\x8a\xd8\xa9";  // "closing and settlement"
constexpr const char* kArVolume = "\xd8\xad\xd8\xac\xd9\x85\x20\xd8\xa7\xd9\x84\xd8\xaa\xd8\xaf\xd8\xa7\xd9\x88\xd9\x84";
constexpr const char* kArValue = "\xd9\x82\xd9\x8a\xd9\x85\xd8\xa9\x20\xd8\xa7\xd9\x84\xd8\xaa\xd8\xaf\xd8\xa7\xd9\x88\xd9\x84";
constexpr const char* kArContract = "\xd9\x83\xd9\x88\xd8\xaf\x20\xd8\xa7\xd9\x84\xd8\xb9\xd9\x82\xd9\x88\xd8\xaf";

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

struct BulletinColumns {
    std::size_t name = csv::Table::npos, settle = csv::Table::npos, prev_settle = csv::Table::npos,
                volume = csv::Table::npos, value = csv::Table::npos;
    bool ok() const { return name != csv::Table::npos && settle != csv::Table::npos && volume != csv::Table::npos; }
};

BulletinColumns find_bulletin_columns(const std::vector<std::string>& header) {
    BulletinColumns c;
    for (std::size_t i = 0; i < header.size(); ++i) {
        const std::string h = lower_trim(header[i]);
        const bool previous = contains(h, "previous") || contains(h, kArPrevious);
        if (contains(h, "contract code") || contains(h, kArContract)) c.name = i;
        else if ((contains(h, "closing and settlement") || contains(h, kArSettle)) && previous) c.prev_settle = i;
        else if (contains(h, "closing and settlement") || contains(h, kArSettle)) c.settle = i;
        else if (contains(h, "trading volume") || contains(h, kArVolume)) c.volume = i;
        else if (contains(h, "trading value") || contains(h, kArValue)) c.value = i;
    }
    return c;
}

std::optional<double> optional_number(const std::vector<std::string>& row, std::size_t col) {
    if (col == csv::Table::npos || col >= row.size()) return std::nullopt;
    const std::string s = trim(row[col]);
    if (s.empty() || s == "-") return std::nullopt;
    return parse_number(s);
}

}  // namespace

MergeResult<FuturesSettlement> merge_by_date(const std::vector<FuturesSettlement>& base,
                                             const std::vector<FuturesSettlement>& update) {
    // Key on (date, contract); the expiry is in the sort key so rows order by date, then expiry.
    std::map<std::string, std::string> key_of;  // "date|contract" -> sort key
    std::map<std::string, FuturesSettlement> by_key;
    auto id = [](const FuturesSettlement& f) { return format_date(f.date) + "|" + f.contract; };
    for (const auto& r : base) {
        if (auto it = key_of.find(id(r)); it != key_of.end()) by_key.erase(it->second);
        key_of[id(r)] = futures_key(r);
        by_key.insert_or_assign(futures_key(r), r);
    }
    MergeResult<FuturesSettlement> out;
    for (const auto& r : update) {
        const auto it = key_of.find(id(r));
        if (it == key_of.end()) {
            ++out.added;
        } else {
            const auto& old = by_key.at(it->second);
            if (old.settle != r.settle || old.volume != r.volume || old.open_interest != r.open_interest ||
                old.expiry != r.expiry)
                ++out.changed;
            by_key.erase(it->second);
        }
        key_of[id(r)] = futures_key(r);
        by_key.insert_or_assign(futures_key(r), r);
    }
    for (auto& [key, row] : by_key) out.rows.push_back(row);
    return out;
}

FuturesBulletin read_egx_futures_bulletin_html(const std::filesystem::path& path, const Date& trade_date) {
    const auto tables = html::read_tables_file(path);
    const std::string file = path.filename().string();

    // The bulletin table is the one whose header row names the settlement and volume columns.
    const html::Table* table = nullptr;
    std::size_t header_row = 0;
    BulletinColumns cols;
    for (const auto& t : tables) {
        for (std::size_t r = 0; r < t.rows.size() && !table; ++r) {
            if (t.rows[r].size() < 5) continue;  // layout tables wrap the whole bulletin in one cell
            const auto c = find_bulletin_columns(t.rows[r]);
            if (c.ok()) {
                table = &t;
                header_row = r;
                cols = c;
            }
        }
        if (table) break;
    }
    if (!table)
        throw std::runtime_error(file + ": no futures table found (expected a 'closing and settlement price' and a "
                                        "'trading volume' column). Save the Derivatives Market Watch page as HTML.");

    FuturesBulletin out;
    const std::size_t width = table->rows[header_row].size();
    for (std::size_t r = header_row + 1; r < table->rows.size(); ++r) {
        const auto& row = table->rows[r];
        if (row.size() != width) continue;  // group headings and the totals row span fewer cells
        const std::string name = trim(row[cols.name]);
        const auto ym = egx30::parse_contract(name);
        if (!ym) {
            // Another futures contract (it has a settlement price), or the totals row (it does not).
            try {
                if (!name.empty() && optional_number(row, cols.settle)) ++out.other_contracts;
            } catch (const std::exception&) {
            }
            continue;
        }
        try {
            const auto settle = optional_number(row, cols.settle);
            if (!settle) {
                ++out.no_settlement;
                continue;
            }
            if (*settle <= 0) throw std::invalid_argument("settlement price must be positive");

            FuturesSettlement f;
            f.date = trade_date;
            f.contract = egx30::contract_code(*ym);
            f.expiry = egx30::expiry_date(*ym);
            f.settle = *settle;
            if (days_between(trade_date, f.expiry) < 0)
                throw std::invalid_argument("trade date " + format_date(trade_date) + " is after the contract expiry " +
                                            format_date(f.expiry));

            const auto volume = optional_number(row, cols.volume).value_or(0.0);
            if (volume < 0 || volume != std::floor(volume)) throw std::invalid_argument("bad volume");
            f.volume = static_cast<std::int64_t>(volume);

            // Daily price limit is 10% of the previous settlement: anything beyond
            // that is a parsing or formatting problem, not a real price.
            if (const auto prev = optional_number(row, cols.prev_settle); prev && *prev > 0) {
                const double move = *settle / *prev - 1.0;
                if (std::abs(move) > 0.105) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "settlement %.3f is %.1f%% from the previous %.3f, beyond the 10%% daily limit "
                                  "(misplaced decimal point?)",
                                  *settle, move * 100.0, *prev);
                    throw std::invalid_argument(buf);
                }
            }
            // Traded value / volume is the average traded price; it must be near the settlement.
            if (const auto value = optional_number(row, cols.value); value && f.volume > 0) {
                const double avg = *value / (static_cast<double>(f.volume) * egx30::kMultiplier);
                if (std::abs(avg / *settle - 1.0) > 0.105) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "average traded price %.3f (value / volume) is far from the settlement %.3f",
                                  avg, *settle);
                    throw std::invalid_argument(buf);
                }
            }
            out.rows.push_back(std::move(f));
        } catch (const std::exception& e) {
            throw std::runtime_error(file + ": " + name + ": " + e.what());
        }
    }
    if (out.rows.empty() && out.no_settlement == 0)
        throw std::runtime_error(file + ": the futures table has no EGX30 index futures rows");

    std::sort(out.rows.begin(), out.rows.end(), [](const FuturesSettlement& a, const FuturesSettlement& b) {
        return futures_key(a) < futures_key(b);
    });
    for (std::size_t i = 1; i < out.rows.size(); ++i)
        if (out.rows[i].contract == out.rows[i - 1].contract)
            throw std::runtime_error(file + ": contract " + out.rows[i].contract + " appears twice");
    return out;
}

}  // namespace egxq
