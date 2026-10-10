// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/clean.hpp"
#include "openstitch/lettering/lettering.hpp"

namespace openstitch::lettering {

namespace {

// Tolérance d'aplatissement des courbes de glyphes : 15 µm (invisible, ~1/60 de mm).
constexpr double kFlattenToleranceUm = 15.0;

std::int32_t round_um(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

struct Item {
    char32_t cp{0};
    GlyphOutline outline;
    bool space{false};
    double pen{0.0}; // abscisse de la ligne de base du glyphe, en µm (avant alignement)
};

} // namespace

std::string encode_utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

std::vector<char32_t> decode_utf8(const std::string& text) {
    std::vector<char32_t> out;
    const auto n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const auto b = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        char32_t cp = b;
        if (b < 0x80) {
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            len = 2;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            len = 3;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            len = 4;
            cp = b & 0x07;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool valid = i + len <= n;
        for (std::size_t k = 1; valid && k < len; ++k) {
            const auto c = static_cast<unsigned char>(text[i + k]);
            if ((c & 0xC0) != 0x80) {
                valid = false;
            } else {
                cp = (cp << 6) | (c & 0x3F);
            }
        }
        // Surrogates, hors plage et formes non minimales : invalides.
        if (valid && (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || (len == 2 && cp < 0x80) ||
                      (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000))) {
            valid = false;
        }
        if (!valid) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::vector<TextWarning> check_text_size(const document::TextObject& text) {
    std::vector<TextWarning> out;
    const double mm = static_cast<double>(text.cap_height.value) / 1000.0;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", mm);
    std::string shown = buf;
    std::replace(shown.begin(), shown.end(), '.', ',');
    if (text.cap_height < document::kTextMinCapHeight) {
        out.push_back(
            {"texte-trop-petit",
             "Texte de " + shown + " mm : illisible une fois brodé (minimum conseillé 5 mm)."});
    } else if (text.cap_height < document::kTextSatinMinCapHeight &&
               text.fill != document::TextFill::Contour) {
        out.push_back({"texte-trop-petit",
                       "Texte de " + shown +
                           " mm : en dessous de 5 mm le satin est fragile ; agrandissez ou "
                           "choisissez une police à traits épais."});
    }
    return out;
}

double mean_stroke_width_um(const std::vector<geometry::PathSet>& shapes) {
    double area = 0.0;
    double perimeter = 0.0;
    const auto add_perimeter = [&](const geometry::Path& p) {
        const auto& nodes = p.nodes;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& a = nodes[i].pos;
            const auto& b = nodes[(i + 1) % nodes.size()].pos;
            perimeter += length_um(b - a);
        }
    };
    for (const auto& set : shapes) {
        area += geometry::path_set_area_um2(set);
        add_perimeter(set.outer);
        for (const auto& hole : set.holes) {
            add_perimeter(hole);
        }
    }
    return perimeter > 0.0 ? 2.0 * area / perimeter : 0.0;
}

Result<TextLayout> layout_text(const Font& font, const document::TextObject& text) {
    TextLayout layout;
    if (text.cap_height.value <= 0) {
        return fail(ErrorCategory::UserInput, "La hauteur du texte doit être positive.");
    }
    const double cap_um = static_cast<double>(text.cap_height.value);
    const double scale = cap_um / font.cap_height_units(); // µm par unité de police
    const double tol_units = kFlattenToleranceUm / scale;

    // Découpe en lignes ; '\r' ignoré, '\t' = 4 espaces.
    std::vector<std::vector<char32_t>> lines(1);
    for (char32_t cp : decode_utf8(text.text)) {
        if (cp == U'\r') {
            continue;
        }
        if (cp == U'\n') {
            lines.emplace_back();
        } else if (cp == U'\t') {
            lines.back().insert(lines.back().end(), 4, U' ');
        } else {
            lines.back().push_back(cp);
        }
    }
    layout.line_count = static_cast<int>(lines.size());

    std::vector<char32_t> missing;
    const double line_pitch = cap_um * text.line_spacing;
    const double cos_r = std::cos(text.rotation.radians);
    const double sin_r = std::sin(text.rotation.radians);
    double max_width = 0.0;

    // Pass 1 : chasses, crénage et abscisses de pas de chaque ligne.
    struct Line {
        std::vector<Item> items;
        double width{0.0};
        int spaces{0};
    };
    std::vector<Line> placed(lines.size());
    for (std::size_t li = 0; li < lines.size(); ++li) {
        Line& line = placed[li];
        double pen = 0.0;
        double end = 0.0; // bord droit du dernier caractère non blanc
        char32_t prev = 0;
        bool first = true;
        for (char32_t cp : lines[li]) {
            Item item;
            item.cp = cp;
            item.outline = font.outline(cp, tol_units);
            item.space = (cp == U' ' || cp == 0x00A0);
            if (!item.outline.present) {
                if (std::find(missing.begin(), missing.end(), cp) == missing.end()) {
                    missing.push_back(cp);
                }
                // Caractère absent : jamais un carré silencieux -- omis, avec une
                // chasse d'espace pour garder la mise en page lisible.
                item.space = true;
                item.outline.advance = 0.5 * font.cap_height_units();
            }
            if (!first) {
                pen += text.letter_spacing.value;
                if (text.kerning && prev != 0) {
                    pen += font.kerning(prev, cp) * scale;
                }
            }
            item.pen = pen;
            pen += item.outline.advance * scale;
            if (item.space) {
                pen += text.word_spacing.value;
                ++line.spaces;
            } else {
                end = pen;
            }
            line.items.push_back(std::move(item));
            prev = cp;
            first = false;
        }
        line.width = end;
        // Les espaces de fin ne comptent pas dans la largeur (alignement exact).
        while (!line.items.empty() && line.items.back().space) {
            line.items.pop_back();
            --line.spaces;
        }
        line.spaces = std::max(line.spaces, 0);
        max_width = std::max(max_width, line.width);
    }

    // Pass 2 : alignement, justification, rotation, formes en µm absolus.
    for (std::size_t li = 0; li < placed.size(); ++li) {
        Line& line = placed[li];
        double shift = 0.0;
        double extra_per_space = 0.0;
        switch (text.align) {
        case document::TextAlign::Left:
            break;
        case document::TextAlign::Center:
            shift = -line.width / 2.0;
            break;
        case document::TextAlign::Right:
            shift = -line.width;
            break;
        case document::TextAlign::Justify: {
            const double target = static_cast<double>(text.justify_width.value);
            const bool last = (li + 1 == placed.size());
            // Interne : espaces entre mots seulement (ni début ni fin de ligne).
            int inner = 0;
            bool seen_glyph = false;
            for (const Item& it : line.items) {
                if (!it.space) {
                    seen_glyph = true;
                } else if (seen_glyph) {
                    ++inner;
                }
            }
            if (!last && inner > 0 && target > line.width) {
                extra_per_space = (target - line.width) / inner;
            }
            break;
        }
        }
        const double base_y = -static_cast<double>(li) * line_pitch;
        double accumulated = 0.0;
        bool seen_glyph = false;
        for (Item& it : line.items) {
            if (it.space) {
                if (seen_glyph) {
                    accumulated += extra_per_space;
                }
                continue;
            }
            seen_glyph = true;
            const double px = it.pen + accumulated + shift;
            // Contours : polylignes en µm, rotation autour de l'origine du texte.
            std::vector<geometry::Path> raw;
            for (const auto& loop : it.outline.loops) {
                geometry::Path path;
                path.closed = true;
                path.nodes.reserve(loop.size());
                for (const FontPoint& p : loop) {
                    const double x = px + p.x * scale;
                    const double y = base_y + p.y * scale;
                    const double rx =
                        x * cos_r - y * sin_r + static_cast<double>(text.origin.x.value);
                    const double ry =
                        x * sin_r + y * cos_r + static_cast<double>(text.origin.y.value);
                    geometry::PathNode node;
                    node.pos = Vec2um{Micrometers{round_um(rx)}, Micrometers{round_um(ry)}};
                    path.nodes.push_back(node);
                }
                raw.push_back(std::move(path));
            }
            PlacedGlyph glyph;
            glyph.code_point = it.cp;
            glyph.line = static_cast<int>(li);
            const double gx = px;
            glyph.pen = Vec2um{Micrometers{round_um(gx * cos_r - base_y * sin_r +
                                                    static_cast<double>(text.origin.x.value))},
                               Micrometers{round_um(gx * sin_r + base_y * cos_r +
                                                    static_cast<double>(text.origin.y.value))}};
            auto sets = geometry::union_nonzero(raw);
            if (!sets) {
                return std::unexpected(sets.error());
            }
            glyph.shapes = std::move(*sets);
            if (!glyph.shapes.empty()) {
                layout.glyphs.push_back(std::move(glyph));
            }
        }
    }

    layout.width = Micrometers{round_um(max_width)};
    layout.height =
        Micrometers{round_um(cap_um + line_pitch * static_cast<double>(lines.size() - 1))};
    for (char32_t cp : missing) {
        layout.warnings.push_back(
            {"glyphe-absent", "Le caractère « " + encode_utf8(cp) +
                                  " » n'existe pas dans cette police : il est omis."});
    }
    return layout;
}

} // namespace openstitch::lettering
