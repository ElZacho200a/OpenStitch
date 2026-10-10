// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/chart_import.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <initializer_list>
#include <set>
#include <vector>

namespace openstitch::thread_palette {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Découpe une ligne CSV en champs (guillemets doubles, "" = guillemet).
std::vector<std::string> split_row(std::string_view line, char sep) {
    std::vector<std::string> fields;
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cur.push_back(c);
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == sep) {
            fields.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    fields.push_back(trim(cur));
    return fields;
}

std::optional<int> parse_channel(const std::string& s) {
    int v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size() || v < 0 || v > 255) {
        return std::nullopt;
    }
    return v;
}

int column_of(const std::vector<std::string>& header, std::initializer_list<const char*> names) {
    for (std::size_t i = 0; i < header.size(); ++i) {
        const std::string h = lower(header[i]);
        for (const char* n : names) {
            if (h == n) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::string cell(const std::vector<std::string>& row, int col) {
    return col >= 0 && static_cast<std::size_t>(col) < row.size()
               ? row[static_cast<std::size_t>(col)]
               : std::string();
}

} // namespace

std::optional<std::array<std::uint8_t, 3>> parse_hex_color(std::string_view text) {
    std::string t = trim(text);
    if (!t.empty() && t.front() == '#') {
        t.erase(t.begin());
    }
    if (t.size() != 6) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> rgb{};
    for (std::size_t i = 0; i < 3; ++i) {
        unsigned v = 0;
        const auto [ptr, ec] = std::from_chars(t.data() + 2 * i, t.data() + 2 * i + 2, v, 16);
        if (ec != std::errc{} || ptr != t.data() + 2 * i + 2) {
            return std::nullopt;
        }
        rgb[i] = static_cast<std::uint8_t>(v);
    }
    return rgb;
}

std::string to_hex_color(std::array<std::uint8_t, 3> rgb) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out = "#";
    for (const std::uint8_t c : rgb) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0x0F]);
    }
    return out;
}

std::string make_user_chart_id(std::string_view name) {
    std::string id = "user_";
    bool lastUnderscore = true;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 128 && std::isalnum(u) != 0) {
            id.push_back(static_cast<char>(std::tolower(u)));
            lastUnderscore = false;
        } else if (!lastUnderscore) {
            id.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!id.empty() && id.back() == '_') {
        id.pop_back();
    }
    if (id == "user") {
        id = "user_nuancier";
    }
    return id;
}

Result<ThreadChart> parse_chart_csv(std::string_view text, const ChartImportInfo& info) {
    if (info.chart_id.empty()) {
        return fail(ErrorCategory::UserInput, "Identifiant de nuancier vide");
    }
    std::vector<std::string_view> lines;
    for (std::size_t pos = 0; pos <= text.size();) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        pos = end + 1;
    }
    if (!lines.empty() && lines.front().starts_with("\xEF\xBB\xBF")) {
        lines.front().remove_prefix(3); // BOM UTF-8
    }

    ThreadChart chart;
    chart.chart_id = info.chart_id;
    chart.display_name = info.display_name.empty() ? info.chart_id : info.display_name;
    chart.source_note = info.source_note;

    char sep = ',';
    bool haveHeader = false;
    int cCode = -1;
    int cName = -1;
    int cHex = -1;
    int cR = -1;
    int cG = -1;
    int cB = -1;
    int cBrand = -1;
    int cRange = -1;
    std::set<std::string> seen;

    for (std::size_t n = 0; n < lines.size(); ++n) {
        const std::string trimmed = trim(lines[n]);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        const std::string where = " (ligne " + std::to_string(n + 1) + ")";
        if (!haveHeader) {
            sep = std::count(trimmed.begin(), trimmed.end(), ';') >
                          std::count(trimmed.begin(), trimmed.end(), ',')
                      ? ';'
                      : ',';
            const auto header = split_row(trimmed, sep);
            cCode = column_of(header, {"code", "reference", "ref"});
            cName = column_of(header, {"name", "nom"});
            cHex = column_of(header, {"hex", "color", "couleur"});
            cR = column_of(header, {"r", "red", "rouge"});
            cG = column_of(header, {"g", "green", "vert"});
            cB = column_of(header, {"b", "blue", "bleu"});
            cBrand = column_of(header, {"brand", "marque"});
            cRange = column_of(header, {"range", "gamme"});
            const bool haveColor = cHex >= 0 || (cR >= 0 && cG >= 0 && cB >= 0);
            if (cCode < 0 || cName < 0 || !haveColor) {
                return fail(ErrorCategory::InvalidFile,
                            "En-tête CSV invalide" + where +
                                " : colonnes attendues « code, name, hex » ou « code, name, r, "
                                "g, b »");
            }
            haveHeader = true;
            continue;
        }
        const auto row = split_row(trimmed, sep);
        Thread t;
        t.key.chart_id = chart.chart_id;
        t.key.code = cell(row, cCode);
        t.name = cell(row, cName);
        t.brand = cBrand >= 0 ? cell(row, cBrand) : chart.display_name;
        t.range = cell(row, cRange);
        if (t.key.code.empty()) {
            return fail(ErrorCategory::InvalidFile, "Code de fil vide" + where);
        }
        if (cHex >= 0) {
            const auto rgb = parse_hex_color(cell(row, cHex));
            if (!rgb) {
                return fail(ErrorCategory::InvalidFile,
                            "Couleur illisible « " + cell(row, cHex) + " »" + where);
            }
            t.rgb = *rgb;
        } else {
            const auto r = parse_channel(cell(row, cR));
            const auto g = parse_channel(cell(row, cG));
            const auto b = parse_channel(cell(row, cB));
            if (!r || !g || !b) {
                return fail(ErrorCategory::InvalidFile,
                            "Composantes r, g, b invalides (0 à 255)" + where);
            }
            t.rgb = {static_cast<std::uint8_t>(*r), static_cast<std::uint8_t>(*g),
                     static_cast<std::uint8_t>(*b)};
        }
        if (!seen.insert(t.key.code).second) {
            return fail(ErrorCategory::InvalidFile,
                        "Code en double « " + t.key.code + " »" + where);
        }
        chart.threads.push_back(std::move(t));
    }
    if (!haveHeader) {
        return fail(ErrorCategory::InvalidFile, "Fichier vide : en-tête CSV manquant");
    }
    if (chart.threads.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le nuancier ne contient aucun fil");
    }
    return chart;
}

} // namespace openstitch::thread_palette
