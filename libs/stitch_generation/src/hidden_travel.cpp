// SPDX-License-Identifier: Apache-2.0
#include "hidden_travel.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <queue>
#include <utility>

namespace openstitch::stitch_generation {

namespace {

using stitch::CommandType;
using stitch::StitchPass;

constexpr std::int64_t kMaxCells = 4'000'000;     // grille globale
constexpr std::int64_t kMaxCropCells = 2'500'000; // fenêtre de recherche d'un écart

double dist_point_segment(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

Vec2um to_vec(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

} // namespace

HiddenTravelPlanner::HiddenTravelPlanner(const std::vector<stitch::StitchCommand>& cmds,
                                         const document::SequenceFinishing& finishing)
    : cmds_(cmds), cfg_(finishing.hidden_travel),
      minStitch_(finishing.filter_short_stitches ? finishing.min_stitch_length.value : 0) {}

void HiddenTravelPlanner::build() {
    built_ = true;
    bool any = false;
    std::int32_t minx = 0;
    std::int32_t miny = 0;
    std::int32_t maxx = 0;
    std::int32_t maxy = 0;
    for (const auto& c : cmds_) {
        if (c.type != CommandType::Stitch) {
            continue;
        }
        if (!any) {
            minx = maxx = c.pos.x.value;
            miny = maxy = c.pos.y.value;
            any = true;
        } else {
            minx = std::min(minx, c.pos.x.value);
            maxx = std::max(maxx, c.pos.x.value);
            miny = std::min(miny, c.pos.y.value);
            maxy = std::max(maxy, c.pos.y.value);
        }
    }
    if (!any) {
        return;
    }
    const double halfW = std::max(1, cfg_.cover_thread_width.value) / 2.0;
    std::int32_t cell = 100;
    const auto dims = [&](std::int32_t cs) {
        const std::int64_t pad = static_cast<std::int64_t>(halfW) + cs;
        const std::int64_t w = (static_cast<std::int64_t>(maxx) - minx + 2 * pad) / cs + 1;
        const std::int64_t h = (static_cast<std::int64_t>(maxy) - miny + 2 * pad) / cs + 1;
        return std::pair{w, h};
    };
    while (dims(cell).first * dims(cell).second > kMaxCells && cell < 200) {
        cell += 25;
    }
    const auto [w, h] = dims(cell);
    if (w * h > kMaxCells) {
        return; // design trop grand pour une preuve fiable : aucun pont (sûr)
    }
    const std::int32_t pad = static_cast<std::int32_t>(halfW) + cell;
    grid_.cell = cell;
    grid_.ox = minx - pad;
    grid_.oy = miny - pad;
    grid_.w = static_cast<std::int32_t>(w);
    grid_.h = static_cast<std::int32_t>(h);
    grid_.last.assign(static_cast<std::size_t>(w * h), -1);

    for (std::size_t i = 1; i < cmds_.size(); ++i) {
        stamp(i, false);
    }
    usable_ = true;
}

void HiddenTravelPlanner::stamp(std::size_t i, bool intoSame) {
    const auto& c = cmds_[i];
    const auto& p = cmds_[i - 1];
    if (c.type != CommandType::Stitch || c.pass != StitchPass::TopStitch ||
        p.type != CommandType::Stitch) {
        return;
    }
    const double halfW = std::max(1, cfg_.cover_thread_width.value) / 2.0;
    const std::int32_t cell = grid_.cell;
    const double ax = p.pos.x.value;
    const double ay = p.pos.y.value;
    const double bx = c.pos.x.value;
    const double by = c.pos.y.value;
    const auto idxOf = [&](double v, std::int32_t o) {
        return static_cast<std::int32_t>(std::floor((v - o) / cell));
    };
    const std::int32_t x0 = std::clamp(idxOf(std::min(ax, bx) - halfW, grid_.ox), 0, grid_.w - 1);
    const std::int32_t y0 = std::clamp(idxOf(std::min(ay, by) - halfW, grid_.oy), 0, grid_.h - 1);
    const std::int32_t x1 = std::clamp(idxOf(std::max(ax, bx) + halfW, grid_.ox), 0, grid_.w - 1);
    const std::int32_t y1 = std::clamp(idxOf(std::max(ay, by) + halfW, grid_.oy), 0, grid_.h - 1);
    for (std::int32_t iy = y0; iy <= y1; ++iy) {
        for (std::int32_t ix = x0; ix <= x1; ++ix) {
            const double cx = grid_.ox + (ix + 0.5) * cell;
            const double cy = grid_.oy + (iy + 0.5) * cell;
            if (dist_point_segment(cx, cy, ax, ay, bx, by) <= halfW) {
                const std::size_t k = static_cast<std::size_t>(iy) * grid_.w + ix;
                if (intoSame) {
                    same_[k] = 1;
                } else {
                    grid_.last[k] = static_cast<std::int32_t>(i);
                }
            }
        }
    }
}

void HiddenTravelPlanner::advance_same_color(std::size_t g) {
    if (same_.size() != grid_.last.size() || g + 1 < sameNext_) {
        same_.assign(grid_.last.size(), 0); // premier appel ou retour en arrière
        sameNext_ = 0;
    }
    for (; sameNext_ <= g && sameNext_ < cmds_.size(); ++sameNext_) {
        const auto& c = cmds_[sameNext_];
        if (c.type == CommandType::ColorChange || c.type == CommandType::Stop) {
            std::fill(same_.begin(), same_.end(), static_cast<std::uint8_t>(0));
        } else if (sameNext_ > 0) {
            stamp(sameNext_, true);
        }
    }
}

TravelPlan HiddenTravelPlanner::plan(std::size_t g, Vec2um from, Vec2um to) {
    TravelPlan out;
    const double d = length_um(to - from);
    if (d <= 0.0) {
        return out;
    }
    if (d > static_cast<double>(cfg_.max_bridge_length.value)) {
        out.refusal = TravelRefusal::TooLong;
        return out;
    }
    if (!built_) {
        build();
    }
    if (!usable_) {
        out.refusal = TravelRefusal::NoPath;
        return out;
    }
    const Grid& G = grid_;
    const std::int32_t cell = G.cell;
    const std::int64_t gi = static_cast<std::int64_t>(g);
    const auto cellX = [&](double x) {
        return static_cast<std::int32_t>(std::floor((x - G.ox) / cell));
    };
    const auto cellY = [&](double y) {
        return static_cast<std::int32_t>(std::floor((y - G.oy) / cell));
    };
    const auto inGrid = [&](std::int32_t ix, std::int32_t iy) {
        return ix >= 0 && iy >= 0 && ix < G.w && iy < G.h;
    };
    const bool useSame = cfg_.allow_same_color_surface;
    if (useSame) {
        advance_same_color(g);
    }
    const auto coveredLater = [&](std::int32_t ix, std::int32_t iy) {
        if (!inGrid(ix, iy)) {
            return false;
        }
        const std::size_t k = static_cast<std::size_t>(iy) * G.w + ix;
        return G.last[k] > gi || (useSame && same_[k] != 0);
    };

    const double halfW = std::max(1, cfg_.cover_thread_width.value) / 2.0;
    const std::int32_t rE =
        static_cast<std::int32_t>(std::ceil((halfW + std::max(0, cfg_.cover_margin.value)) / cell));
    const std::int32_t sx = cellX(from.x.value);
    const std::int32_t sy = cellY(from.y.value);
    const std::int32_t tx = cellX(to.x.value);
    const std::int32_t ty = cellY(to.y.value);
    if (!coveredLater(sx, sy) || !coveredLater(tx, ty)) {
        out.refusal = TravelRefusal::NoCoverAtEnd;
        return out;
    }

    // Fenêtre de recherche : boîte des deux extrémités élargie de ce que permet la longueur
    // maximale, bornée à la grille.
    const std::int32_t grow =
        static_cast<std::int32_t>(
            std::ceil((static_cast<double>(cfg_.max_bridge_length.value) - d) / 2.0 / cell)) +
        rE + 2;
    const std::int32_t cx0 = std::max(0, std::min(sx, tx) - grow);
    const std::int32_t cy0 = std::max(0, std::min(sy, ty) - grow);
    const std::int32_t cx1 = std::min(G.w - 1, std::max(sx, tx) + grow);
    const std::int32_t cy1 = std::min(G.h - 1, std::max(sy, ty) + grow);
    const std::int32_t cw = cx1 - cx0 + 1;
    const std::int32_t ch = cy1 - cy0 + 1;
    if (static_cast<std::int64_t>(cw) * ch > kMaxCropCells) {
        out.refusal = TravelRefusal::NoPath;
        return out;
    }

    // Image intégrale des cellules NON recouvertes sur la fenêtre élargie de rE (érosion carrée,
    // plus stricte qu'un disque : sûr).
    const std::int32_t wx0 = std::max(0, cx0 - rE);
    const std::int32_t wy0 = std::max(0, cy0 - rE);
    const std::int32_t wx1 = std::min(G.w - 1, cx1 + rE);
    const std::int32_t wy1 = std::min(G.h - 1, cy1 + rE);
    const std::int32_t ww = wx1 - wx0 + 1;
    const std::int32_t wh = wy1 - wy0 + 1;
    std::vector<std::int32_t> integral(static_cast<std::size_t>(ww + 1) * (wh + 1), 0);
    const auto I = [&](std::int32_t x, std::int32_t y) -> std::int32_t& {
        return integral[static_cast<std::size_t>(y) * (ww + 1) + x];
    };
    for (std::int32_t y = 0; y < wh; ++y) {
        for (std::int32_t x = 0; x < ww; ++x) {
            const std::int32_t unc = coveredLater(wx0 + x, wy0 + y) ? 0 : 1;
            I(x + 1, y + 1) = unc + I(x, y + 1) + I(x + 1, y) - I(x, y);
        }
    }
    const auto uncoveredIn = [&](std::int32_t ax, std::int32_t ay, std::int32_t bx,
                                 std::int32_t by) { // bornes incluses, coordonnées de grille
        const std::int32_t x0 = ax - wx0;
        const std::int32_t y0 = ay - wy0;
        const std::int32_t x1 = bx - wx0 + 1;
        const std::int32_t y1 = by - wy0 + 1;
        return I(x1, y1) - I(x0, y1) - I(x1, y0) + I(x0, y0);
    };

    std::vector<std::uint8_t> pass(static_cast<std::size_t>(cw) * ch, 0);
    const auto at = [&](std::int32_t ix, std::int32_t iy) -> std::uint8_t& {
        return pass[static_cast<std::size_t>(iy - cy0) * cw + (ix - cx0)];
    };
    const auto cheb = [](std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) {
        return std::max(std::abs(ax - bx), std::abs(ay - by));
    };
    for (std::int32_t iy = cy0; iy <= cy1; ++iy) {
        for (std::int32_t ix = cx0; ix <= cx1; ++ix) {
            bool ok = false;
            if (ix - rE >= 0 && iy - rE >= 0 && ix + rE < G.w && iy + rE < G.h) {
                ok = uncoveredIn(ix - rE, iy - rE, ix + rE, iy + rE) == 0;
            }
            // Autour des deux piqûres d'extrémité (déjà existantes) : recouvrement simple.
            if (!ok && coveredLater(ix, iy) &&
                (cheb(ix, iy, sx, sy) <= rE || cheb(ix, iy, tx, ty) <= rE)) {
                ok = true;
            }
            at(ix, iy) = ok ? 1 : 0;
        }
    }
    const auto passable = [&](std::int32_t ix, std::int32_t iy) {
        return ix >= cx0 && iy >= cy0 && ix <= cx1 && iy <= cy1 && at(ix, iy) != 0;
    };
    const auto lineOfSight = [&](std::int32_t ax, std::int32_t ay, std::int32_t bx,
                                 std::int32_t by) {
        const std::int32_t dx = std::abs(bx - ax);
        const std::int32_t dy = std::abs(by - ay);
        const std::int32_t stepx = ax < bx ? 1 : -1;
        const std::int32_t stepy = ay < by ? 1 : -1;
        std::int32_t err = dx - dy;
        std::int32_t x = ax;
        std::int32_t y = ay;
        for (;;) {
            if (!passable(x, y)) {
                return false;
            }
            if (x == bx && y == by) {
                return true;
            }
            const std::int32_t e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += stepx;
            }
            if (e2 < dx) {
                err += dx;
                y += stepy;
            }
        }
    };
    // Vérification finale d'une polyligne en coordonnées µm, échantillonnée au demi-pas de grille.
    const auto polyOk = [&](const std::vector<Vec2um>& pts) {
        for (std::size_t k = 1; k < pts.size(); ++k) {
            const double ax = pts[k - 1].x.value;
            const double ay = pts[k - 1].y.value;
            const double bx = pts[k].x.value;
            const double by = pts[k].y.value;
            const int steps = std::max(
                1, static_cast<int>(std::ceil(std::hypot(bx - ax, by - ay) / (cell / 2.0))));
            for (int s = 0; s <= steps; ++s) {
                const double t = static_cast<double>(s) / steps;
                if (!passable(cellX(ax + (bx - ax) * t), cellY(ay + (by - ay) * t))) {
                    return false;
                }
            }
        }
        return true;
    };

    // Saut recouvert : aucun point ajouté, ni coupe ni point d'arrêt.
    if (d <= static_cast<double>(cfg_.max_hidden_jump.value) && lineOfSight(sx, sy, tx, ty) &&
        polyOk({from, to})) {
        out.decision = TravelDecision::HiddenJump;
        return out;
    }

    // Trajet cousu : A* à 8 voisins, coûts entiers 10/14, voisins dans un ordre fixe, file
    // départagée par (coût, indice) : résultat indépendant de tout ordre non spécifié.
    const auto idx = [&](std::int32_t ix, std::int32_t iy) {
        return static_cast<std::int32_t>((iy - cy0) * cw + (ix - cx0));
    };
    const std::size_t total = static_cast<std::size_t>(cw) * ch;
    std::vector<std::int32_t> gScore(total, INT_MAX);
    std::vector<std::int32_t> parent(total, -1);
    std::vector<std::uint8_t> closed(total, 0);
    using Entry = std::pair<std::int32_t, std::int32_t>; // (f, indice)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    const auto heur = [&](std::int32_t ix, std::int32_t iy) {
        const std::int32_t dx = std::abs(ix - tx);
        const std::int32_t dy = std::abs(iy - ty);
        return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
    };
    constexpr std::array<std::array<int, 2>, 8> kNeighbors{
        {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    gScore[static_cast<std::size_t>(idx(sx, sy))] = 0;
    open.emplace(heur(sx, sy), idx(sx, sy));
    const std::int32_t goal = idx(tx, ty);
    bool found = false;
    while (!open.empty()) {
        const auto [f, cur] = open.top();
        open.pop();
        (void)f;
        if (closed[static_cast<std::size_t>(cur)] != 0) {
            continue;
        }
        closed[static_cast<std::size_t>(cur)] = 1;
        if (cur == goal) {
            found = true;
            break;
        }
        const std::int32_t cxi = cx0 + cur % cw;
        const std::int32_t cyi = cy0 + cur / cw;
        for (const auto& nb : kNeighbors) {
            const std::int32_t nx = cxi + nb[0];
            const std::int32_t ny = cyi + nb[1];
            if (!passable(nx, ny)) {
                continue;
            }
            const bool diagonal = nb[0] != 0 && nb[1] != 0;
            if (diagonal && (!passable(cxi + nb[0], cyi) || !passable(cxi, cyi + nb[1]))) {
                continue; // pas de passage en biais entre deux cellules bloquées
            }
            const std::int32_t ni = idx(nx, ny);
            const std::int32_t cand = gScore[static_cast<std::size_t>(cur)] + (diagonal ? 14 : 10);
            if (cand < gScore[static_cast<std::size_t>(ni)]) {
                gScore[static_cast<std::size_t>(ni)] = cand;
                parent[static_cast<std::size_t>(ni)] = cur;
                open.emplace(cand + heur(nx, ny), ni);
            }
        }
    }
    if (!found) {
        out.refusal = TravelRefusal::NoPath;
        return out;
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> cells;
    for (std::int32_t cur = goal; cur != -1; cur = parent[static_cast<std::size_t>(cur)]) {
        cells.emplace_back(cx0 + cur % cw, cy0 + cur / cw);
    }
    std::reverse(cells.begin(), cells.end());

    // Simplification gloutonne par ligne de vue, puis points intermédiaires aux centres de cellule.
    std::vector<Vec2um> poly{from};
    std::size_t i = 0;
    while (i + 1 < cells.size()) {
        std::size_t j = cells.size() - 1;
        while (j > i + 1 &&
               !lineOfSight(cells[i].first, cells[i].second, cells[j].first, cells[j].second)) {
            --j;
        }
        i = j;
        if (i + 1 < cells.size()) {
            poly.push_back(to_vec(G.ox + (cells[i].first + 0.5) * cell,
                                  G.oy + (cells[i].second + 0.5) * cell));
        }
    }
    poly.push_back(to);
    double pathLen = 0.0;
    for (std::size_t k = 1; k < poly.size(); ++k) {
        pathLen += length_um(poly[k] - poly[k - 1]);
    }
    if (pathLen > static_cast<double>(cfg_.max_bridge_length.value)) {
        out.refusal = TravelRefusal::TooLong;
        return out;
    }

    // Points du trajet : chaque côté redécoupé en parts égales d'au plus `bridge_stitch_length`.
    const double step = std::max(1, cfg_.bridge_stitch_length.value);
    std::vector<Vec2um> via;
    for (std::size_t k = 1; k < poly.size(); ++k) {
        const double ax = poly[k - 1].x.value;
        const double ay = poly[k - 1].y.value;
        const double bx = poly[k].x.value;
        const double by = poly[k].y.value;
        const int parts =
            std::max(1, static_cast<int>(std::ceil(std::hypot(bx - ax, by - ay) / step)));
        for (int s = 1; s < parts; ++s) {
            via.push_back(to_vec(ax + (bx - ax) * s / parts, ay + (by - ay) * s / parts));
        }
        if (k + 1 < poly.size()) {
            via.push_back(poly[k]);
        }
    }
    std::vector<Vec2um> full{from};
    full.insert(full.end(), via.begin(), via.end());
    full.push_back(to);
    if (!polyOk(full)) {
        out.refusal = TravelRefusal::NoPath;
        return out;
    }

    // Aucun point cousu plus court que la longueur minimale des finitions : on retire les points
    // intermédiaires trop proches, sauf si la polyligne qui en résulte sort du masque prouvé.
    if (minStitch_ > 0) {
        std::vector<Vec2um> kept;
        Vec2um prev = from;
        for (const Vec2um& p : via) {
            if (length_um(p - prev) >= minStitch_) {
                kept.push_back(p);
                prev = p;
            }
        }
        while (!kept.empty() && length_um(to - kept.back()) < minStitch_) {
            kept.pop_back();
        }
        std::vector<Vec2um> filtered{from};
        filtered.insert(filtered.end(), kept.begin(), kept.end());
        filtered.push_back(to);
        if (polyOk(filtered)) {
            via = std::move(kept);
        }
    }
    out.decision = TravelDecision::Bridge;
    out.via = std::move(via);
    return out;
}

} // namespace openstitch::stitch_generation
