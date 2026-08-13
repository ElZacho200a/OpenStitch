// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/offset.hpp"

#include <clipper2/clipper.h>

#include <algorithm>

#include "openstitch/geometry/clean.hpp"

namespace openstitch::geometry {

namespace {

Clipper2Lib::Path64 to_clipper(const Path& path) {
    Clipper2Lib::Path64 out;
    out.reserve(path.nodes.size());
    for (const PathNode& node : path.nodes) {
        out.emplace_back(node.pos.x.value, node.pos.y.value);
    }
    return out;
}

// Réoriente `path` pour que son aire signée ait le signe de `wantPositive`
// (vrai -> CCW, faux -> CW) -- même pattern que `boolean.cpp` (ADR-005 :
// aucun type Clipper2 ne traverse une frontière de fichier, donc ce petit
// helper est dupliqué localement plutôt que partagé). `InflatePaths` offsette
// chaque chemin par rapport à SA PROPRE orientation ; un `PathSet` dont le
// trou partage l'orientation de son extérieur (au lieu de l'opposée -- cf.
// `path.hpp` : « région vectorielle », convention implicite mais jamais
// vérifiée avant ce correctif) fait alors grandir le trou au lieu de le
// réduire (ou l'inverse) lors d'une érosion, produisant une géométrie
// incohérente sans qu'aucune erreur ne remonte (trouvé via
// `satin_coverage::analyze_satin_coverage` sur la fixture `ring` de
// `shapes.cpp`, dont le trou est construit avec la MÊME orientation que
// l'extérieur -- rien d'autre dans le pipeline n'étant sensible à
// l'orientation d'un trou, ce défaut latent était invisible jusqu'ici).
Path oriented_as(const Path& path, bool wantPositive) {
    if ((signed_area_um2(path) >= 0.0) == wantPositive) {
        return path;
    }
    Path reversed = path;
    std::reverse(reversed.nodes.begin(), reversed.nodes.end());
    return reversed;
}

}  // namespace

Result<std::vector<PathSet>> inset_path_set(const PathSet& set, Micrometers delta) {
    if (delta.value == 0) {
        return std::vector<PathSet>{set};
    }

    Clipper2Lib::Paths64 subject;
    subject.push_back(to_clipper(oriented_as(set.outer, true)));
    for (const Path& hole : set.holes) {
        subject.push_back(to_clipper(oriented_as(hole, false)));
    }

    // delta > 0 = retrait intérieur -> offset négatif au sens Clipper.
    const auto solution = Clipper2Lib::InflatePaths(
        subject, static_cast<double>(-delta.value), Clipper2Lib::JoinType::Miter,
        Clipper2Lib::EndType::Polygon);

    // On repasse par le nettoyage pour reconstruire la hiérarchie trous/extérieur.
    std::vector<Path> raw;
    raw.reserve(solution.size());
    for (const auto& poly : solution) {
        Path path;
        path.closed = true;
        path.nodes.reserve(poly.size());
        for (const auto& pt : poly) {
            path.nodes.push_back(PathNode{
                Vec2um{Micrometers{static_cast<std::int32_t>(pt.x)},
                       Micrometers{static_cast<std::int32_t>(pt.y)}},
                NodeType::Corner, std::nullopt, std::nullopt});
        }
        raw.push_back(std::move(path));
    }
    return clean_to_path_sets(raw);
}

}  // namespace openstitch::geometry
