#include "egxquant/data/csv.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace egxq::csv {

namespace {

std::string normalize(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool is_blank(const std::vector<std::string>& fields) {
    return fields.size() == 1 && fields[0].empty();
}

}  // namespace

std::size_t Table::find_column(std::string_view name) const {
    const std::string want = normalize(name);
    for (std::size_t i = 0; i < header.size(); ++i)
        if (normalize(header[i]) == want) return i;
    return npos;
}

std::size_t Table::column(std::string_view name) const {
    const std::size_t i = find_column(name);
    if (i == npos) throw std::runtime_error("missing CSV column '" + std::string(name) + "'");
    return i;
}

Table parse(std::string_view text) {
    // Strip a UTF-8 byte-order mark.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);

    Table table;
    bool have_header = false;

    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    bool field_was_quoted = false;
    std::size_t line = 1;
    std::size_t record_line = 1;

    auto end_field = [&] {
        fields.push_back(std::move(field));
        field.clear();
        field_was_quoted = false;
    };
    auto end_record = [&] {
        end_field();
        if (!is_blank(fields)) {
            if (!have_header) {
                table.header = std::move(fields);
                have_header = true;
            } else {
                table.rows.push_back(Row{record_line, std::move(fields)});
            }
        }
        fields.clear();
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                if (c == '\n') ++line;
                field += c;
            }
            continue;
        }
        switch (c) {
            case '"':
                if (!field.empty() || field_was_quoted)
                    throw std::runtime_error("CSV line " + std::to_string(line) +
                                             ": unexpected quote inside an unquoted field");
                in_quotes = true;
                field_was_quoted = true;
                break;
            case ',':
                end_field();
                break;
            case '\r':
                break;  // tolerate CRLF
            case '\n':
                end_record();
                ++line;
                record_line = line;
                break;
            default:
                field += c;
        }
    }
    if (in_quotes) throw std::runtime_error("CSV: unterminated quoted field starting on line " +
                                            std::to_string(record_line));
    if (!field.empty() || !fields.empty() || field_was_quoted) end_record();

    return table;
}

Table read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path.string() + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

std::string escape(std::string_view field) {
    if (field.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(field);
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

void write_row(std::ostream& out, const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out << ',';
        out << escape(fields[i]);
    }
    out << '\n';
}

}  // namespace egxq::csv
