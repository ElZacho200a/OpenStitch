// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>

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
    bool mark{false};       // marque combinante : se superpose à la lettre précédente
    double pen{0.0};        // abscisse de la ligne de base du glyphe, en µm (avant alignement)
    double center_off{0.0}; // décalage du centre de la chasse par rapport à `pen` (bases courbes)
};

// Base de pose des lettres (HP-TXT-005) : repère local où la ligne de base est l'axe X
// (abscisse curviligne `s`, µm). `at(s)` donne le point de la base et sa tangente unitaire ;
// la normale (côté sommet des lettres) est (-ty, tx). Repère avant rotation du texte.
class BaseCurve {
public:
    struct Frame {
        double x{0.0};
        double y{0.0};
        double tx{1.0};
        double ty{0.0};
    };

    explicit BaseCurve(const document::TextBase& base) : kind_(base.kind), inside_(base.inside) {
        radius_ = std::max(1.0, static_cast<double>(base.radius.value));
        if (kind_ == document::TextBaseKind::Path) {
            double acc = 0.0;
            for (std::size_t i = 0; i < base.path.size(); ++i) {
                const double x = static_cast<double>(base.path[i].x.value);
                const double y = static_cast<double>(base.path[i].y.value);
                if (i > 0) {
                    acc += std::hypot(x - pts_.back().x, y - pts_.back().y);
                }
                pts_.push_back({x, y, acc});
            }
            if (pts_.size() < 2 || acc <= 0.0) {
                kind_ = document::TextBaseKind::Line; // chemin inutilisable : ligne droite
            }
        }
    }

    [[nodiscard]] bool curved() const { return kind_ != document::TextBaseKind::Line; }
    [[nodiscard]] bool is_circle() const { return kind_ == document::TextBaseKind::Circle; }
    [[nodiscard]] double circumference() const { return 2.0 * std::numbers::pi * radius_; }

    [[nodiscard]] Frame at(double s) const {
        Frame f;
        switch (kind_) {
        case document::TextBaseKind::Line:
            f.x = s;
            return f;
        case document::TextBaseKind::Arc:
        case document::TextBaseKind::Circle: {
            const double phi = s / radius_;
            const double sn = std::sin(phi);
            const double cs = std::cos(phi);
            if (!inside_) {
                f = {radius_ * sn, radius_ * cs - radius_, cs, -sn};
            } else {
                f = {radius_ * sn, radius_ - radius_ * cs, cs, sn};
            }
            return f;
        }
        case document::TextBaseKind::Path: {
            // Segment contenant s ; hors bornes : prolongement du premier / dernier segment.
            std::size_t i = 1;
            while (i + 1 < pts_.size() && pts_[i].s < s) {
                ++i;
            }
            const Pt& a = pts_[i - 1];
            const Pt& b = pts_[i];
            const double len = std::max(b.s - a.s, 1e-9);
            f.tx = (b.x - a.x) / len;
            f.ty = (b.y - a.y) / len;
            f.x = a.x + f.tx * (s - a.s);
            f.y = a.y + f.ty * (s - a.s);
            return f;
        }
        }
        return f;
    }

private:
    struct Pt {
        double x;
        double y;
        double s;
    };
    document::TextBaseKind kind_;
    bool inside_{false};
    double radius_{1.0};
    std::vector<Pt> pts_;
};

std::string hex_code(char32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04X", static_cast<unsigned>(cp));
    return buf;
}

// Normalisation d'une ligne avant mise en page : caractères sans rendu retirés,
// composition/décomposition des accents, repli des caractères absents, ligatures,
// puis ordre visuel des passages de droite à gauche.
struct Prepared {
    std::vector<char32_t> cps;
    std::vector<std::string> replaced; // « A par B » (une fois chacun)
    bool arabic{false};
};

void note_replacement(Prepared& out, char32_t from, const std::u32string& to) {
    std::string text = "« " + encode_utf8(from) + " » par « ";
    for (char32_t c : to) {
        text += encode_utf8(c);
    }
    text += " »";
    if (std::find(out.replaced.begin(), out.replaced.end(), text) == out.replaced.end()) {
        out.replaced.push_back(std::move(text));
    }
}

