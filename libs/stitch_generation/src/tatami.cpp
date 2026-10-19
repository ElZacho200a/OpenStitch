// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/tatami.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

#include "openstitch/geometry/offset.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"

namespace openstitch::stitch_generation {

namespace {

struct PointD {
    double x{0.0};
    double y{0.0};
};

// Rotation d'un point du repÃ¨re modÃ¨le vers le repÃ¨re Â« rangÃ©es horizontales Â»
// (rotation de -angle) et retour (+angle).
PointD rotate(PointD p, double cosA, double sinA) {
    return {p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA};
}

// Intersections d'une horizontale y=scanY avec un polygone fermÃ© (liste
// d'arÃªtes), ajoutÃ©es Ã  `xs`. Convention demi-ouverte sur y pour ne pas
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

// Distance au carrÃ© de p au segment [c,d].
double point_seg_dist2(PointD p, PointD c, PointD d) {
    const double dx = d.x - c.x;
    const double dy = d.y - c.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        const double ex = p.x - c.x;
        const double ey = p.y - c.y;
        return ex * ex + ey * ey;
    }
    double t = ((p.x - c.x) * dx + (p.y - c.y) * dy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const double fx = p.x - (c.x + t * dx);
    const double fy = p.y - (c.y + t * dy);
    return fx * fx + fy * fy;
}

// Index des arÃªtes (extÃ©rieur + trous) par bandes horizontales, construit
// une fois par remplissage (audit perf 2026-09, docs/performance-audit.md) :
// les tests gÃ©omÃ©triques ci-dessous Ã©taient en O(arÃªtes) et appelÃ©s des
// dizaines de milliers de fois par gÃ©nÃ©ration (liaisons de rangÃ©es,
// autoroute d'underpath), soit l'essentiel du coÃ»t du tatami.
//
// Contrat d'EXACTITUDE : une requÃªte renvoie un SUR-ENSEMBLE des arÃªtes
// capables de satisfaire le prÃ©dicat d'origine (chaque arÃªte est inscrite
// dans toutes les bandes couvrant son intervalle en y Ã©largi de `kMargin`),
// et chaque candidate est Ã©valuÃ©e avec exactement la mÃªme arithmÃ©tique
// qu'avant ; les rÃ©sultats ne dÃ©pendent pas de l'ordre de visite (paritÃ©,
// liste de paramÃ¨tres triÃ©e). Une arÃªte hors des bandes interrogÃ©es est Ã 
// plus de `kMargin` en y du point/segment testÃ© : elle ne pouvait ni
// basculer la paritÃ©, ni Ãªtre Ã  moins de 0,01 Âµm, ni couper le segment.
class EdgeIndex {
public:
    struct Edge {
        PointD c;            // poly[k]
        PointD d;            // poly[(k + 1) % n]
        std::size_t poly{0}; // 0 = extÃ©rieur, >= 1 = trous
    };

    explicit EdgeIndex(const std::vector<std::vector<PointD>>& polys) : polyCount_(polys.size()) {
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();
        for (std::size_t p = 0; p < polys.size(); ++p) {
            const auto& poly = polys[p];
            const std::size_t n = poly.size();
            for (std::size_t k = 0; k < n; ++k) {
                edges_.push_back({poly[k], poly[(k + 1) % n], p});
                minY = std::min(minY, poly[k].y);
                maxY = std::max(maxY, poly[k].y);
            }
        }
        stamp_.assign(edges_.size(), 0);
        if (edges_.empty()) {
            return;
        }
        minY_ = minY;
        const double span = maxY - minY;
        bandCount_ = std::clamp(
            static_cast<int>(std::sqrt(static_cast<double>(edges_.size())) * 2.0), 1, 4096);
        bandH_ = span > 0.0 ? span / bandCount_ : 1.0;
        bands_.assign(static_cast<std::size_t>(bandCount_), {});
        for (std::size_t e = 0; e < edges_.size(); ++e) {
            const double y0 = std::min(edges_[e].c.y, edges_[e].d.y) - kMargin;
            const double y1 = std::max(edges_[e].c.y, edges_[e].d.y) + kMargin;
            for (int b = band(y0); b <= band(y1); ++b) {
                bands_[static_cast<std::size_t>(b)].push_back(static_cast<std::uint32_t>(e));
            }
        }
    }

    [[nodiscard]] bool empty() const { return polyCount_ == 0; }
    [[nodiscard]] std::size_t poly_count() const { return polyCount_; }

