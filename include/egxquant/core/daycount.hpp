#pragma once

#include <chrono>
#include <string_view>

namespace egxq {

using Date = std::chrono::year_month_day;

enum class DayCount {
    Act365Fixed,  ///< Actual days / 365. Default for EGP money markets.
    Act360,       ///< Actual days / 360.
};

/// Calendar days from start to end (negative if end is before start).
int days_between(const Date& start, const Date& end);

/// Year fraction between two dates under the given convention.
double year_fraction(const Date& start, const Date& end,
                     DayCount dc = DayCount::Act365Fixed);

/// Year fraction from a day count.
double year_fraction(int days, DayCount dc = DayCount::Act365Fixed);

/// Parse an ISO date "YYYY-MM-DD". Throws std::invalid_argument on bad input.
Date parse_date(std::string_view iso);

}  // namespace egxq
