#pragma once

#include <cmath>
#include <cstdio>
#include <string>

/// Number formatting shared by the command-line tools and the dashboard.

namespace egxq::fmt {

/// Fixed decimals: num(3.14159, 2) -> "3.14"
inline std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

/// Fraction as a percentage: pct(0.0123) -> "1.23%"
inline std::string pct(double v, int decimals = 2) { return num(v * 100.0, decimals) + "%"; }

/// With a sign: signed_pct(0.05) -> "+5.0%"
inline std::string signed_pct(double v, int decimals = 1) { return (v >= 0 ? "+" : "") + pct(v, decimals); }

/// Rounded with thousands separators: 1234567.8 -> "1,234,568"; -1234 -> "-1,234"
inline std::string money(double v) {
    const bool neg = std::llround(v) < 0;
    const auto digits = std::to_string(static_cast<long long>(std::llround(std::fabs(v))));
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return neg ? "-" + out : out;
}

/// Thousands separators with decimals: grouped(51462.5, 2) -> "51,462.50"
inline std::string grouped(double v, int decimals) {
    long long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const long long total = std::llround(std::fabs(v) * static_cast<double>(scale));
    std::string out = money(static_cast<double>(total / scale));
    if (decimals > 0) {
        std::string frac = std::to_string(total % scale);
        frac.insert(0, static_cast<std::size_t>(decimals) - frac.size(), '0');
        out += "." + frac;
    }
    return (v < 0 && total > 0 ? "-" : "") + out;
}

}  // namespace egxq::fmt