struct Ligature {
    std::size_t len;
    char32_t code;
    char32_t a, b, c;
};
constexpr Ligature kLigatures[] = {{3, 0xFB03, U'f', U'f', U'i'},
                                   {3, 0xFB04, U'f', U'f', U'l'},
                                   {2, 0xFB00, U'f', U'f', 0},
                                   {2, 0xFB01, U'f', U'i', 0},
                                   {2, 0xFB02, U'f', U'l', 0}};

Prepared prepare_line(const Font& font, const std::vector<char32_t>& line,
                      const LayoutOptions& options) {
    Prepared out;
    for (char32_t cp : line) {
        if (is_ignorable(cp)) {
            continue;
        }
        if (is_arabic_script(cp)) {
            out.arabic = true;
        }
        if (options.compose_marks && is_combining_mark(cp) && !out.cps.empty() &&
            !is_combining_mark(out.cps.back()) && out.cps.back() != U' ') {
            const char32_t composed = compose_pair(out.cps.back(), cp);
            if (composed != 0 && font.has_glyph(composed)) {
                out.cps.back() = composed;
                continue;
            }
            out.cps.push_back(cp);
            continue;
        }
        if (!font.has_glyph(cp)) {
            char32_t base = 0;
            char32_t mark = 0;
            if (options.compose_marks && decompose_char(cp, base, mark) && font.has_glyph(base) &&
                font.has_glyph(mark)) {
                out.cps.push_back(base);
                out.cps.push_back(mark);
                continue;
            }
            if (options.fallback_glyphs) {
                const std::u32string seq = fallback_sequence(cp);
                if (!seq.empty() && std::all_of(seq.begin(), seq.end(),
                                                [&](char32_t c) { return font.has_glyph(c); })) {
                    out.cps.insert(out.cps.end(), seq.begin(), seq.end());
                    note_replacement(out, cp, seq);
                    continue;
                }
            }
        }
        out.cps.push_back(cp);
    }
    if (options.ligatures) {
        std::vector<char32_t> merged;
        for (std::size_t i = 0; i < out.cps.size(); ++i) {
            const auto at = [&](std::size_t k) {
                return i + k < out.cps.size() ? out.cps[i + k] : char32_t{0};
            };
            bool done = false;
            for (const Ligature& l : kLigatures) {
                if (at(0) == l.a && at(1) == l.b && (l.len == 2 || at(2) == l.c) &&
                    font.has_glyph(l.code)) {
                    merged.push_back(l.code);
                    i += l.len - 1;
                    done = true;
                    break;
                }
            }
            if (!done) {
                merged.push_back(out.cps[i]);
            }
        }
        out.cps = std::move(merged);
    }
    if (options.bidi) {
        out.cps = visual_order(out.cps);
    }
    return out;
}

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

LayoutOptions layout_options_for(const document::TextObject& text) {
    LayoutOptions options;
    options.kerning_overrides.reserve(text.kerning_pairs.size());
    for (const auto& pair : text.kerning_pairs) {
        options.kerning_overrides.push_back({pair.left, pair.right, pair.adjust});
    }
    options.ligatures = text.ligatures;
    return options;
}

