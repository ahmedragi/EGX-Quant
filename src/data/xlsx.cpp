#include "egxquant/data/xlsx.hpp"

#include <miniz.h>

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

#include "markup_util.hpp"

namespace egxq::xlsx {

namespace {

// ---- zip access --------------------------------------------------------------

class Zip {
public:
    explicit Zip(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open '" + path.string() + "'");
        data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!mz_zip_reader_init_mem(&zip_, data_.data(), data_.size(), 0))
            throw std::runtime_error("'" + path.string() + "' is not an .xlsx workbook (not a zip archive)");
    }
    ~Zip() { mz_zip_reader_end(&zip_); }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    bool has(const std::string& name) { return mz_zip_reader_locate_file(&zip_, name.c_str(), nullptr, 0) >= 0; }

    std::string read(const std::string& name) {
        std::size_t size = 0;
        void* p = mz_zip_reader_extract_file_to_heap(&zip_, name.c_str(), &size, 0);
        if (!p) throw std::runtime_error("xlsx: missing or unreadable part '" + name + "'");
        std::string out(static_cast<const char*>(p), size);
        mz_free(p);
        return out;
    }

private:
    std::string data_;
    mz_zip_archive zip_{};
};

// ---- tiny XML helpers (enough for SpreadsheetML) -----------------------------

using detail::decode_entities;

std::string attr(std::string_view tag, std::string_view name) { return detail::tag_attr(tag, name); }

/// Iterate elements named `name` in `xml`. For each, call fn(open_tag, inner),
/// where inner is empty for a self-closing element.
template <typename Fn>
void for_each_element(std::string_view xml, std::string_view name, Fn&& fn) {
    const std::string open = "<" + std::string(name);
    const std::string close = "</" + std::string(name) + ">";
    std::size_t pos = 0;
    while ((pos = xml.find(open, pos)) != std::string_view::npos) {
        const std::size_t after = pos + open.size();
        if (after >= xml.size()) break;
        const char c = xml[after];
        if (c != ' ' && c != '>' && c != '/' && c != '\t' && c != '\r' && c != '\n') {
            pos = after;  // a longer tag name, e.g. <cols> when looking for <c>
            continue;
        }
        const auto tag_end = xml.find('>', after);
        if (tag_end == std::string_view::npos) break;
        const std::string_view tag = xml.substr(pos, tag_end - pos + 1);
        if (tag.size() >= 2 && tag[tag.size() - 2] == '/') {
            fn(tag, std::string_view{});
            pos = tag_end + 1;
            continue;
        }
        const auto end = xml.find(close, tag_end + 1);
        if (end == std::string_view::npos) break;
        fn(tag, xml.substr(tag_end + 1, end - tag_end - 1));
        pos = end + close.size();
    }
}

/// Concatenated text of all <t> elements, skipping phonetic runs (<rPh>).
std::string text_of(std::string_view inner) {
    std::string cleaned;
    std::size_t pos = 0;
    while (true) {
        const auto ph = inner.find("<rPh", pos);
        if (ph == std::string_view::npos) {
            cleaned.append(inner.substr(pos));
            break;
        }
        cleaned.append(inner.substr(pos, ph - pos));
        const auto ph_end = inner.find("</rPh>", ph);
        if (ph_end == std::string_view::npos) break;
        pos = ph_end + 6;
    }
    std::string out;
    for_each_element(cleaned, "t", [&](std::string_view, std::string_view t) { out += decode_entities(t); });
    return out;
}

std::string first_child_text(std::string_view inner, std::string_view name) {
    std::string out;
    bool found = false;
    for_each_element(inner, name, [&](std::string_view, std::string_view v) {
        if (!found) {
            out = decode_entities(v);
            found = true;
        }
    });
    return out;
}

std::string resolve_target(const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    return "xl/" + target;
}

}  // namespace

