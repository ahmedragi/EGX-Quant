#include "egxquant/data/html_table.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "markup_util.hpp"

namespace egxq::html {

namespace {

struct OpenTable {
    std::size_t index;  // into the output vector
    bool in_row = false;
    bool in_cell = false;
    std::string cell;
};

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string collapse_spaces(std::string_view s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

/// End of a tag starting at `lt` ('<'), honouring quoted attribute values.
std::size_t tag_end(std::string_view html, std::size_t lt) {
    char quote = 0;
    for (std::size_t i = lt + 1; i < html.size(); ++i) {
        const char c = html[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return std::string_view::npos;
}

}  // namespace

std::vector<Table> read_tables(std::string_view html) {
    std::vector<Table> tables;
    std::vector<OpenTable> stack;

    auto add_text = [&](std::string_view text) {
        for (auto& t : stack)
            if (t.in_cell) t.cell.append(text);
    };
    auto close_cell = [&](OpenTable& t) {
        if (!t.in_cell) return;
        if (tables[t.index].rows.empty()) tables[t.index].rows.emplace_back();  // cell outside any <tr>
        tables[t.index].rows.back().push_back(collapse_spaces(detail::decode_entities(t.cell)));
        t.cell.clear();
        t.in_cell = false;
    };
    auto close_row = [&](OpenTable& t) {
        close_cell(t);
        t.in_row = false;
    };

    std::size_t pos = 0;
    while (pos < html.size()) {
        const std::size_t lt = html.find('<', pos);
        if (lt == std::string_view::npos) {
            add_text(html.substr(pos));
            break;
        }
        add_text(html.substr(pos, lt - pos));

        if (html.compare(lt, 4, "<!--") == 0) {
            const auto end = html.find("-->", lt + 4);
            pos = end == std::string_view::npos ? html.size() : end + 3;
            continue;
        }
        const std::size_t gt = tag_end(html, lt);
        if (gt == std::string_view::npos) break;
        const std::string_view tag = html.substr(lt, gt - lt + 1);
        pos = gt + 1;

        const bool closing = tag.size() > 1 && tag[1] == '/';
        std::size_t n = closing ? 2 : 1;
        std::string name;
        while (n < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[n])))) name += tag[n++];
        name = lower(name);
        if (name.empty()) continue;

        if (!closing && (name == "script" || name == "style")) {
            // Skip raw text up to the matching close tag.
            const std::string close = "</" + name;
            std::size_t p = pos;
            while (true) {
                p = html.find("</", p);
                if (p == std::string_view::npos) {
                    pos = html.size();
                    break;
                }
                if (lower(html.substr(p, close.size())) == close) {
                    const auto e = html.find('>', p);
                    pos = e == std::string_view::npos ? html.size() : e + 1;
                    break;
                }
                p += 2;
            }
            continue;
        }

        // Table structure separates words: "x<table><tr><td>a</td><td>b</td>" reads "x a b".
        if (name == "table" || name == "tr" || name == "td" || name == "th") add_text(" ");

        if (name == "table") {
            if (!closing) {
                tables.push_back(Table{detail::tag_attr(tag, "id"), {}});
                stack.push_back(OpenTable{tables.size() - 1});
            } else if (!stack.empty()) {
                close_row(stack.back());
                stack.pop_back();
            }
        } else if (stack.empty()) {
            continue;
        } else if (name == "tr") {
            auto& t = stack.back();
            close_row(t);
            if (!closing) {
                tables[t.index].rows.emplace_back();
                t.in_row = true;
            }
        } else if (name == "td" || name == "th") {
            auto& t = stack.back();
            close_cell(t);
            if (!closing) {
                if (!t.in_row) {
                    tables[t.index].rows.emplace_back();
                    t.in_row = true;
                }
                t.in_cell = true;
            }
        } else if (name == "br" || name == "div" || name == "p" || name == "li") {
            add_text(" ");
        }
    }
    while (!stack.empty()) {
        close_row(stack.back());
        stack.pop_back();
    }
    return tables;
}

std::vector<Table> read_tables_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path.string() + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return read_tables(ss.str());
}

}  // namespace egxq::html