    // Appelle f(edge) pour chaque arÃªte candidate (sans doublon) dont
    // l'intervalle en y Ã©largi peut rencontrer [y0, y1].
    template <typename F> void for_edges(double y0, double y1, F&& f) const {
        if (edges_.empty()) {
            return;
        }
        ++generation_;
        if (generation_ == 0) { // dÃ©bordement du compteur : rÃ©initialise les marques
            std::fill(stamp_.begin(), stamp_.end(), 0u);
            generation_ = 1;
        }
        const int b1 = band(y1);
        for (int b = band(y0); b <= b1; ++b) {
            for (const std::uint32_t e : bands_[static_cast<std::size_t>(b)]) {
                if (stamp_[e] != generation_) {
                    stamp_[e] = generation_;
                    f(edges_[e]);
                }
            }
        }
    }

private:
    static constexpr double kMargin = 1.0; // 1 Âµm, trÃ¨s au-delÃ  des tolÃ©rances des prÃ©dicats

    [[nodiscard]] int band(double y) const {
        const double b = std::floor((y - minY_) / bandH_);
        if (!(b > 0.0)) {
            return 0;
        }
        return b >= bandCount_ ? bandCount_ - 1 : static_cast<int>(b);
    }

    std::size_t polyCount_{0};
    std::vector<Edge> edges_;
    std::vector<std::vector<std::uint32_t>> bands_;
    double minY_{0.0};
    double bandH_{1.0};
    int bandCount_{1};
    mutable std::vector<std::uint32_t> stamp_;
    mutable std::uint32_t generation_{0};
};

// p est-il dans la rÃ©gion = dans l'extÃ©rieur ET hors de tous les trous ?
//
// Un point (quasi) SUR une arÃªte d'un des polygones (extÃ©rieur ou trou) est
// traitÃ© comme faisant partie de la rÃ©gion, AVANT le test pair-impair : ce
// dernier est ambigu pile sur une frontiÃ¨re â€” pour un trou, il classe mÃªme le
// point comme Â« dans le trou Â» (cf. arÃªte opposÃ©e qui bascule seule la
// paritÃ©), ce qui rejetterait Ã  tort un suivi de bord lÃ©gitime. Pair-impair :
// lancer de rayon horizontal, arÃªte (a = poly[k+1], b = poly[k]) exactement
// comme la boucle `for (i = 0, j = n - 1; i < n; j = i++)` d'origine.
bool in_region(const EdgeIndex& index, PointD p) {
    if (index.empty()) {
        return false;
    }
    constexpr double kEps2 = 1e-4; // 0,01 Âµm : marge numÃ©rique, pas une tolÃ©rance gÃ©omÃ©trique
    bool onBoundary = false;
    std::vector<char> parity(index.poly_count(), 0);
    index.for_edges(p.y, p.y, [&](const EdgeIndex::Edge& e) {
        if (point_seg_dist2(p, e.c, e.d) < kEps2) {
            onBoundary = true;
        }
        const PointD a = e.d;
        const PointD b = e.c;
        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            parity[e.poly] = static_cast<char>(!parity[e.poly]);
        }
    });
    if (onBoundary) {
        return true;
    }
    if (!parity[0]) {
        return false;
    }
    for (std::size_t i = 1; i < parity.size(); ++i) {
        if (parity[i]) {
            return false;
        }
    }
    return true;
}

// La liaison [a,b] (dans le repÃ¨re des rangÃ©es) est-elle un trajet cousu
// INVALIDE ? Robuste Ã  l'orientation du segment et aux contacts par
// sommet/extrÃ©mitÃ© : on dÃ©coupe [a,b] Ã  CHAQUE intersection paramÃ©trique avec
// une arÃªte d'un polygone (extÃ©rieur ou trou), y COMPRIS les contacts
// dÃ©gÃ©nÃ©rÃ©s (le segment passe exactement par un sommet), puis on vÃ©rifie que
// le MILIEU de chaque sous-segment ainsi obtenu reste dans la rÃ©gion. Une
// arÃªte colinÃ©aire au segment (suivi de bord/frontiÃ¨re) ne produit aucune
// dÃ©coupe : longer un bord reste autorisÃ©. Ce test ne dÃ©pend d'aucun seuil sur
// l'Ã©cart en x â€” un connecteur parfaitement vertical qui traverse un trou de
// part en part (mÃªme en touchant ses sommets, sans jamais le Â« croiser Â»
// franchement) est donc dÃ©tectÃ©.
//
// `edgeTol` (µm, 0 = strict) : tolérance au bord. Un sous-segment dont le milieu est
// dehors reste admis si tous ses points d'échantillon sont à moins de `edgeTol` du
// contour. C'est le cas d'une liaison entre deux fins de rangée voisines au bord d'un
// coin rentrant ou d'une fente : la corde sort du polygone d'une fraction de
// millimètre (audit Possible.osp : 973 sauts sur 1 459, presque tous < 1 mm, pour des
// rangées voisines pourtant reliées par le graphe). Le fil y reste sous la bordure.
bool near_boundary(const EdgeIndex& index, PointD p, double tol) {
    const double tol2 = tol * tol;
    bool near = false;
    index.for_edges(p.y - tol, p.y + tol, [&](const EdgeIndex::Edge& e) {
        if (!near && point_seg_dist2(p, e.c, e.d) <= tol2) {
            near = true;
        }
    });
    return near;
}

bool connector_invalid(const EdgeIndex& index, PointD a, PointD b, double edgeTol = 0.0) {
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abLen = std::sqrt(abx * abx + aby * aby);
    if (abLen < 1e-6) {
        return !in_region(index, a);
    }
    std::vector<double> ts{0.0, 1.0};
    index.for_edges(std::min(a.y, b.y), std::max(a.y, b.y), [&](const EdgeIndex::Edge& e) {
        const PointD c = e.c;
        const PointD d = e.d;
        const double dcx = d.x - c.x;
        const double dcy = d.y - c.y;
        const double cdLen = std::sqrt(dcx * dcx + dcy * dcy);
        const double denom = abx * dcy - aby * dcx;
        if (std::abs(denom) < 1e-9 * abLen * cdLen) {
            return; // parallÃ¨le/colinÃ©aire : suivi de bord, pas de dÃ©coupe
        }
        const double t = ((c.x - a.x) * dcy - (c.y - a.y) * dcx) / denom;
        const double s = ((c.x - a.x) * aby - (c.y - a.y) * abx) / denom;
        if (t > 1e-9 && t < 1.0 - 1e-9 && s >= -1e-9 && s <= 1.0 + 1e-9) {
            ts.push_back(t);
        }
    });
    std::sort(ts.begin(), ts.end());
    for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
        const double tm = (ts[i] + ts[i + 1]) / 2.0;
        const PointD p{a.x + abx * tm, a.y + aby * tm};
        if (in_region(index, p)) {
            continue;
        }
        if (edgeTol <= 0.0) {
            return true;
        }
        // Dehors : toléré seulement si le sous-segment ne s'écarte jamais du contour de plus
        // de `edgeTol` (échantillons au quart, milieu et trois quarts).
        for (const double f : {0.25, 0.5, 0.75}) {
            const double tf = ts[i] + (ts[i + 1] - ts[i]) * f;
            if (!near_boundary(index, {a.x + abx * tf, a.y + aby * tf}, edgeTol)) {
                return true;
            }
        }
    }
    return false;
}

