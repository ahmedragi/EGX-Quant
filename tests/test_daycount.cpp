#include <gtest/gtest.h>

#include <stdexcept>

#include "egxquant/core/daycount.hpp"

using namespace egxq;

TEST(DayCount, ParseDate) {
    const Date d = parse_date("2026-03-01");
    EXPECT_EQ(static_cast<int>(d.year()), 2026);
    EXPECT_EQ(static_cast<unsigned>(d.month()), 3u);
    EXPECT_EQ(static_cast<unsigned>(d.day()), 1u);
}

TEST(DayCount, ParseRejectsGarbage) {
    EXPECT_THROW(parse_date("2026/03/01"), std::invalid_argument);
    EXPECT_THROW(parse_date("2026-02-30"), std::invalid_argument);
    EXPECT_THROW(parse_date("20x6-01-01"), std::invalid_argument);
}

TEST(DayCount, DaysBetween) {
    EXPECT_EQ(days_between(parse_date("2026-01-01"), parse_date("2026-12-31")), 364);
    EXPECT_EQ(days_between(parse_date("2028-02-28"), parse_date("2028-03-01")), 2);  // leap year
    EXPECT_EQ(days_between(parse_date("2026-06-01"), parse_date("2026-03-01")), -92);
}

TEST(DayCount, YearFraction) {
    EXPECT_DOUBLE_EQ(year_fraction(365), 1.0);
    EXPECT_DOUBLE_EQ(year_fraction(360, DayCount::Act360), 1.0);
    EXPECT_NEAR(year_fraction(parse_date("2026-03-01"), parse_date("2026-05-31")), 91.0 / 365.0, 1e-15);
}
