// SPDX-License-Identifier: Apache-2.0
#include "satin_column.hpp"

#include <cmath>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr double kMaxCoordMm = 1000.0;
constexpr std::size_t kMaxRungs = 500;

ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}

bool inRange(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) <= kMaxCoordMm &&
           std::fabs(p.y) <= kMaxCoordMm;
}

Vec2um toUm(PointMm p) {
    return {to_micrometers(Millimeters{p.x}), to_micrometers(Millimeters{p.y})};
}

const document::SatinParams* satinOf(const document::Project& project, std::uint64_t id) {
    const auto* obj = project.findEmbroidery(ObjectId{id});
    return obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
}

const geometry::Path& railOf(const document::SatinParams& p, RailSide side) {
    return side == RailSide::A ? p.rail_a : p.rail_b;
}

commands::SatinRailSide engineSide(RailSide side) {
    return side == RailSide::A ? commands::SatinRailSide::RailA : commands::SatinRailSide::RailB;
}

SatinCreateResult failure(std::string why) {
    return {false, std::move(why), 0, false};
}
} // namespace

SatinCreateResult createSatinColumn(DocumentSession& session,
                                    const std::vector<std::pair<PointMm, PointMm>>& rungs) {
    if (rungs.size() < 2) {
        return failure("Une colonne satin demande au moins deux paires de points.");
    }
    if (rungs.size() > kMaxRungs) {
        return failure("Trop de barreaux (500 au maximum).");
    }
    geometry::Path railA;
    geometry::Path railB;
    railA.closed = false;
    railB.closed = false;
    std::vector<document::SatinRung> rungList;
    for (const auto& [a, b] : rungs) {
        if (!inRange(a) || !inRange(b)) {
            return failure("Point hors limites (±1000 mm).");
        }
        const Vec2um ua = toUm(a);
        const Vec2um ub = toUm(b);
        if (ua == ub) {
            return failure("Les deux rails se touchent : écartez les points d'une paire.");
        }
        railA.nodes.push_back(geometry::PathNode{ua, geometry::NodeType::Corner, {}, {}});
        railB.nodes.push_back(geometry::PathNode{ub, geometry::NodeType::Corner, {}, {}});
        rungList.push_back(document::SatinRung{ua, ub, std::nullopt});
    }
    const auto flatA = geometry::flatten(railA, Micrometers{50});
    const auto flatB = geometry::flatten(railB, Micrometers{50});
    const bool cross = geometry::polylines_cross(flatA.points, flatB.points);

    // Contour source synthétique (rail A aller, rail B retour) : seulement le rattachement
    // `source_vector` exigé par l'objet ; les rails de SatinParams restent la géométrie réelle.
    geometry::Path outline;
    outline.closed = true;
    outline.nodes = railA.nodes;
    for (auto it = railB.nodes.rbegin(); it != railB.nodes.rend(); ++it) {
        outline.nodes.push_back(*it);
    }
    auto& ids = session.projectForIdAllocation().object_ids;
    document::VectorObject vec;
    vec.id = ids.next();
    vec.name = "Colonne satin (rails)";
    vec.rgb = {200, 90, 40};
    vec.visible = false;
    vec.paths.push_back(geometry::PathSet{std::move(outline), {}});

    document::EmbroideryObject emb;
    emb.id = ids.next();
    emb.name = "Colonne satin manuelle";
    emb.source_vector = vec.id;
    emb.rgb = {200, 90, 40};
    document::SatinParams params;
    params.rail_a = std::move(railA);
    params.rail_b = std::move(railB);
    params.rungs = std::move(rungList);
    params.split_stitch = document::SatinSplit::Staggered;
    emb.params = std::move(params);
    const auto newId = emb.id.value;

    std::vector<document::VectorObject> vectors;
    vectors.push_back(std::move(vec));
    std::vector<document::EmbroideryObject> embs;
    embs.push_back(std::move(emb));
    session.execute(std::make_unique<commands::AddObjectBatchCommand>(
        std::move(vectors), std::move(embs), "Colonne satin (création manuelle)"));
    return {true, {}, newId, cross};
}

int railNodeCount(const document::Project& project, std::uint64_t objectId, RailSide side) {
    const auto* satin = satinOf(project, objectId);
    return satin == nullptr ? -1 : static_cast<int>(railOf(*satin, side).nodes.size());
}

ServiceResult moveSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                std::size_t node, PointMm to) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    const auto& rail = railOf(*satin, side);
    if (node >= rail.nodes.size()) {
        return refuse("Nœud inconnu.");
    }
    if (!inRange(to)) {
        return refuse("Position hors limites (±1000 mm).");
    }
    session.execute(std::make_unique<commands::MoveSatinRailNodeCommand>(
        ObjectId{objectId}, engineSide(side), node, rail.nodes[node].pos, toUm(to)));
    return {true, {}};
}

ServiceResult insertSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                  std::size_t segment, double t) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    const auto& rail = railOf(*satin, side);
    if (rail.nodes.size() < 2) {
        return refuse("Segment inconnu.");
    }
    const std::size_t segments = rail.closed ? rail.nodes.size() : rail.nodes.size() - 1;
    if (segment >= segments) {
        return refuse("Segment inconnu.");
    }
    if (!(t > 0.0 && t < 1.0)) {
        return refuse("Le nœud doit tomber à l'intérieur du segment (0 < t < 1).");
    }
    session.execute(std::make_unique<commands::InsertSatinRailNodeCommand>(
        ObjectId{objectId}, engineSide(side), segment, t));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
