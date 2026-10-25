// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/placement_template.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include <fmt/format.h>

namespace openstitch::stitch_analysis {

namespace {

using stitch::CommandType;

constexpr std::int64_t kMinPaper = 50'000; // 50 mm
constexpr std::int64_t kCross = 5'000;     // demi-bras de la croix de centrage
constexpr std::int64_t kMark = 5'000;      // longueur d'un repère de recollage

struct Rect {
    std::int64_t x0, y0, x1, y1; // coordonnées « feuille » (Y vers le bas), µm
};

struct P64 {
    std::int64_t x, y;
};

// Liang-Barsky en arithmétique double, résultat arrondi au µm.
std::optional<std::pair<P64, P64>> clip(P64 a, P64 b, const Rect& r) {
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {static_cast<double>(a.x - r.x0), static_cast<double>(r.x1 - a.x),
                         static_cast<double>(a.y - r.y0), static_cast<double>(r.y1 - a.y)};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) {
                return std::nullopt;
            }
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) {
            if (t > t1) {
                return std::nullopt;
            }
            t0 = std::max(t0, t);
        } else {
            if (t < t0) {
                return std::nullopt;
            }
            t1 = std::min(t1, t);
        }
    }
    auto at = [&](double t) { return P64{a.x + std::llround(dx * t), a.y + std::llround(dy * t)}; };
    return std::make_pair(at(t0), at(t1));
}

Micrometers um(std::int64_t v) {
    return Micrometers{static_cast<std::int32_t>(v)};
}

} // namespace