namespace {

// Édition individuelle d'une lettre (HP-TXT-008) : rotation et échelle autour du centre de
// la boîte de la lettre, puis décalage ; couleur propre mémorisée dans le glyphe.
void apply_glyph_edit(const document::TextObject& text, std::uint32_t index, PlacedGlyph& glyph) {
    const auto it = std::find_if(text.glyph_edits.begin(), text.glyph_edits.end(),
                                 [&](const document::GlyphEdit& e) { return e.index == index; });
    if (it == text.glyph_edits.end()) {
        return;
    }
    const document::GlyphEdit& edit = *it;
    glyph.rgb = edit.rgb;
    std::int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    const auto visit = [&](auto&& fn) {
        for (auto& set : glyph.shapes) {
            for (auto& n : set.outer.nodes) {
                fn(n);
            }
            for (auto& hole : set.holes) {
                for (auto& n : hole.nodes) {
                    fn(n);
                }
            }
        }
    };
    visit([&](geometry::PathNode& n) {
        x0 = std::min(x0, n.pos.x.value);
        x1 = std::max(x1, n.pos.x.value);
        y0 = std::min(y0, n.pos.y.value);
        y1 = std::max(y1, n.pos.y.value);
    });
    if (x0 > x1) {
        return;
    }
    const double cx = (static_cast<double>(x0) + x1) / 2.0;
    const double cy = (static_cast<double>(y0) + y1) / 2.0;
    const double s = edit.scale > 0.0 ? edit.scale : 1.0;
    const double co = std::cos(edit.rotation.radians);
    const double si = std::sin(edit.rotation.radians);
    visit([&](geometry::PathNode& n) {
        const double dx = static_cast<double>(n.pos.x.value) - cx;
        const double dy = static_cast<double>(n.pos.y.value) - cy;
        const double rx = cx + s * (dx * co - dy * si) + static_cast<double>(edit.dx.value);
        const double ry = cy + s * (dx * si + dy * co) + static_cast<double>(edit.dy.value);
        n.pos = Vec2um{Micrometers{round_um(rx)}, Micrometers{round_um(ry)}};
    });
    glyph.pen = Vec2um{Micrometers{glyph.pen.x.value + edit.dx.value},
                       Micrometers{glyph.pen.y.value + edit.dy.value}};
}

} // namespace

