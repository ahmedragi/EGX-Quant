#include "egxquant/futures/contract_spec.hpp"

#include <cctype>
#include <cstdio>
#include <string>

namespace egxq::egx30 {

using namespace std::chrono;

namespace {

constexpr std::array<const char*, 12> kShort = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr std::array<const char*, 12> kLong = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                               "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

std::string upper_alnum_spaces(std::string_view s) {
    std::string out;
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += static_cast<char>(std::toupper(u));
        else if (!out.empty() && out.back() != ' ') out += ' ';  // collapse punctuation / dashes to one space
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::optional<unsigned> month_from_name(std::string_view name, bool allow_long) {
    for (unsigned i = 0; i < 12; ++i) {
        if (name == kShort[i] || (allow_long && name == kLong[i])) return i + 1;
    }
    return std::nullopt;
}

std::optional<int> parse_year(std::string_view digits) {
    if (digits.size() != 2 && digits.size() != 4) return std::nullopt;
    int y = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        y = y * 10 + (c - '0');
    }
    return digits.size() == 2 ? 2000 + y : y;
}

}  // namespace

Date third_wednesday(const year_month& ym) {
    return Date{sys_days{year_month_weekday{ym.year(), ym.month(), weekday_indexed{Wednesday, 3}}}};
}

Date expiry_date(const year_month& ym) { return third_wednesday(ym); }

bool is_quarterly_month(const month& m) { return static_cast<unsigned>(m) % 3 == 0; }

std::string contract_code(const year_month& ym) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "EGX30F%s%02d", kShort[static_cast<unsigned>(ym.month()) - 1],
                  static_cast<int>(ym.year()) % 100);
    return buf;
}

std::optional<year_month> parse_contract(std::string_view text) {
    const std::string s = upper_alnum_spaces(text);
    std::optional<unsigned> mon;
    std::optional<int> yr;

    if (s.rfind("EGX30F", 0) == 0 && s.size() == 11) {
        // Code: EGX30FDEC26
        mon = month_from_name(std::string_view(s).substr(6, 3), false);
        yr = parse_year(std::string_view(s).substr(9, 2));
    } else if (s.rfind("EGX30 INDEX FUTURES ", 0) == 0) {
        // Name: "EGX30 INDEX FUTURES DECEMBER 2026" (also "DEC 2026", "DECEMBER2026")
        std::string rest = s.substr(20);
        std::string name, digits;
        for (char c : rest) {
            if (std::isalpha(static_cast<unsigned char>(c))) name += c;
            else if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        }
        mon = month_from_name(name, true);
        yr = parse_year(digits);
    }
    if (!mon || !yr) return std::nullopt;
    const year_month ym{year{*yr}, month{*mon}};
    return ym.ok() ? std::optional<year_month>{ym} : std::nullopt;
}

std::array<year_month, 2> listed_contracts(const Date& d) {
    year_month ym = d.year() / d.month();
    while (!is_quarterly_month(ym.month()) || sys_days{expiry_date(ym)} < sys_days{d}) ym += months{1};
    return {ym, ym + months{3}};
}

}  // namespace egxq::egx30
