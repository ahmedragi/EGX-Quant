#include "markup_util.hpp"

#include <cctype>
#include <stdexcept>

namespace egxq::detail {

namespace {

void append_utf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

std::string decode_entities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const auto semi = s.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else if (!ent.empty() && ent[0] == '#') {
            try {
                const unsigned long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                                             ? std::stoul(std::string(ent.substr(2)), nullptr, 16)
                                             : std::stoul(std::string(ent.substr(1)));
                append_utf8(out, cp);
            } catch (const std::exception&) {
                out.append(s.substr(i, semi - i + 1));
            }
        } else {
            out.append(s.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

std::string tag_attr(std::string_view tag, std::string_view name) {
    std::size_t pos = 0;
    while ((pos = tag.find(name, pos)) != std::string_view::npos) {
        const bool starts = pos > 0 && std::isspace(static_cast<unsigned char>(tag[pos - 1]));
        const std::size_t after = pos + name.size();
        if (starts && after + 1 < tag.size() && tag[after] == '=' && (tag[after + 1] == '"' || tag[after + 1] == '\'')) {
            const char q = tag[after + 1];
            const auto end = tag.find(q, after + 2);
            if (end == std::string_view::npos) return {};
            return decode_entities(tag.substr(after + 2, end - after - 2));
        }
        pos = after;
    }
    return {};
}

}  // namespace egxq::detail
