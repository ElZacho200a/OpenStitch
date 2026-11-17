// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/frontier.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>

namespace openstitch::geometry {

namespace {

// Helpers dupliqués localement (aucun type Clipper ne traverse une frontière de fichier,
// cf. ADR-005 et boolean.cpp).
Clipper2Lib::Path64 to_clipper_points(const Path& rawPath) {
    const Path path = flatten_to_polygon(rawPath, Micrometers{20});
    Clipper2Lib::Path64 out;
    out.reserve(path.nodes.size() + 1);
    for (const PathNode& node : path.nodes) {
        out.emplace_back(node.pos.x.value, node.pos.y.value);
    }
    return out;
}

Path from_clipper(const Clipper2Lib::Path64& path) {
    Path out;
    out.closed = true;
    out.nodes.reserve(path.size());
    for (const auto& pt : path) {
        out.nodes.push_back(PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(pt.x)},
                                            Micrometers{static_cast<std::int32_t>(pt.y)}},
                                     NodeType::Corner, std::nullopt, std::nullopt});
    }
    return out;
}

void collect(const Clipper2Lib::PolyPath64& outer, std::vector<PathSet>& out) {
    PathSet set;
    set.outer = from_clipper(outer.Polygon());
    for (const auto& hole : outer) {
        set.holes.push_back(from_clipper(hole->Polygon()));
    }
    out.push_back(std::move(set));
    for (const auto& hole : outer) {
        for (const auto& nested : *hole) {
            collect(*nested, out);
        }
    }
}

// Contour orienté pour NonZero : extérieur CCW (aire > 0), trous CW.
Clipper2Lib::Path64 oriented(const Path& path, bool wantPositive) {
    Clipper2Lib::Path64 pts = to_clipper_points(path);
    if ((Clipper2Lib::Area(pts) >= 0.0) != wantPositive) {
        std::reverse(pts.begin(), pts.end());
    }
    return pts;
}

Vec2um to_vec(const Clipper2Lib::Point64& p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(p.x)},
                  Micrometers{static_cast<std::int32_t>(p.y)}};
}

// Recolle les morceaux dont un bout coïncide exactement avec un bout d'un autre (coupure au
// point de départ d'un contour fermé). Déterministe : parcours dans l'ordre des indices.
std::vector<std::vector<Vec2um>> join_pieces(std::vector<std::vector<Vec2um>> pieces) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < pieces.size() && !merged; ++i) {
            for (std::size_t j = 0; j < pieces.size() && !merged; ++j) {
                if (i == j || pieces[i].empty() || pieces[j].empty()) {
                    continue;
                }
                auto& a = pieces[i];
                auto& b = pieces[j];
                if (a.back() == b.front()) {
                    a.insert(a.end(), b.begin() + 1, b.end());
                } else if (a.back() == b.back()) {
                    a.insert(a.end(), b.rbegin() + 1, b.rend());
                } else {
                    continue;
                }
                pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(j));
                merged = true;
            }
        }
    }
    return pieces;
}

} // namespace

