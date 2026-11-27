#pragma once

// Small helpers shared by the command-line tools: option parsing and number formatting.

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace egxq::cli {

/// "--key value" options plus bare "--flag" switches named in `switches`.
struct Args {
    std::map<std::string, std::string> opts;
    std::set<std::string> flags;

    bool has(const std::string& k) const { return opts.count(k) > 0; }
    bool flag(const std::string& k) const { return flags.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def) const { return has(k) ? opts.at(k) : def; }
    double number(const std::string& k, double def) const { return has(k) ? std::stod(opts.at(k)) : def; }
};

inline Args parse_args(int argc, char** argv, int first, const std::set<std::string>& switches) {
    Args a;
    for (int i = first; i < argc; ++i) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) != 0) throw std::invalid_argument("bad argument " + key);
        const auto name = key.substr(2);
        if (switches.count(name)) {
            a.flags.insert(name);
            continue;
        }
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
        a.opts[name] = argv[++i];
    }
    return a;
}

inline std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

inline std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

inline std::string pct(double v, int decimals = 2) { return num(v * 100.0, decimals) + "%"; }

inline std::string signed_pct(double v, int decimals = 1) { return (v >= 0 ? "+" : "") + pct(v, decimals); }

/// 1234567.8 -> "1,234,568"; -1234 -> "-1,234"
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

}  // namespace egxq::cli
