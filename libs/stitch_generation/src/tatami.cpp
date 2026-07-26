// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/tatami.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace openstitch::stitch_generation {

namespace {

struct PointD {
    double x{0.0};
    double y{0.0};
};

// Rotation d'un point du repère modèle vers le repère « rangées horizontales »
// (rotation de -angle) et retour (+angle).
PointD rotate(PointD p, double cosA, double sinA) {
    return {p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA};
}

// Intersections d'une horizontale y=scanY avec un polygone fermé (liste
// d'arêtes), ajoutées à `xs`. Convention demi-ouverte sur y pour ne pas
// compter deux fois un sommet.
void scan_polygon(const std::vector<PointD>& poly, double scanY, std::vector<double>& xs) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const PointD a = poly[i];
        const PointD b = poly[(i + 1) % n];
        const double y0 = a.y;
        const double y1 = b.y;
        if ((y0 <= scanY && y1 > scanY) || (y1 <= scanY && y0 > scanY)) {
            const double t = (scanY - y0) / (y1 - y0);
            xs.push_back(a.x + t * (b.x - a.x));
        }
    }
}

Vec2um to_um(PointD p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

// Orientation du triplet (a, b, c) : > 0 gauche, < 0 droite, 0 colinéaire.
double orient(PointD a, PointD b, PointD c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Intersection PROPRE de deux segments (ils se croisent transversalement).
// Exclut les cas colinéaires et les contacts par extrémité/sommet (orient == 0),
// ce qui est exactement ce qu'il faut : une liaison qui longe un bord (colinéaire)
// est autorisée ; une liaison qui coupe franchement un bord ne l'est pas.
bool proper_intersect(PointD a, PointD b, PointD c, PointD d) {
    const double o1 = orient(a, b, c);
    const double o2 = orient(a, b, d);
    const double o3 = orient(c, d, a);
    const double o4 = orient(c, d, b);
    return (o1 > 0.0) != (o2 > 0.0) && (o3 > 0.0) != (o4 > 0.0) && o1 != 0.0 && o2 != 0.0 &&
           o3 != 0.0 && o4 != 0.0;
}

// Point dans polygone (lancer de rayon horizontal, règle pair-impair).
bool point_in_poly(const std::vector<PointD>& poly, PointD p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const PointD a = poly[i];
        const PointD b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

// p est-il dans la région = dans l'extérieur ET hors de tous les trous ?
bool in_region(const std::vector<std::vector<PointD>>& polys, PointD p) {
    if (polys.empty() || !point_in_poly(polys[0], p)) {
        return false;
    }
    for (std::size_t i = 1; i < polys.size(); ++i) {
        if (point_in_poly(polys[i], p)) {
            return false;
        }
    }
    return true;
}

// La liaison [a,b] (dans le repère des rangées) est-elle un trajet cousu
// INVALIDE ? `spacing` est l'écart entre rangées. Une liaison légitime entre
// deux rangées voisines est quasi VERTICALE (petit écart en x, ~spacing en y)
// et longe éventuellement un bord — elle est autorisée. Une liaison INVALIDE
// enjambe un vide : c'est soit un croisement franc d'une arête, soit un
// connecteur DIAGONAL LARGE (grand écart en x) dont un point intérieur sort de
// la région (encoche, poche concave, pont au-dessus d'un trou — cas où les
// extrémités reposent sur les bords, que le seul test de croisement manque).
bool connector_invalid(const std::vector<std::vector<PointD>>& polys, PointD a, PointD b,
                       double spacing) {
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (proper_intersect(a, b, poly[i], poly[(i + 1) % n])) {
                return true;
            }
        }
    }
    // Seuls les connecteurs LARGES en x peuvent enjamber un vide ; les
    // connecteurs quasi-verticaux (le long d'un bord) sont légitimes.
    if (std::abs(b.x - a.x) > 2.0 * spacing) {
        for (int k = 1; k <= 3; ++k) {
            const double t = static_cast<double>(k) / 4.0;
            const PointD p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            if (!in_region(polys, p)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::vector<FillStitch> fill_tatami(const geometry::PathSet& region,
                                    const document::TatamiParams& params) {
    std::vector<FillStitch> out;
    const double spacing = static_cast<double>(std::max<std::int32_t>(1, params.row_spacing.value));
    const double stitchLen =
        static_cast<double>(std::max<std::int32_t>(1, params.stitch_length.value));
    const int stagger = std::max(1, params.stagger);

    const double cosA = std::cos(-params.angle.radians);
    const double sinA = std::sin(-params.angle.radians);
    const double cosB = std::cos(params.angle.radians);
    const double sinB = std::sin(params.angle.radians);

    // Polygones (extérieur + trous) exprimés dans le repère des rangées.
    std::vector<std::vector<PointD>> polys;
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<PointD> poly;
        poly.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            poly.push_back(rotate({static_cast<double>(node.pos.x.value),
                                   static_cast<double>(node.pos.y.value)},
                                  cosA, sinA));
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    addPoly(region.outer);
    for (const auto& hole : region.holes) {
        addPoly(hole);
    }
    if (polys.empty()) {
        return out;
    }

    double minY = polys[0][0].y;
    double maxY = minY;
    for (const auto& poly : polys) {
        for (const PointD& p : poly) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }

    // 1) Construit les SEGMENTS de rangée (un par intervalle intérieur), avec
    //    leurs pénétrations d'aiguille. Les segments sont les nœuds d'un graphe.
    struct Segment {
        int row{0};
        double y{0.0};
        double lo{0.0};
        double hi{0.0};
        std::vector<double> pens;  // pénétrations, ordonnées lo -> hi
    };
    std::vector<Segment> segs;
    std::map<int, std::vector<int>> byRow;
    int rowIndex = 0;
    for (double y = minY + spacing / 2.0; y < maxY; y += spacing, ++rowIndex) {
        std::vector<double> xs;
        for (const auto& poly : polys) {
            scan_polygon(poly, y, xs);
        }
        std::sort(xs.begin(), xs.end());
        const double phase = stitchLen * (static_cast<double>(rowIndex % stagger) /
                                          static_cast<double>(stagger));
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            const double lo = xs[i];
            const double hi = xs[i + 1];
            if (hi <= lo) {
                continue;
            }
            Segment s;
            s.row = rowIndex;
            s.y = y;
            s.lo = lo;
            s.hi = hi;
            s.pens.push_back(lo);
            const double firstK = std::ceil((lo - phase) / stitchLen);
            for (double x = phase + firstK * stitchLen; x < hi; x += stitchLen) {
                if (x > lo) {
                    s.pens.push_back(x);
                }
            }
            s.pens.push_back(hi);
            byRow[rowIndex].push_back(static_cast<int>(segs.size()));
            segs.push_back(std::move(s));
        }
    }
    const int n = static_cast<int>(segs.size());
    if (n == 0) {
        return out;
    }

    // 2) ADJACENCE : deux segments de rangées consécutives qui se chevauchent en
    //    x sont voisins — la bande entre eux est intérieure à la région, donc
    //    une liaison entre eux ne traverse jamais un trou (le trou aurait coupé
    //    la rangée). C'est ce qui permet de contourner les trous (§15.5).
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(n));
    for (const auto& [row, ids] : byRow) {
        const auto it = byRow.find(row + 1);
        if (it == byRow.end()) {
            continue;
        }
        for (const int i : ids) {
            for (const int j : it->second) {
                const double ov = std::min(segs[i].hi, segs[j].hi) -
                                  std::max(segs[i].lo, segs[j].lo);
                if (ov > 0.0) {
                    adj[i].push_back(j);
                    adj[j].push_back(i);
                }
            }
        }
    }

    // 3) PARCOURS glouton du graphe : on suit toujours un voisin non visité (la
    //    liaison reste dans la région) ; à défaut, on saute (déplacement) vers le
    //    segment non visité le plus proche. On entre chaque segment par l'extrémité
    //    la plus proche du point courant. Déterministe (tie-break par index).
    const auto dist2 = [](PointD a, PointD b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        return dx * dx + dy * dy;
    };
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        order[static_cast<std::size_t>(i)] = i;
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (segs[a].row != segs[b].row) return segs[a].row < segs[b].row;
        return segs[a].lo < segs[b].lo;
    });

    std::vector<char> visited(static_cast<std::size_t>(n), 0);
    bool hasPrev = false;
    PointD prev{};
    int visitedCount = 0;
    int current = -1;
    bool jumpStart = true;  // true = on ARRIVE sur ce segment par un déplacement
    std::size_t orderCursor = 0;

    while (visitedCount < n) {
        if (current == -1) {
            while (orderCursor < order.size() && visited[static_cast<std::size_t>(order[orderCursor])]) {
                ++orderCursor;
            }
            current = order[orderCursor];
            jumpStart = true;  // segment atteint par saut (nouvelle composante)
        }
        const Segment& s = segs[static_cast<std::size_t>(current)];
        const PointD endLo{s.lo, s.y};
        const PointD endHi{s.hi, s.y};
        const bool forward = !hasPrev || dist2(prev, endLo) <= dist2(prev, endHi);
        const int m = static_cast<int>(s.pens.size());
        for (int k = 0; k < m; ++k) {
            const double x = forward ? s.pens[static_cast<std::size_t>(k)]
                                     : s.pens[static_cast<std::size_t>(m - 1 - k)];
            const PointD rp{x, s.y};
            // La liaison vers le premier point d'un segment est un SAUT (aiguille
            // levée) si l'on n'y est pas arrivé par une arête du graphe, OU si le
            // trajet cousu couperait un bord de la région ou d'un trou. Le
            // chevauchement des rangées ne donne que des candidats ; la validation
            // géométrique (connector_crosses) tranche.
            const bool jump =
                (k == 0) &&
                (jumpStart || !hasPrev || connector_invalid(polys, prev, rp, spacing));
            out.push_back({to_um(rotate(rp, cosB, sinB)), jump});
            prev = rp;
            hasPrev = true;
        }
        visited[static_cast<std::size_t>(current)] = 1;
        ++visitedCount;

        // Prochain voisin non visité, le plus proche d'une extrémité.
        int next = -1;
        double best = std::numeric_limits<double>::max();
        for (const int j : adj[static_cast<std::size_t>(current)]) {
            if (visited[static_cast<std::size_t>(j)]) {
                continue;
            }
            const double d = std::min(dist2(prev, {segs[j].lo, segs[j].y}),
                                      dist2(prev, {segs[j].hi, segs[j].y}));
            if (d < best || (d == best && (next == -1 || j < next))) {
                best = d;
                next = j;
            }
        }
        current = next;
        jumpStart = false;  // atteint par une arête du graphe -> couture
    }
    return out;
}

}  // namespace openstitch::stitch_generation