PlacementTemplate make_placement_template(const stitch::StitchSequence& sequence,
                                          const PlacementTemplateOptions& options) {
    PlacementTemplate out;
    std::int64_t pw = std::max<std::int64_t>(options.paper_width.value, kMinPaper);
    std::int64_t ph = std::max<std::int64_t>(options.paper_height.value, kMinPaper);
    if (options.landscape) {
        std::swap(pw, ph);
    }
    const std::int64_t margin =
        std::clamp<std::int64_t>(options.margin.value, 0, std::min(pw, ph) / 4);
    const std::int64_t uw = pw - 2 * margin;
    const std::int64_t uh = ph - 2 * margin;
    const std::int64_t overlap =
        std::clamp<std::int64_t>(options.overlap.value, 0, std::min(uw, uh) / 2);
    out.page_width = um(pw);
    out.page_height = um(ph);

    // Boîte englobante des points cousus.
    bool any = false;
    std::int64_t minX = 0;
    std::int64_t maxX = 0;
    std::int64_t minY = 0;
    std::int64_t maxY = 0;
    for (const auto& c : sequence.commands) {
        if (c.type != CommandType::Stitch) {
            continue;
        }
        const std::int64_t x = c.pos.x.value;
        const std::int64_t y = c.pos.y.value;
        if (!any) {
            minX = maxX = x;
            minY = maxY = y;
            any = true;
        }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    const std::int64_t dw = maxX - minX;
    const std::int64_t dh = maxY - minY;
    out.design_width = um(dw);
    out.design_height = um(dh);
    const std::int64_t cx = (minX + maxX) / 2;
    const std::int64_t cy = (minY + maxY) / 2;

    // Segments du motif en repère « feuille » centré (X à droite, Y vers le bas).
    std::vector<std::pair<P64, P64>> segs;
    bool hasPrev = false;
    P64 prev{0, 0};
    for (const auto& c : sequence.commands) {
        if (c.type != CommandType::Stitch && c.type != CommandType::Jump) {
            continue;
        }
        const P64 cur{c.pos.x.value - cx, -(c.pos.y.value - cy)};
        // Pas de segment de longueur nulle (saut puis premier point au même endroit).
        if (c.type == CommandType::Stitch && hasPrev && (prev.x != cur.x || prev.y != cur.y)) {
            segs.emplace_back(prev, cur);
        }
        prev = cur;
        hasPrev = true;
    }

    // Grille de pages centrée sur le centre du motif.
    const std::int64_t stepX = uw - overlap;
    const std::int64_t stepY = uh - overlap;
    auto count = [](std::int64_t span, std::int64_t usable, std::int64_t step) {
        if (span <= usable) {
            return std::int64_t{1};
        }
        return (span - usable + step - 1) / step + 1;
    };
    const std::int64_t cols = count(dw, uw, stepX);
    const std::int64_t rows = count(dh, uh, stepY);
    out.columns = static_cast<std::size_t>(cols);
    out.rows = static_cast<std::size_t>(rows);
    const std::int64_t spanX = cols * uw - (cols - 1) * overlap;
    const std::int64_t spanY = rows * uh - (rows - 1) * overlap;
    const std::int64_t startX = -spanX / 2;
    const std::int64_t startY = -spanY / 2;
    const std::int64_t total = cols * rows;

    for (std::int64_t r = 0; r < rows; ++r) {
        for (std::int64_t c = 0; c < cols; ++c) {
            TemplatePage page;
            page.column = static_cast<std::size_t>(c);
            page.row = static_cast<std::size_t>(r);
            const std::int64_t tx = startX + c * stepX;
            const std::int64_t ty = startY + r * stepY;
            const Rect tile{tx, ty, tx + uw, ty + uh};
            auto toPage = [&](P64 g) {
                return Vec2um{um(margin + g.x - tx), um(margin + g.y - ty)};
            };
            auto add = [&](std::vector<TemplateSegment>& dst, P64 a, P64 b) {
                if (const auto cl = clip(a, b, tile)) {
                    dst.push_back({toPage(cl->first), toPage(cl->second)});
                }
            };
            for (const auto& s : segs) {
                add(page.stitches, s.first, s.second);
            }
            // Axes du motif par le centre, sur toute la zone utile de la page.
            add(page.axes, P64{tile.x0, 0}, P64{tile.x1, 0});
            add(page.axes, P64{0, tile.y0}, P64{0, tile.y1});
            // Croix de centrage.
            add(page.crosshair, P64{-kCross, 0}, P64{kCross, 0});
            add(page.crosshair, P64{0, -kCross}, P64{0, kCross});
            // Repères de recollage : équerres aux quatre angles de la zone utile.
            const std::int64_t mk = std::min({kMark, uw / 2, uh / 2});
            const P64 corners[4] = {{margin, margin},
                                    {margin + uw, margin},
                                    {margin, margin + uh},
                                    {margin + uw, margin + uh}};
            for (int i = 0; i < 4; ++i) {
                const std::int64_t sx = (i % 2 == 0) ? 1 : -1;
                const std::int64_t sy = (i / 2 == 0) ? 1 : -1;
                const P64 k = corners[i];
                page.marks.push_back(
                    {Vec2um{um(k.x), um(k.y)}, Vec2um{um(k.x + sx * mk), um(k.y)}});
                page.marks.push_back(
                    {Vec2um{um(k.x), um(k.y)}, Vec2um{um(k.x), um(k.y + sy * mk)}});
            }
            // Règle de contrôle d'échelle, première page seulement.
            const std::int64_t rl = options.ruler_length.value;
            if (r == 0 && c == 0 && rl > 0 && rl <= uw - 2 * kMark) {
                const std::int64_t y = margin + uh - 8'000;
                const std::int64_t x0 = margin + kMark;
                page.ruler.push_back({Vec2um{um(x0), um(y)}, Vec2um{um(x0 + rl), um(y)}});
                for (std::int64_t t = 0; t <= rl; t += 10'000) {
                    const std::int64_t h = (t % 50'000 == 0) ? 4'000 : 2'000;
                    page.ruler.push_back(
                        {Vec2um{um(x0 + t), um(y)}, Vec2um{um(x0 + t), um(y - h)}});
                }
                page.texts.push_back(
                    {Vec2um{um(x0), um(y + 1'000)},
                     fmt::format("Regle de controle : {} mm - doit mesurer exactement {} mm",
                                 rl / 1000, rl / 1000),
                     7.0});
            }
            const std::int64_t idx = r * cols + c + 1;
            std::string footer = options.title.empty() ? std::string("Gabarit 1:1") : options.title;
            footer += fmt::format(" - page {}/{} (colonne {}/{}, ligne {}/{}) - echelle 1:1", idx,
                                  total, c + 1, cols, r + 1, rows);
            page.texts.push_back(
                {Vec2um{um(margin), um(std::max<std::int64_t>(0, margin - 6'000))}, footer, 7.0});
            out.pages.push_back(std::move(page));
        }
    }
    return out;
}

namespace {

constexpr double kPtPerUm = 72.0 / 25'400.0;

std::string pdf_ascii(const std::string& s) {
    std::string r = "(";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '(' || c == ')' || c == '\\') {
            r += '\\';
            r += static_cast<char>(c);
        } else if (c >= 32 && c < 127) {
            r += static_cast<char>(c);
        } else {
            r += '?';
        }
    }
    return r + ")";
}

void path(std::string& s, const std::vector<TemplateSegment>& segs, double ph, const char* style) {
    if (segs.empty()) {
        return;
    }
    s += style;
    for (const auto& g : segs) {
        s += fmt::format("{:.2f} {:.2f} m {:.2f} {:.2f} l\n", g.a.x.value * kPtPerUm,
                         ph - g.a.y.value * kPtPerUm, g.b.x.value * kPtPerUm,
                         ph - g.b.y.value * kPtPerUm);
    }
    s += "S\n";
}

} // namespace

std::string placement_template_to_pdf(const PlacementTemplate& tpl) {
    const double pw = tpl.page_width.value * kPtPerUm;
    const double ph = tpl.page_height.value * kPtPerUm;
    std::vector<std::string> contents;
    for (const auto& pg : tpl.pages) {
        std::string s;
        path(s, pg.stitches, ph, "0.1 G 0.35 w 1 J 1 j [] 0 d\n");
        path(s, pg.axes, ph, "0.5 G 0.4 w [3 2] 0 d\n");
        path(s, pg.crosshair, ph, "0.8 0.1 0.1 RG 0.8 w [] 0 d\n");
        path(s, pg.marks, ph, "0 G 0.5 w [] 0 d\n");
        path(s, pg.ruler, ph, "0 G 0.8 w [] 0 d\n");
        for (const auto& t : pg.texts) {
            s += fmt::format("BT /F1 {:.1f} Tf 0.2 g {:.2f} {:.2f} Td {} Tj ET\n", t.size_pt,
                             t.pos.x.value * kPtPerUm, ph - t.pos.y.value * kPtPerUm - t.size_pt,
                             pdf_ascii(t.text));
        }
        contents.push_back(std::move(s));
    }
    if (contents.empty()) {
        contents.emplace_back();
    }
    std::vector<std::string> objs;
    objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (std::size_t i = 0; i < contents.size(); ++i) {
        kids += fmt::format("{} 0 R ", 4 + 2 * i);
    }
    objs.push_back(fmt::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, contents.size()));
    objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                   "/Encoding /WinAnsiEncoding >>");
    for (std::size_t i = 0; i < contents.size(); ++i) {
        objs.push_back(fmt::format("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {:.2f} {:.2f}] "
                                   "/Resources << /Font << /F1 3 0 R >> >> /Contents {} 0 R >>",
                                   pw, ph, 5 + 2 * i));
        objs.push_back(
            fmt::format("<< /Length {} >>\nstream\n{}endstream", contents[i].size(), contents[i]));
    }
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        offsets.push_back(out.size());
        out += fmt::format("{} 0 obj\n{}\nendobj\n", i + 1, objs[i]);
    }
    const std::size_t xref = out.size();
    out += fmt::format("xref\n0 {}\n0000000000 65535 f \n", objs.size() + 1);
    for (const auto off : offsets) {
        out += fmt::format("{:010} 00000 n \n", off);
    }
    out += fmt::format("trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n",
                       objs.size() + 1, xref);
    return out;
}

} // namespace openstitch::stitch_analysis