Result<TextLayout> layout_text(const Font& font, const document::TextObject& text,
                               const LayoutOptions& options) {
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
    std::vector<std::string> replaced;
    bool arabic = false;
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
        double prev_pen = 0.0; // abscisse de la dernière lettre (hors marques)
        double prev_center_off = 0.0;
        bool first = true;
        const Prepared prepared = prepare_line(font, lines[li], options);
        arabic = arabic || prepared.arabic;
        for (const std::string& r : prepared.replaced) {
            if (std::find(replaced.begin(), replaced.end(), r) == replaced.end()) {
                replaced.push_back(r);
            }
        }
        for (char32_t cp : prepared.cps) {
            Item item;
            item.cp = cp;
            item.outline = font.outline(cp, tol_units);
            item.space = (cp == U' ' || cp == 0x00A0);
            item.mark = is_combining_mark(cp) && !first && prev != 0;
            if (!item.outline.present) {
                if (std::find(missing.begin(), missing.end(), cp) == missing.end()) {
                    missing.push_back(cp);
                }
                // Caractère absent : jamais un carré silencieux -- omis, avec une
                // chasse d'espace pour garder la mise en page lisible.
                item.space = !item.mark;
                item.outline.advance = item.mark ? 0.0 : 0.5 * font.cap_height_units();
            }
            if (item.mark) {
                // Marque combinante : sur la lettre précédente, sans chasse propre.
                item.pen = prev_pen;
                item.center_off = prev_center_off;
                line.items.push_back(std::move(item));
                continue;
            }
            if (!first) {
                pen += text.letter_spacing.value;
                if (prev != 0) {
                    const auto manual = std::find_if(
                        options.kerning_overrides.begin(), options.kerning_overrides.end(),
                        [&](const KerningOverride& k) { return k.left == prev && k.right == cp; });
                    if (manual != options.kerning_overrides.end()) {
                        pen += static_cast<double>(manual->adjust.value);
                    } else if (text.kerning) {
                        pen += font.kerning(prev, cp) * scale;
                    }
                }
            }
            item.pen = pen;
            item.center_off = item.outline.advance * scale / 2.0;
            prev_center_off = item.center_off;
            prev_pen = pen;
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
    const BaseCurve curve(text.base);
    int word = 0;
    bool any_glyph = false;
    bool circle_overlap = false;
    for (std::size_t li = 0; li < placed.size(); ++li) {
        Line& line = placed[li];
        if (any_glyph) {
            ++word; // une nouvelle ligne ouvre un nouveau mot
        }
        double shift = 0.0;
        double extra_per_space = 0.0;
        // Cercle complet : lettres réparties sur toute la circonférence (le texte part du
        // sommet, l'alignement ne s'applique pas).
        double circle_k = 1.0;
        if (curve.is_circle() && line.width > 0.0) {
            int glyph_count = 0;
            for (const Item& it : line.items) {
                glyph_count += it.space ? 0 : 1;
            }
            const double gap = line.width / std::max(1, glyph_count);
            circle_k = curve.circumference() / (line.width + gap);
            circle_overlap = circle_overlap || circle_k < 1.0;
        }
        switch (curve.is_circle() ? document::TextAlign::Left : text.align) {
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
                    ++word;
                }
                continue;
            }
            seen_glyph = true;
            const double px = (it.pen + accumulated + shift) * circle_k;
            // Base courbe : la lettre est posée rigidement autour du centre de sa chasse.
            const double center = px + it.center_off;
            const BaseCurve::Frame frame = curve.at(center);
            // Contours : polylignes en µm, rotation autour de l'origine du texte.
            std::vector<geometry::Path> raw;
            for (const auto& loop : it.outline.loops) {
                geometry::Path path;
                path.closed = true;
                path.nodes.reserve(loop.size());
                for (const FontPoint& p : loop) {
                    double x = px + p.x * scale;
                    double y = base_y + p.y * scale;
                    if (curve.curved()) {
                        const double d = x - center;
                        const double bx = frame.x + d * frame.tx - y * frame.ty;
                        const double by = frame.y + d * frame.ty + y * frame.tx;
                        x = bx;
                        y = by;
                    }
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
            double gx = px;
            double gy = base_y;
            if (curve.curved()) {
                gx = frame.x - it.center_off * frame.tx - base_y * frame.ty;
                gy = frame.y - it.center_off * frame.ty + base_y * frame.tx;
            }
            glyph.pen = Vec2um{Micrometers{round_um(gx * cos_r - gy * sin_r +
                                                    static_cast<double>(text.origin.x.value))},
                               Micrometers{round_um(gx * sin_r + gy * cos_r +
                                                    static_cast<double>(text.origin.y.value))}};
            glyph.word = word;
            any_glyph = true;
            auto sets = geometry::union_nonzero(raw);
            if (!sets) {
                return std::unexpected(sets.error());
            }
            glyph.shapes = std::move(*sets);
            if (!glyph.shapes.empty()) {
                apply_glyph_edit(text, static_cast<std::uint32_t>(layout.glyphs.size()), glyph);
                layout.glyphs.push_back(std::move(glyph));
            }
        }
    }

    layout.width = Micrometers{round_um(max_width)};
    layout.height =
        Micrometers{round_um(cap_um + line_pitch * static_cast<double>(lines.size() - 1))};
    if (missing.size() == 1) {
        layout.warnings.push_back(
            {"glyphe-absent", "Le caractère « " + encode_utf8(missing[0]) + " » (" +
                                  hex_code(missing[0]) +
                                  ") n'existe pas dans cette police : il est omis."});
    } else if (missing.size() > 1) {
        std::string list;
        for (char32_t cp : missing) {
            list += (list.empty() ? "" : " ") + encode_utf8(cp);
        }
        layout.warnings.push_back({"glyphe-absent", "Les caractères « " + list +
                                                        " » n'existent pas dans cette police : "
                                                        "ils sont omis."});
    }
    if (!replaced.empty()) {
        std::string list;
        for (const std::string& r : replaced) {
            list += (list.empty() ? "" : ", ") + r;
        }
        layout.warnings.push_back(
            {"glyphe-remplace", "Caractères absents de la police, remplacés : " + list + "."});
    }
    if (circle_overlap) {
        layout.warnings.push_back(
            {"texte-trop-long-cercle",
             "Le texte est plus long que la circonférence du cercle : les lettres se "
             "chevauchent ; augmentez le rayon ou réduisez le texte."});
    }
    if (arabic) {
        layout.warnings.push_back(
            {"ecriture-non-supportee",
             "Écriture arabe : les lettres ne sont pas liées entre elles (façonnage contextuel "
             "non pris en charge) ; les formes isolées sont utilisées."});
    }
    return layout;
}

} // namespace openstitch::lettering
