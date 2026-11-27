#pragma once

#include <string>
#include <string_view>
#include <vector>

/// Self-contained SVG time-series charts for the HTML dashboard: no scripts or
/// fonts from the network. Colours are CSS custom properties (--series-1..3,
/// --critical, --grid, --axis, --ink-muted) defined by the page, so the page's
/// light and dark themes apply. Each chart carries its points in a data
/// attribute for the page's hover script (crosshair and tooltip).

namespace egxq::report {

/// HTML/XML escaping of &, <, >, " and '.
std::string escape(std::string_view s);

struct Point {
    double x = 0.0;  ///< Days since 1970-01-01 when the chart has a date axis
    double y = 0.0;
    std::string label;  ///< Tooltip text for dots (optional)
};

enum class Mark { Line, Dots };

struct Series {
    std::string name;
    std::vector<Point> points;
    Mark mark = Mark::Line;
    std::string color = "--series-1";  ///< CSS variable name
};

struct ChartOptions {
    std::string title;
    std::string subtitle;
    int width = 760;
    int height = 260;
    int y_decimals = 0;
    std::string y_suffix;  ///< e.g. "%"
    bool zero_line = false;  ///< Emphasise y = 0
};

/// "Nice" axis ticks (1, 2, 2.5, 5 x 10^k steps) covering [lo, hi].
std::vector<double> nice_ticks(double lo, double hi, int target = 5);

/// Month-aligned date ticks for a range of days since the epoch, at most ~7.
std::vector<double> date_ticks(double first_day, double last_day);

/// A figure: title, legend (for two or more series) and the SVG plot.
/// Series with no points are skipped; with no points at all a placeholder is drawn.
std::string time_chart(const std::vector<Series>& series, const ChartOptions& opt);

/// Days since the epoch for a calendar date given as y, m, d.
double day_number(int y, unsigned m, unsigned d);

}  // namespace egxq::report
