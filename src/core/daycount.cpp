#include "egxquant/core/daycount.hpp"

#include <charconv>
#include <stdexcept>
#include <string>

namespace egxq {

int days_between(const Date& start, const Date& end) {
    if (!start.ok() || !end.ok()) throw std::invalid_argument("days_between: invalid date");
    const auto d = std::chrono::sys_days{end} - std::chrono::sys_days{start};
    return static_cast<int>(d.count());
}

double year_fraction(int days, DayCount dc) {
    switch (dc) {
        case DayCount::Act365Fixed: return static_cast<double>(days) / 365.0;
        case DayCount::Act360:      return static_cast<double>(days) / 360.0;
    }
    throw std::invalid_argument("year_fraction: unknown day count");
}

double year_fraction(const Date& start, const Date& end, DayCount dc) {
    return year_fraction(days_between(start, end), dc);
}

namespace {
int parse_int(std::string_view s) {
    int v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size())
        throw std::invalid_argument("parse_date: bad number '" + std::string(s) + "'");
    return v;
}
}  // namespace

Date parse_date(std::string_view iso) {
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-')
        throw std::invalid_argument("parse_date: expected YYYY-MM-DD, got '" + std::string(iso) + "'");
    const Date d{std::chrono::year{parse_int(iso.substr(0, 4))},
                 std::chrono::month{static_cast<unsigned>(parse_int(iso.substr(5, 2)))},
                 std::chrono::day{static_cast<unsigned>(parse_int(iso.substr(8, 2)))}};
    if (!d.ok()) throw std::invalid_argument("parse_date: not a valid calendar date '" + std::string(iso) + "'");
    return d;
}

}  // namespace egxq