// Tolérance au bord des liaisons de rangées (µm) : bien sous l'écartement (0,4 mm).
constexpr double kLinkEdgeTol = 150.0;

} // namespace

std::vector<FillStitch> fill_tatami(const geometry::PathSet& region,
                                    const document::TatamiParams& params) {
    std::vector<FillStitch> out;
    const double spacing = static_cast<double>(std::max<std::int32_t>(1, params.row_spacing.value));
    const double stitchLen =
        static_cast<double>(std::max<std::int32_t>(1, params.stitch_length.value));
    const int stagger = std::max(1, params.stagger);
    // HP-ENG-001 : dÃ©passement des rangÃ©es dans l'axe du fil (bornÃ© Ã  3 mm : un fichier
    // aberrant ne doit pas Ã©taler la couture hors de toute proportion).
    const double pull =
        static_cast<double>(std::clamp<std::int32_t>(params.pull_compensation.value, 0, 3'000));

    const double cosA = std::cos(-params.angle.radians);
    const double sinA = std::sin(-params.angle.radians);
    const double cosB = std::cos(params.angle.radians);
    const double sinB = std::sin(params.angle.radians);

    // Polygones (extÃ©rieur + trous) exprimÃ©s dans le repÃ¨re des rangÃ©es.
    std::vector<std::vector<PointD>> polys;
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<PointD> poly;
        poly.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            poly.push_back(rotate(
                {static_cast<double>(node.pos.x.value), static_cast<double>(node.pos.y.value)},
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
    const EdgeIndex edgeIndex(polys);

    double minY = polys[0][0].y;
    double maxY = minY;
    for (const auto& poly : polys) {
        for (const PointD& p : poly) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }

    // 1) Construit les SEGMENTS de rangÃ©e (un par intervalle intÃ©rieur), avec
    //    leurs pÃ©nÃ©trations d'aiguille. Les segments sont les nÅ“uds d'un graphe.
    struct Segment {
        int row{0};
        double y{0.0};
        double lo{0.0};
        double hi{0.0};
        std::vector<double> pens; // pÃ©nÃ©trations, ordonnÃ©es lo -> hi
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
        const double phase =
            stitchLen * (static_cast<double>(rowIndex % stagger) / static_cast<double>(stagger));
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
            // Compensation du tirage : seules les pÃ©nÃ©trations d'extrÃ©mitÃ© dÃ©passent ; les
            // bornes `lo`/`hi` du segment restent celles de la rÃ©gion (adjacence, validation
            // des liaisons).
            s.pens.front() = lo - pull;
            s.pens.back() = hi + pull;
            byRow[rowIndex].push_back(static_cast<int>(segs.size()));
            segs.push_back(std::move(s));
        }
    }
    const int n = static_cast<int>(segs.size());
    if (n == 0) {
        return out;
    }

    // 2) ADJACENCE : deux segments de rangÃ©es consÃ©cutives qui se chevauchent en
    //    x sont voisins â€” la bande entre eux est intÃ©rieure Ã  la rÃ©gion, donc
    //    une liaison entre eux ne traverse jamais un trou (le trou aurait coupÃ©
    //    la rangÃ©e). C'est ce qui permet de contourner les trous (Â§15.5).
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(n));
    for (const auto& [row, ids] : byRow) {
        const auto it = byRow.find(row + 1);
        if (it == byRow.end()) {
            continue;
        }
        for (const int i : ids) {
            for (const int j : it->second) {
                const double ov =
                    std::min(segs[i].hi, segs[j].hi) - std::max(segs[i].lo, segs[j].lo);
                if (ov > 0.0) {
                    adj[i].push_back(j);
                    adj[j].push_back(i);
                }
            }
        }
    }

    // 3) PARCOURS glouton du graphe : on suit toujours un voisin non visitÃ© (la
    //    liaison reste dans la rÃ©gion) ; Ã  dÃ©faut, on saute (dÃ©placement) vers le
    //    segment non visitÃ© le plus proche. On entre chaque segment par l'extrÃ©mitÃ©
    //    la plus proche du point courant. DÃ©terministe (tie-break par index).
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
        if (segs[a].row != segs[b].row)
            return segs[a].row < segs[b].row;
        return segs[a].lo < segs[b].lo;
    });

    std::vector<char> visited(static_cast<std::size_t>(n), 0);
    bool hasPrev = false;
    PointD prev{};
    // DerniÃ¨re pÃ©nÃ©tration ramenÃ©e dans la rÃ©gion (identique Ã  `prev` sans compensation) :
    // c'est elle qui valide les liaisons, un dÃ©passement de rangÃ©e n'Ã©tant pas un point
    // de la rÃ©gion.
    PointD prevC{};
    int visitedCount = 0;
    int current = -1;
    bool jumpStart = true; // true = on ARRIVE sur ce segment par un dÃ©placement
    std::size_t orderCursor = 0;

    // EntrÃ©e (Â§15) : on dÃ©marre depuis ce point (repÃ¨re des rangÃ©es) et l'on
    // choisit Ã  chaque nouvelle composante le segment non visitÃ© le plus proche.
    const bool hasEntry = params.entry_point.has_value();
    if (hasEntry) {
        prev = rotate({static_cast<double>(params.entry_point->x.value),
                       static_cast<double>(params.entry_point->y.value)},
                      cosA, sinA);
        prevC = prev;
        hasPrev = true;
    }
    // Cap du trajet cousu cachÃ© : au-delÃ , on saute (dÃ©placement Ã  dÃ©couvert).
    const double underpathCap = std::max(6.0 * spacing, 16'000.0);

    // Autoroute d'underpath (Â§15) : points de passage sur le contour RENTRÃ‰ de la
    // rÃ©gion entiÃ¨re -- extÃ©rieur rentrÃ© ET trous agrandis (repÃ¨re des rangÃ©es).
    // Longer ces contours reste intÃ©rieur ; les trous sont contournÃ©s par leurs
    // propres sommets (et non plus seulement par le contour extÃ©rieur : une forme
    // criblÃ©e de trous gardait des centaines de sauts internes). RÃ©gions sans
    // contour exploitable : pas d'underpath (on saute).
    std::vector<PointD> wps;
    if (params.hidden_underpath) {
        if (auto r = geometry::inset_path_set(region, params.underlay_inset); r && !r->empty()) {
            const auto addWaypoints = [&](const geometry::Path& path) {
                if (path.nodes.size() < 3) {
                    return;
                }
                for (const auto& nd : path.nodes) {
                    wps.push_back(rotate(
                        {static_cast<double>(nd.pos.x.value), static_cast<double>(nd.pos.y.value)},
                        cosA, sinA));
                }
            };
            for (const auto& set : *r) {
                addWaypoints(set.outer);
                for (const auto& hole : set.holes) {
                    addWaypoints(hole);
                }
            }
        }
    }
    const auto seglen = [](PointD a, PointD b) {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    };
    // Trajet cachÃ© de a Ã  b par graphe de visibilitÃ© sur les points de passage :
    // plus court chemin (Dijkstra) dont CHAQUE segment est un trajet cousu valide
    // (`connector_invalid`) et dont la longueur reste sous `underpathCap`. Seuls
    // les points de passage de l'ellipse |aw| + |wb| <= cap sont candidats, donc
    // le graphe reste petit. DÃ©terministe : candidats dans l'ordre de crÃ©ation,
    // Ã©galitÃ©s dÃ©partagÃ©es par l'indice. Renvoie [a, pointsâ€¦, b], sinon vide.
    const auto routeHighway = [&](PointD a, PointD b) -> std::vector<PointD> {
        if (wps.empty()) {
            return {};
        }
        std::vector<PointD> nodes{a};
        for (const PointD& w : wps) {
            if (seglen(a, w) + seglen(w, b) <= underpathCap) {
                nodes.push_back(w);
            }
        }
        nodes.push_back(b);
        const std::size_t N = nodes.size();
        if (N == 2) {
            return {};
        }
        constexpr double kInf = std::numeric_limits<double>::max();
        std::vector<double> dist(N, kInf);
        std::vector<int> from(N, -1);
        std::vector<char> done(N, 0);
        dist[0] = 0.0;
        for (std::size_t iter = 0; iter < N; ++iter) {
            std::size_t u = N;
            for (std::size_t i = 0; i < N; ++i) {
                if (!done[i] && dist[i] < kInf && (u == N || dist[i] < dist[u])) {
                    u = i;
                }
            }
            if (u == N || u == N - 1) {
                break;
            }
            done[u] = 1;
            for (std::size_t v = 1; v < N; ++v) {
                if (done[v] || v == u) {
                    continue;
                }
                const double nd = dist[u] + seglen(nodes[u], nodes[v]);
                if (nd > underpathCap || nd >= dist[v]) {
                    continue;
                }
                if (connector_invalid(edgeIndex, nodes[u], nodes[v])) {
                    continue;
                }
                dist[v] = nd;
                from[v] = static_cast<int>(u);
            }
        }
        if (from[N - 1] < 0) {
            return {};
        }
        std::vector<PointD> path;
        for (int i = static_cast<int>(N) - 1; i >= 0; i = from[static_cast<std::size_t>(i)]) {
            path.push_back(nodes[static_cast<std::size_t>(i)]);
        }
        std::reverse(path.begin(), path.end());
        return path;
    };

    while (visitedCount < n) {
        if (current == -1) {
            // Nouvelle composante : reprise par le segment non visitÃ© le plus
            // PROCHE du point courant (entrÃ©e demandÃ©e, ou fin de la
            // composante prÃ©cÃ©dente) -- l'ordre de balayage seul renvoyait Ã 
            // l'autre bout de la forme (audit marine plein cadre : 2 470 sauts
            // internes aux tatami, autant de piqÃ»res nulles d'arrivÃ©e).
            if (hasPrev) {
                int best = -1;
                double bd = std::numeric_limits<double>::max();
                for (int j = 0; j < n; ++j) {
                    if (visited[static_cast<std::size_t>(j)])
                        continue;
                    const double d = std::min(dist2(prev, {segs[j].lo, segs[j].y}),
                                              dist2(prev, {segs[j].hi, segs[j].y}));
                    if (d < bd || (d == bd && (best == -1 || j < best))) {
                        bd = d;
                        best = j;
                    }
                }
                current = best;
            } else {
                while (orderCursor < order.size() &&
                       visited[static_cast<std::size_t>(order[orderCursor])]) {
                    ++orderCursor;
                }
                current = order[orderCursor];
            }
            jumpStart = true; // segment atteint par saut (nouvelle composante)
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
            const PointD rpC{std::clamp(x, s.lo, s.hi), s.y};
            // La liaison vers le premier point d'un segment est un SAUT (aiguille
            // levÃ©e) si l'on n'y est pas arrivÃ© par une arÃªte du graphe, OU si le
            // trajet cousu couperait un bord de la rÃ©gion ou d'un trou. Le
            // chevauchement des rangÃ©es ne donne que des candidats ; la validation
            // gÃ©omÃ©trique (connector_invalid) tranche.
            // `cross` n'est lu que pour le premier point du segment (k == 0 :
            // `jump` l'exige, et la branche underpath exige `jump`) : ne pas
            // payer ce test en O(arÃªtes) Ã  chaque pÃ©nÃ©tration (audit perf
            // 2026-09, docs/performance-audit.md).
            const bool cross =
                (k == 0) && hasPrev && connector_invalid(edgeIndex, prevC, rpC, kLinkEdgeTol);
            bool jump = (k == 0) && (jumpStart || !hasPrev || cross);
            // Liaison COUSUE vers une rangÃ©e voisine (arÃªte du graphe, trajet
            // validÃ© intÃ©rieur) : deux segments qui ne se chevauchent que sur
            // une petite portion peuvent Ãªtre reliÃ©s sur des dizaines de mm
            // (audit marine : points de 12 Ã  62 mm dans le ciel). La liaison
            // est dÃ©coupÃ©e en pÃ©nÃ©trations cachÃ©es d'au plus `stitch_length`.
            const bool longLink = (k == 0) && !jump && hasPrev && seglen(prev, rp) > stitchLen;
            // Underpath cachÃ© (Â§15) : au lieu d'un saut, on coud un trajet cachÃ©.
            // Ã‰met des pÃ©nÃ©trations intermÃ©diaires taguÃ©es `travel` le long du
            // trajet, `rp` restant une pÃ©nÃ©tration normale. `emitTravel` Ã©chantillonne
            // une polyligne (repÃ¨re des rangÃ©es) sans son dernier point (== rp).
            const auto emitTravel = [&](const std::vector<PointD>& poly) {
                for (std::size_t s = 1; s < poly.size(); ++s) {
                    const double d = seglen(poly[s - 1], poly[s]);
                    const int steps = std::max(1, static_cast<int>(std::ceil(d / stitchLen)));
                    const bool lastSeg = (s + 1 == poly.size());
                    for (int t = 1; t <= steps; ++t) {
                        if (lastSeg && t == steps)
                            break; // dernier point == rp
                        const double f = static_cast<double>(t) / steps;
                        const PointD ip{poly[s - 1].x + (poly[s].x - poly[s - 1].x) * f,
                                        poly[s - 1].y + (poly[s].y - poly[s - 1].y) * f};
                        out.push_back({to_um(rotate(ip, cosB, sinB)), false, true});
                    }
                }
            };
            if (longLink) {
                emitTravel({prev, rp});
            }
            if (jump && params.hidden_underpath && hasPrev) {
                // 1) trajet DIRECT s'il reste intÃ©rieur et court ;
                if (!cross && seglen(prevC, rpC) <= underpathCap) {
                    emitTravel({prevC, rpC});
                    jump = false;
                } else if (auto route = routeHighway(prevC, rpC); !route.empty()) {
                    // 2) sinon on longe le contour rentrÃ© (contourne les trous).
                    emitTravel(route);
                    jump = false;
                }
            }
            out.push_back({to_um(rotate(rp, cosB, sinB)), jump, false});
            prev = rp;
            prevC = rpC;
            hasPrev = true;
        }
        visited[static_cast<std::size_t>(current)] = 1;
        ++visitedCount;

        // Prochain voisin non visitÃ©, le plus proche d'une extrÃ©mitÃ©.
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
        jumpStart = false; // atteint par une arÃªte du graphe -> couture
    }
    return out;
}

std::vector<std::vector<Vec2um>> tatami_underlay(const geometry::PathSet& region,
                                                 const document::TatamiParams& params) {
    std::vector<std::vector<Vec2um>> passes;

    // 1) Contour rentrÃ© : running le long du bord (et des trous), retrait
    //    `underlay_inset`. Politique sÃ»re EXPLICITE : si un retrait est demandÃ©
    //    (`underlay_inset` > 0) mais Ã©choue ou fait disparaÃ®tre la forme (piÃ¨ce
    //    trop petite pour ce retrait), on N'Ã‰MET AUCUNE sous-couche de contour
    //    pour cette forme â€” jamais de repli silencieux sur le bord BRUT. Coudre
    //    exactement sur l'arÃªte finale n'offrirait aucune marge de stabilisation
    //    et risquerait de dÃ©border une fois la compensation de bord de la couche
    //    supÃ©rieure appliquÃ©e par-dessus. Un retrait NUL est une intention
    //    explicite distincte (pas un Ã©chec) : le bord brut est alors voulu.
    if (params.underlay_edge) {
        std::vector<geometry::PathSet> shells;
        if (params.underlay_inset.value > 0) {
            if (auto r = geometry::inset_path_set(region, params.underlay_inset);
                r && !r->empty()) {
                shells = std::move(*r);
            }
        } else {
            shells.push_back(region);
        }
        // On ne longe que le contour EXTÃ‰RIEUR rentrÃ© : l'inset d'un trou le
        // rÃ©trÃ©cit (vers l'intÃ©rieur du trou), si bien qu'en longer le bord
        // ferait passer la sous-couche AU-DESSUS du trou. Les bords de trous ne
        // reÃ§oivent donc pas de sous-couche de contour (sÃ»retÃ©).
        for (const auto& shell : shells) {
            if (shell.outer.nodes.size() < 2) {
                continue;
            }
            auto pts = sample_path(shell.outer, params.stitch_length, Micrometers{500});
            if (pts.size() >= 2) {
                passes.push_back(std::move(pts));
            }
        }
    }

    // 2) RangÃ©es perpendiculaires espacÃ©es : on rÃ©utilise le balayage tatami avec
    //    un angle Ã  +90Â° et un grand pas, puis on dÃ©coupe en trajets cousus (on
    //    ignore les sauts). Les options avancÃ©es sont neutralisÃ©es pour Ã©viter
    //    toute rÃ©cursion.
    if (params.underlay_parallel) {
        document::TatamiParams up = params;
        up.angle = Angle{params.angle.radians + std::numbers::pi / 2.0};
        up.row_spacing =
            Micrometers{std::max(params.underlay_spacing.value, params.row_spacing.value)};
        up.inset = Micrometers{0};
        up.pull_compensation = Micrometers{0}; // la sous-couche reste dans la forme
        up.underlay_edge = false;
        up.underlay_parallel = false;
        // Trajets cachÃ©s hÃ©ritÃ©s de l'objet : sous la couche supÃ©rieure, une
        // liaison cousue de sous-couche est invisible (audit marine : 625
        // sauts dans les sous-couches en rangÃ©es).
        up.hidden_underpath = params.hidden_underpath;
        up.entry_point.reset();
        const auto fill = fill_tatami(region, up);
        std::vector<Vec2um> run;
        for (const auto& fs : fill) {
            if (fs.jump) {
                if (run.size() >= 2)
                    passes.push_back(run);
                run.clear();
            }
            run.push_back(fs.pos);
        }
        if (run.size() >= 2)
            passes.push_back(std::move(run));
    }

    return passes;
}

namespace {

// Polygones (extÃ©rieur + trous, >= 3 sommets) dans le repÃ¨re du modÃ¨le.
std::vector<std::vector<PointD>> model_polys(const geometry::PathSet& region) {
    std::vector<std::vector<PointD>> polys;
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<PointD> poly;
        poly.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            poly.push_back(
                {static_cast<double>(node.pos.x.value), static_cast<double>(node.pos.y.value)});
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    addPoly(region.outer);
    for (const auto& hole : region.holes) {
        addPoly(hole);
    }
    return polys;
}

} // namespace

bool segment_stays_in_region(const geometry::PathSet& region, Vec2um a, Vec2um b) {
    return RegionSegmentTester(region).stays_inside(a, b);
}

struct RegionSegmentTester::Impl {
    explicit Impl(const std::vector<std::vector<PointD>>& polys) : index(polys) {}
    EdgeIndex index;
};

RegionSegmentTester::RegionSegmentTester(const geometry::PathSet& region) {
    const auto polys = model_polys(region);
    if (!polys.empty()) {
        impl_ = std::make_unique<Impl>(polys);
    }
}

RegionSegmentTester::~RegionSegmentTester() = default;
RegionSegmentTester::RegionSegmentTester(RegionSegmentTester&&) noexcept = default;
RegionSegmentTester& RegionSegmentTester::operator=(RegionSegmentTester&&) noexcept = default;

bool RegionSegmentTester::stays_inside(Vec2um a, Vec2um b) const {
    if (impl_ == nullptr) {
        return false; // aucun polygone exploitable (cf. segment_stays_in_region)
    }
    const PointD pa{static_cast<double>(a.x.value), static_cast<double>(a.y.value)};
    const PointD pb{static_cast<double>(b.x.value), static_cast<double>(b.y.value)};
    return !connector_invalid(impl_->index, pa, pb);
}

struct HiddenRouter::Impl {
    // Au plus ce nombre de points de passage (les moins dÃ©tournÃ©s) sont retenus par trajet :
    // le graphe de visibilitÃ© reste petit mÃªme sur une forme criblÃ©e de trous.
    static constexpr std::size_t kMaxCandidates = 24;

    Impl(const geometry::PathSet& region_, Micrometers inset_, double cap_um)
        : region(region_), inset(inset_), tester(region_), cap(cap_um) {}

    // Points de passage, calculÃ©s au premier besoin seulement : un trajet droit suffit dans la
    // plupart des cas et le dÃ©calage du contour (Clipper) coÃ»te plus cher que le test.
    void ensure_waypoints() const {
        if (ready) {
            return;
        }
        ready = true;
        const auto add = [](std::vector<Vec2um>& out, const geometry::Path& path) {
            if (path.nodes.size() >= 3) {
                for (const auto& node : path.nodes) {
                    out.push_back(node.pos);
                }
            }
        };
        // Sommets du contour RENTRÃ‰ de la rÃ©gion entiÃ¨re (extÃ©rieur rentrÃ© et trous agrandis).
        // Les longer reste intÃ©rieur et contourne les trous.
        if (auto r = geometry::inset_path_set(region, inset); r && !r->empty()) {
            for (const auto& set : *r) {
                add(waypoints, set.outer);
                for (const auto& hole : set.holes) {
                    add(waypoints, hole);
                }
            }
        }
        // Secours : sommets du bord de la rÃ©gion elle-mÃªme (extÃ©rieur et trous), pour les
        // parties plus Ã©troites que 2 x `inset` oÃ¹ le contour rentrÃ© n'existe pas. Longer le
        // bord reste un trajet valide (`stays_inside` accepte le suivi de frontiÃ¨re).
        add(boundary, region.outer);
        for (const auto& hole : region.holes) {
            add(boundary, hole);
        }
    }

    geometry::PathSet region;
    Micrometers inset;
    RegionSegmentTester tester;
    double cap;
    mutable bool ready{false};
    mutable std::vector<Vec2um> waypoints;
    mutable std::vector<Vec2um> boundary;
};

namespace {

double distance_um(Vec2um a, Vec2um b) {
    return std::hypot(static_cast<double>(a.x.value) - static_cast<double>(b.x.value),
                      static_cast<double>(a.y.value) - static_cast<double>(b.y.value));
}

} // namespace

HiddenRouter::HiddenRouter(const geometry::PathSet& region, Micrometers inset, double cap_um)
    : impl_(std::make_unique<Impl>(region, inset, cap_um)) {}
HiddenRouter::~HiddenRouter() = default;
HiddenRouter::HiddenRouter(HiddenRouter&&) noexcept = default;
HiddenRouter& HiddenRouter::operator=(HiddenRouter&&) noexcept = default;

std::vector<Vec2um> HiddenRouter::route(Vec2um a, Vec2um b) const {
    const double direct = distance_um(a, b);
    if (direct <= 0.0) {
        return {};
    }
    if (direct <= impl_->cap && impl_->tester.stays_inside(a, b)) {
        return {a, b};
    }

    // Plus court chemin (Dijkstra) a -> b par ces points de passage, dont chaque segment reste
    // dans la rÃ©gion. Candidats : ceux dont le dÃ©tour a -> w -> b tient sous le cap, les moins
    // dÃ©tournÃ©s d'abord (Ã©galitÃ©s : ordre de crÃ©ation, donc dÃ©terministe).
    const auto search = [&](const std::vector<Vec2um>& points) -> std::vector<Vec2um> {
        struct Candidate {
            double detour;
            std::size_t index;
        };
        std::vector<Candidate> candidates;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double detour = distance_um(a, points[i]) + distance_um(points[i], b);
            if (detour <= impl_->cap) {
                candidates.push_back({detour, i});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) {
            return l.detour != r.detour ? l.detour < r.detour : l.index < r.index;
        });
        if (candidates.size() > Impl::kMaxCandidates) {
            candidates.resize(Impl::kMaxCandidates);
        }
        if (candidates.empty()) {
            return {};
        }
        std::vector<Vec2um> nodes{a};
        for (const Candidate& c : candidates) {
            nodes.push_back(points[c.index]);
        }
        nodes.push_back(b);
        const std::size_t n = nodes.size();
        const std::size_t last = n - 1;

        // VisibilitÃ© depuis a et vers b, calculÃ©e UNE fois : si l'une des deux extrÃ©mitÃ©s ne
        // voit aucun point de passage, aucun chemin n'existe et l'on s'arrÃªte tout de suite
        // (la majoritÃ© des Ã©checs), sans explorer le graphe.
        std::vector<char> seenFromA(n, 0), seesB(n, 0);
        bool anyFromA = false;
        bool anyToB = false;
        for (std::size_t i = 1; i < last; ++i) {
            seenFromA[i] = impl_->tester.stays_inside(a, nodes[i]) ? 1 : 0;
            seesB[i] = impl_->tester.stays_inside(nodes[i], b) ? 1 : 0;
            anyFromA = anyFromA || seenFromA[i];
            anyToB = anyToB || seesB[i];
        }
        if (!anyFromA || !anyToB) {
            return {};
        }

        // A* (heuristique : distance Ã  b) : les tests de visibilitÃ© entre points de passage ne
        // sont faits que pour les nÅ“uds rÃ©ellement dÃ©veloppÃ©s, et jamais deux fois.
        constexpr double kInf = std::numeric_limits<double>::max();
        std::vector<double> dist(n, kInf);
        std::vector<int> from(n, -1);
        std::vector<char> done(n, 0);
        dist[0] = 0.0;
        for (std::size_t iter = 0; iter < n; ++iter) {
            std::size_t u = n;
            double bestF = kInf;
            for (std::size_t i = 0; i < n; ++i) {
                if (done[i] || dist[i] >= kInf) {
                    continue;
                }
                const double f = dist[i] + distance_um(nodes[i], b);
                if (f < bestF) {
                    bestF = f;
                    u = i;
                }
            }
            if (u == n || u == last) {
                break;
            }
            done[u] = 1;
            for (std::size_t v = 1; v < n; ++v) {
                if (done[v] || v == u) {
                    continue;
                }
                const double nd = dist[u] + distance_um(nodes[u], nodes[v]);
                if (nd > impl_->cap || nd >= dist[v]) {
                    continue;
                }
                const bool visible = u == 0      ? seenFromA[v] != 0
                                     : v == last ? seesB[u] != 0
                                                 : impl_->tester.stays_inside(nodes[u], nodes[v]);
                if (!visible) {
                    continue;
                }
                dist[v] = nd;
                from[v] = static_cast<int>(u);
            }
        }
        if (from[last] < 0) {
            return {};
        }
        std::vector<Vec2um> path;
        for (int i = static_cast<int>(last); i >= 0; i = from[static_cast<std::size_t>(i)]) {
            path.push_back(nodes[static_cast<std::size_t>(i)]);
        }
        std::reverse(path.begin(), path.end());
        return path;
    };

    impl_->ensure_waypoints();
    if (auto path = search(impl_->waypoints); !path.empty()) {
        return path;
    }
    return search(impl_->boundary);
}

} // namespace openstitch::stitch_generation
