// SPDX-License-Identifier: Apache-2.0
#include "pen_shape.hpp"

#include <algorithm>
#include <cmath>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/geometry/reshape.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr double kMaxCoordMm = 1000.0;
constexpr double kMinAreaMm2 = 1.0;
constexpr double kMinGapMm = 0.05; // deux nœuds plus proches sont confondus
constexpr Micrometers kCrossingTolerance{50};

[[nodiscard]] Vec2um toUm(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

[[nodiscard]] bool inRange(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= kMaxCoordMm &&
           std::abs(p.y) <= kMaxCoordMm;
}

PenResult refuse(std::string why) {
    PenResult r;
    r.message = std::move(why);
    return r;
}

} // namespace

PenResult createPenShape(DocumentSession& session, const std::vector<PenNode>& nodes) {
    if (nodes.size() < 3) {
        return refuse("Une forme demande au moins trois nœuds.");
    }
    if (nodes.size() > 2000) {
        return refuse("Trop de nœuds (2000 au maximum).");
    }
    geometry::Path path;
    path.closed = true;
    bool curved = false;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!inRange(n.pos) || (n.handleOut && !inRange(*n.handleOut))) {
            return refuse("Un nœud est hors de la zone de travail (±1000 mm).");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (std::hypot(nodes[j].pos.x - n.pos.x, nodes[j].pos.y - n.pos.y) < kMinGapMm) {
                return refuse("Deux nœuds sont confondus : écartez-les.");
            }
        }
        geometry::PathNode node;
        node.pos = toUm(n.pos.x, n.pos.y);
        if (n.handleOut &&
            std::hypot(n.handleOut->x - n.pos.x, n.handleOut->y - n.pos.y) > kMinGapMm) {
            const double dx = n.handleOut->x - n.pos.x;
            const double dy = n.handleOut->y - n.pos.y;
            node.type = geometry::NodeType::Smooth;
            node.tan_out = toUm(dx, dy);
            node.tan_in = toUm(-dx, -dy); // tangente symétrique : nœud lisse
            curved = true;
        }
        path.nodes.push_back(node);
    }
    if (std::abs(geometry::signed_area_um2(path)) / 1e6 < kMinAreaMm2) {
        return refuse("Forme trop petite ou dégénérée (aire inférieure à 1 mm²).");
    }
    if (geometry::path_self_intersects(path, kCrossingTolerance)) {
        return refuse("Le contour se croise : déplacez un nœud pour le dégager.");
    }

    // Même recette que l'opération `add_shape` : objet vectoriel + objet de broderie (tatami),
    // posés par une seule commande. Les identifiants viennent du compteur du document.
    document::VectorObject vector;
    vector.id = session.allocateObjectId();
    vector.name = curved ? "Courbe" : "Forme";
    vector.rgb = {80, 120, 200};
    vector.paths.push_back(geometry::PathSet{std::move(path), {}});

    document::EmbroideryObject object;
    object.id = session.allocateObjectId();
    object.name = vector.name;
    object.source_vector = vector.id;
    object.rgb = vector.rgb;
    object.intent = document::EmbroideryIntent::ForcedUserChoice;
    object.params = document::TatamiParams{};

    const auto objectId = object.id.value;
    session.execute(std::make_unique<commands::AddObjectBatchCommand>(
        std::vector<document::VectorObject>{std::move(vector)},
        std::vector<document::EmbroideryObject>{std::move(object)},
        "Ajouter une forme à la plume"));
    PenResult r;
    r.ok = true;
    r.objectId = objectId;
    r.curved = curved;
    return r;
}

} // namespace openstitch::desktop_v2
