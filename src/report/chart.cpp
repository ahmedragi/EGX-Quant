#include "egxquant/report/chart.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

#include "egxquant/core/format.hpp"

namespace egxq::report {

namespace {

constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::chrono::year_month_day ymd_of(double day) {
    return std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{std::llround(day)}}};
}

std::string iso_of(double day) {
    const auto d = ymd_of(day);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(d.year()), static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

std::string month_label(double day, bool year_only) {
    const auto d = ymd_of(day);
    const auto y = std::to_string(static_cast<int>(d.year()));
    return year_only ? y : std::string(kMonths[static_cast<unsigned>(d.month()) - 1]) + " " + y;
}

std::string day_label(double day) {
    const auto d = ymd_of(day);
    return std::to_string(static_cast<unsigned>(d.day())) + " " + kMonths[static_cast<unsigned>(d.month()) - 1] + " " +
           std::to_string(static_cast<int>(d.year()));
}

std::string fmt_y(double v, const ChartOptions& o) { return fmt::grouped(v, o.y_decimals) + o.y_suffix; }

std::string px(double v) { return fmt::num(v, 1); }

/// JSON string literal, safe inside <script> (no "</").
std::string json_str(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '<': out += "\\u003c"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

}  // namespace

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

double day_number(int y, unsigned m, unsigned d) {
    const std::chrono::sys_days s{std::chrono::year{y} / std::chrono::month{m} / std::chrono::day{d}};
    return static_cast<double>(s.time_since_epoch().count());
}

std::vector<double> nice_ticks(double lo, double hi, int target) {
    if (!(std::isfinite(lo) && std::isfinite(hi))) return {};
    if (hi < lo) std::swap(lo, hi);
    if (hi - lo <= 0.0) {
        const double pad = lo == 0.0 ? 1.0 : std::fabs(lo) * 0.05;
        lo -= pad;
        hi += pad;
    }
    const double raw = (hi - lo) / std::max(1, target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = 10.0 * mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * mag >= raw * (1.0 - 1e-9)) {
            step = m * mag;
            break;
        }
    const double start = std::floor(lo / step + 1e-9) * step;
    const double end = std::ceil(hi / step - 1e-9) * step;
    std::vector<double> ticks;
    for (int i = 0; start + i * step <= end + step * 1e-6 && i < 100; ++i) {
        double t = start + i * step;
        if (std::fabs(t) < step * 1e-9) t = 0.0;
        ticks.push_back(t);
    }
    return ticks;
}

std::vector<double> date_ticks(double first_day, double last_day) {
    if (last_day < first_day) std::swap(first_day, last_day);
    const double months = (last_day - first_day) / 30.44;
    int step = 60;
    for (int s : {1, 2, 3, 6, 12, 24, 60})
        if (months / s <= 7.0) {
            step = s;
            break;
        }
    const auto d0 = ymd_of(first_day);
    int idx = static_cast<int>(d0.year()) * 12 + static_cast<int>(static_cast<unsigned>(d0.month())) - 1;
    if (static_cast<unsigned>(d0.day()) != 1) ++idx;  // first whole month
    while (idx % step != 0) ++idx;
    std::vector<double> ticks;
    for (; ticks.size() < 100; idx += step) {
        const double t = day_number(idx / 12, static_cast<unsigned>(idx % 12 + 1), 1);
        if (t > last_day) break;
        ticks.push_back(t);
    }
    return ticks;
}

std::string time_chart(const std::vector<Series>& series, const ChartOptions& opt) {
    const double W = opt.width, H = opt.height;
    const double left = 64, right = 16, top = 12, bottom = 28;
    const double pw = W - left - right, ph = H - top - bottom;

    double xmin = std::numeric_limits<double>::infinity(), xmax = -xmin, ymin = xmin, ymax = -xmin;
    std::size_t shown = 0;
    for (const auto& s : series) {
        if (s.points.empty()) continue;
        ++shown;
        for (const auto& p : s.points) {
            xmin = std::min(xmin, p.x);
            xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y);
            ymax = std::max(ymax, p.y);
        }
    }

    std::string out = "<figure class=\"chart\">\n<figcaption><strong>" + escape(opt.title) + "</strong>";
    if (!opt.subtitle.empty()) out += "<span>" + escape(opt.subtitle) + "</span>";
    out += "</figcaption>\n";
    if (shown == 0) {
        return out + "<div class=\"empty\">No data yet</div>\n</figure>\n";
    }
    if (shown >= 2) {
        out += "<div class=\"legend\">";
        for (const auto& s : series)
            if (!s.points.empty())
                out += "<span><i class=\"" + std::string(s.mark == Mark::Dots ? "dot" : "line") +
                       "\" style=\"background:var(" + s.color + ")\"></i>" + escape(s.name) + "</span>";
        out += "</div>\n";
    }

    if (opt.zero_line) {
        ymin = std::min(ymin, 0.0);
        ymax = std::max(ymax, 0.0);
    }
    const auto yt = nice_ticks(ymin, ymax, 5);
    const double y0 = yt.front(), y1 = yt.back();
    bool day_axis = false;
    if (xmax - xmin < 1.0) {  // a single date: centre it in a two-week window
        xmin -= 7.0;
        xmax += 7.0;
        day_axis = true;
    }
    auto X = [&](double x) { return left + (x - xmin) / (xmax - xmin) * pw; };
    auto Y = [&](double y) { return top + (y1 - y) / (y1 - y0) * ph; };

    out += "<svg viewBox=\"0 0 " + px(W) + " " + px(H) + "\" role=\"img\" aria-label=\"" + escape(opt.title) +
           "\" preserveAspectRatio=\"xMidYMid meet\">\n";
    // Grid and y labels.
    for (double t : yt) {
        const bool zero = opt.zero_line && t == 0.0;
        out += "<line class=\"" + std::string(zero ? "axis" : "grid") + "\" x1=\"" + px(left) + "\" x2=\"" +
               px(W - right) + "\" y1=\"" + px(Y(t)) + "\" y2=\"" + px(Y(t)) + "\"/>";
        out += "<text class=\"tick\" x=\"" + px(left - 8) + "\" y=\"" + px(Y(t) + 4) + "\" text-anchor=\"end\">" +
               escape(fmt_y(t, opt)) + "</text>\n";
    }
    // X axis and labels.
    out += "<line class=\"axis\" x1=\"" + px(left) + "\" x2=\"" + px(W - right) + "\" y1=\"" + px(top + ph) +
           "\" y2=\"" + px(top + ph) + "\"/>\n";
    std::vector<std::pair<double, std::string>> xt;
    if (day_axis) {
        xt.emplace_back(xmin + 7.0, day_label(xmin + 7.0));
    } else {
        const auto dt = date_ticks(xmin, xmax);
        const bool year_only = dt.size() >= 2 && dt[1] - dt[0] > 300.0;
        for (double t : dt) xt.emplace_back(t, month_label(t, year_only));
        if (xt.empty()) xt.emplace_back(xmin, day_label(xmin));
    }
    for (const auto& [t, label] : xt)
        out += "<text class=\"tick\" x=\"" + px(X(t)) + "\" y=\"" + px(H - 8) + "\" text-anchor=\"middle\">" +
               escape(label) + "</text>";
    out += "\n";

    // Marks: lines first, dots on top.
    std::string data = "{\"s\":[";
    bool first_series = true;
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& s : series) {
            if (s.points.empty() || (pass == 0) != (s.mark == Mark::Line)) continue;
            if (s.mark == Mark::Line && s.points.size() > 1) {
                out += "<polyline class=\"mark-line\" style=\"stroke:var(" + s.color + ")\" points=\"";
                for (const auto& p : s.points) out += px(X(p.x)) + "," + px(Y(p.y)) + " ";
                out += "\"/>\n";
            } else {
                for (const auto& p : s.points) {
                    out += "<circle class=\"mark-dot\" style=\"fill:var(" + s.color + ")\" cx=\"" + px(X(p.x)) +
                           "\" cy=\"" + px(Y(p.y)) + "\" r=\"4\"/>";
                }
                out += "\n";
            }
            data += std::string(first_series ? "" : ",") + "{\"n\":" + json_str(s.name) + ",\"c\":" + json_str(s.color) +
                    ",\"d\":" + (s.mark == Mark::Dots || s.points.size() == 1 ? "1" : "0") + ",\"p\":[";
            first_series = false;
            for (std::size_t i = 0; i < s.points.size(); ++i) {
                const auto& p = s.points[i];
                const std::string label = p.label.empty() ? iso_of(p.x) + "  " + fmt_y(p.y, opt) : p.label;
                data += std::string(i ? "," : "") + "[" + px(X(p.x)) + "," + px(Y(p.y)) + "," + json_str(label) + "]";
            }
            data += "]}";
        }
    data += "],\"w\":" + px(W) + ",\"t\":" + px(top) + ",\"b\":" + px(top + ph) + "}";
    out += "<line class=\"crosshair\" x1=\"0\" x2=\"0\" y1=\"" + px(top) + "\" y2=\"" + px(top + ph) + "\"/>\n";
    out += "</svg>\n<div class=\"tooltip\" hidden></div>\n<script type=\"application/json\">" + data + "</script>\n";
    return out + "</figure>\n";
}

}  // namespace egxq::report