Result<std::vector<Polyline>> shared_frontier(const PathSet& a, const PathSet& b,
                                              Micrometers tolerance, Micrometers min_length) {
    // Zone de contact : `b` grossie de la tolérance (trous de `b` rétrécis d'autant).
    Clipper2Lib::Paths64 bPaths;
    if (b.outer.nodes.size() >= 3) {
        bPaths.push_back(oriented(b.outer, true));
    }
    for (const auto& hole : b.holes) {
        if (hole.nodes.size() >= 3) {
            bPaths.push_back(oriented(hole, false));
        }
    }
    if (bPaths.empty() || a.outer.nodes.size() < 3) {
        return std::vector<Polyline>{};
    }
    const double grow = static_cast<double>(std::max<std::int32_t>(0, tolerance.value));
    const Clipper2Lib::Paths64 contact =
        grow > 0.0 ? Clipper2Lib::InflatePaths(bPaths, grow, Clipper2Lib::JoinType::Round,
                                               Clipper2Lib::EndType::Polygon)
                   : bPaths;
    if (contact.empty()) {
        return std::vector<Polyline>{};
    }

    // Bords de `a` en chemins ouverts (boucle refermée sur son premier point).
    Clipper2Lib::Paths64 edges;
    const auto addEdge = [&](const Path& path) {
        if (path.nodes.size() < 3) {
            return;
        }
        Clipper2Lib::Path64 pts = to_clipper_points(path);
        pts.push_back(pts.front());
        edges.push_back(std::move(pts));
    };
    addEdge(a.outer);
    for (const auto& hole : a.holes) {
        addEdge(hole);
    }

    Clipper2Lib::Clipper64 clipper;
    clipper.AddOpenSubject(edges);
    clipper.AddClip(contact);
    Clipper2Lib::Paths64 closedOut;
    Clipper2Lib::Paths64 openOut;
    if (!clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero,
                         closedOut, openOut)) {
        return fail(ErrorCategory::Internal, "Échec du calcul de la frontière commune",
                    "Clipper64::Execute (chemins ouverts) a renvoyé false");
    }

    std::vector<std::vector<Vec2um>> pieces;
    for (const auto& piece : openOut) {
        if (piece.size() < 2) {
            continue;
        }
        std::vector<Vec2um> pts;
        pts.reserve(piece.size());
        for (const auto& p : piece) {
            const Vec2um v = to_vec(p);
            if (pts.empty() || pts.back() != v) {
                pts.push_back(v);
            }
        }
        if (pts.size() >= 2) {
            pieces.push_back(std::move(pts));
        }
    }
    pieces = join_pieces(std::move(pieces));

    std::vector<Polyline> out;
    for (auto& pts : pieces) {
        // La tolérance ajoute, à chaque bout d'une frontière ouverte, une amorce le long du
        // contour EXTÉRIEUR (là où le bord de `a` tourne mais reste à moins de `tolerance` de
        // `b`). On rogne chaque bout de `tolerance` le long de la polyligne : l'amorce
        // disparaît et la frontière réelle ne perd au plus que la largeur du jour.
        if (pts.size() >= 2 && pts.front() != pts.back() && grow > 0.0) {
            const double len = polyline_length(pts);
            if (len - 2.0 * grow >= static_cast<double>(min_length.value)) {
                const auto cum = cumulative_lengths(pts);
                std::vector<Vec2um> trimmed;
                trimmed.push_back(point_at_length(pts, cum, grow));
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    if (cum[i] > grow && cum[i] < len - grow && pts[i] != trimmed.back()) {
                        trimmed.push_back(pts[i]);
                    }
                }
                const Vec2um last = point_at_length(pts, cum, len - grow);
                if (last != trimmed.back()) {
                    trimmed.push_back(last);
                }
                pts = std::move(trimmed);
            }
        }
        if (pts.size() < 2 || polyline_length(pts) < static_cast<double>(min_length.value)) {
            continue;
        }
        out.push_back(Polyline{std::move(pts), false});
    }
    std::sort(out.begin(), out.end(), [](const Polyline& l, const Polyline& r) {
        const double ll = polyline_length(l.points);
        const double lr = polyline_length(r.points);
        if (ll != lr) {
            return ll > lr;
        }
        const Vec2um a0 = l.points.front();
        const Vec2um b0 = r.points.front();
        return std::tie(a0.x.value, a0.y.value) < std::tie(b0.x.value, b0.y.value);
    });
    return out;
}

Result<std::vector<PathSet>> polyline_band(const std::vector<Polyline>& lines,
                                           Micrometers half_width) {
    if (lines.empty() || half_width.value <= 0) {
        return std::vector<PathSet>{};
    }
    Clipper2Lib::Paths64 open;
    for (const auto& line : lines) {
        if (line.points.size() < 2) {
            continue;
        }
        Clipper2Lib::Path64 pts;
        pts.reserve(line.points.size());
        for (const Vec2um p : line.points) {
            pts.emplace_back(p.x.value, p.y.value);
        }
        open.push_back(std::move(pts));
    }
    if (open.empty()) {
        return std::vector<PathSet>{};
    }
    const Clipper2Lib::Paths64 inflated =
        Clipper2Lib::InflatePaths(open, static_cast<double>(half_width.value),
                                  Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Round);
    if (inflated.empty()) {
        return std::vector<PathSet>{};
    }
    Clipper2Lib::Clipper64 clipper;
    clipper.AddSubject(inflated);
    Clipper2Lib::PolyTree64 tree;
    if (!clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, tree)) {
        return fail(ErrorCategory::Internal, "Échec du calcul de la bande de fondu",
                    "Clipper64::Execute (union) a renvoyé false");
    }
    std::vector<PathSet> out;
    for (const auto& top : tree) {
        collect(*top, out);
    }
    return out;
}

} // namespace openstitch::geometry