std::size_t column_index(std::string_view ref) {
    std::size_t col = 0, letters = 0;
    for (char c : ref) {
        if (c >= 'A' && c <= 'Z') col = col * 26 + static_cast<std::size_t>(c - 'A' + 1);
        else if (c >= 'a' && c <= 'z') col = col * 26 + static_cast<std::size_t>(c - 'a' + 1);
        else break;
        ++letters;
    }
    if (letters == 0) throw std::invalid_argument("bad cell reference '" + std::string(ref) + "'");
    return col - 1;
}

Date excel_serial_to_date(double serial) {
    if (!std::isfinite(serial) || serial < 61.0 || serial > 2958465.0)
        throw std::invalid_argument("Excel date serial out of range: " + std::to_string(serial));
    // Serial 1 is 1900-01-01, and Excel counts a non-existent 1900-02-29, so
    // from 1900-03-01 (serial 61) on, serial n is n days after 1899-12-30.
    using namespace std::chrono;
    const auto base = sys_days{year{1899} / December / day{30}};
    return Date{base + days{static_cast<int>(std::floor(serial))}};
}

std::vector<Sheet> read_workbook(const std::filesystem::path& path) {
    Zip zip(path);
    if (!zip.has("xl/workbook.xml"))
        throw std::runtime_error("'" + path.string() + "' is not an .xlsx workbook (no xl/workbook.xml)");

    // Shared strings (optional part).
    std::vector<std::string> shared;
    if (zip.has("xl/sharedStrings.xml")) {
        const std::string xml = zip.read("xl/sharedStrings.xml");
        for_each_element(xml, "si", [&](std::string_view, std::string_view inner) { shared.push_back(text_of(inner)); });
    }

    // Relationship id -> part name.
    std::map<std::string, std::string> rels;
    if (zip.has("xl/_rels/workbook.xml.rels")) {
        const std::string xml = zip.read("xl/_rels/workbook.xml.rels");
        for_each_element(xml, "Relationship", [&](std::string_view tag, std::string_view) {
            rels[attr(tag, "Id")] = resolve_target(attr(tag, "Target"));
        });
    }

    std::vector<Sheet> sheets;
    const std::string wb = zip.read("xl/workbook.xml");
    std::size_t sheet_no = 0;
    for_each_element(wb, "sheet", [&](std::string_view tag, std::string_view) {
        ++sheet_no;
        Sheet sheet;
        sheet.name = attr(tag, "name");
        std::string part;
        if (const auto it = rels.find(attr(tag, "r:id")); it != rels.end()) part = it->second;
        else part = "xl/worksheets/sheet" + std::to_string(sheet_no) + ".xml";
        if (!zip.has(part)) return;  // e.g. a chart sheet

        const std::string xml = zip.read(part);
        std::size_t auto_row = 0;
        for_each_element(xml, "row", [&](std::string_view row_tag, std::string_view row_inner) {
            SheetRow row;
            const std::string r = attr(row_tag, "r");
            row.number = r.empty() ? auto_row + 1 : std::stoul(r);
            auto_row = row.number;
            std::size_t next_col = 0;
            for_each_element(row_inner, "c", [&](std::string_view c_tag, std::string_view c_inner) {
                const std::string ref = attr(c_tag, "r");
                const std::size_t col = ref.empty() ? next_col : column_index(ref);
                next_col = col + 1;
                const std::string type = attr(c_tag, "t");
                std::string value;
                if (type == "s") {
                    const std::string idx = first_child_text(c_inner, "v");
                    if (!idx.empty()) {
                        const auto i = std::stoul(idx);
                        if (i >= shared.size())
                            throw std::runtime_error("xlsx: shared string index out of range in " + sheet.name);
                        value = shared[i];
                    }
                } else if (type == "inlineStr") {
                    value = text_of(c_inner);
                } else {
                    value = first_child_text(c_inner, "v");  // number, bool, str (formula result), error
                }
                if (row.cells.size() <= col) row.cells.resize(col + 1);
                row.cells[col] = std::move(value);
            });
            sheet.rows.push_back(std::move(row));
        });
        sheets.push_back(std::move(sheet));
    });
    return sheets;
}

}  // namespace egxq::xlsx
