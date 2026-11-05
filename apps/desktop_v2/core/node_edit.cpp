// SPDX-License-Identifier: Apache-2.0
#include "node_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

#include "document_session.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/node_transform.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/reshape.hpp"
#include "satin_column.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr Micrometers kFlattenTolerance{100}; // 0,1 mm pour l'aperçu
constexpr Micrometers kCrossingTolerance{50}; // même tolérance que le bureau historique
constexpr double kSoftRadiusUm = 6'000.0;     // transition adoucie : 6 mm
constexpr double kMaxCoordMm = 1000.0;

[[nodiscard]] Vec2um toUm(PointMm p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y * 1000.0))}};
}

[[nodiscard]] PointMm toMm(Vec2um p) {
    return PointMm{p.x.value / 1000.0, p.y.value / 1000.0};
}

[[nodiscard]] std::vector<PointMm> outline(const geometry::Path& path) {
    std::vector<PointMm> out;
    const auto line = geometry::flatten(path, kFlattenTolerance);
    out.reserve(line.points.size() + 1);
    for (const auto& p : line.points) {
        out.push_back(toMm(p));
    }
    if (path.closed && !out.empty()) {
        out.push_back(out.front()); // fermer visiblement le tracé
    }
    return out;
}

[[nodiscard]] const document::VectorObject* sourceOf(const document::Project& project,
                                                     std::uint64_t objectId) {
    const auto* object = project.findEmbroidery(ObjectId{objectId});
    if (object == nullptr) {
        return nullptr;
    }
    return project.findObject(object->source_vector);
}

[[nodiscard]] bool sameChain(const std::vector<NodeRef>& nodes) {
    return !nodes.empty() && std::all_of(nodes.begin(), nodes.end(), [&](const NodeRef& n) {
        return n.set == nodes.front().set && n.path == nodes.front().path;
    });
}

[[nodiscard]] const document::SatinParams* satinOf(const document::Project& project,
                                                   std::uint64_t objectId) {
    const auto* object = project.findEmbroidery(ObjectId{objectId});
    return object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
}

[[nodiscard]] const geometry::Path& railOf(const document::SatinParams& s, std::size_t set) {
    return set == 0 ? s.rail_a : s.rail_b;
}

[[nodiscard]] commands::SatinRailSide sideOf(std::size_t set) {
    return set == 0 ? commands::SatinRailSide::RailA : commands::SatinRailSide::RailB;
}

// Nouvelles poignées d'un nœud quand sa poignée `isOut` passe à `rel` (relative, µm) : un nœud
// Lisse garde la poignée opposée colinéaire, avec sa longueur (ou la même longueur si elle
// n'existait pas).
struct HandlePair {
    std::optional<Vec2um> in;
    std::optional<Vec2um> out;
};

[[nodiscard]] HandlePair handlesAfter(const geometry::PathNode& n, bool isOut, Vec2um rel) {
    HandlePair h{n.tan_in, n.tan_out};
    (isOut ? h.out : h.in) = rel;
    if (n.type == geometry::NodeType::Smooth) {
        const auto& other = isOut ? n.tan_in : n.tan_out;
        const double len =
            std::hypot(static_cast<double>(rel.x.value), static_cast<double>(rel.y.value));
        if (len > 0.0) {
            const double otherLen = other ? std::hypot(static_cast<double>(other->x.value),
                                                       static_cast<double>(other->y.value))
                                          : len;
            const Vec2um mirrored{
                Micrometers{static_cast<std::int32_t>(std::lround(-rel.x.value / len * otherLen))},
                Micrometers{static_cast<std::int32_t>(std::lround(-rel.y.value / len * otherLen))}};
            (isOut ? h.in : h.out) = mirrored;
        }
    }
    return h;
}

[[nodiscard]] bool withinBounds(PointMm d) {
    return std::abs(d.x) <= kMaxCoordMm && std::abs(d.y) <= kMaxCoordMm;
}

} // namespace

Result<NodeView> nodeView(const document::Project& project, std::uint64_t objectId) {
    if (const auto* satin = satinOf(project, objectId);
        satin != nullptr && !satin->rail_a.nodes.empty() && !satin->rail_b.nodes.empty()) {
        NodeView view;
        view.satin = true;
        view.recommendedMaxMm = satin->max_width.value / 1000.0;
        for (std::size_t side = 0; side < 2; ++side) {
            const auto& rail = railOf(*satin, side);
            view.outlines.push_back(outline(rail));
            for (std::size_t n = 0; n < rail.nodes.size(); ++n) {
                const auto& node = rail.nodes[n];
                view.nodes.push_back({{side, 0, n},
                                      toMm(node.pos),
                                      node.type == geometry::NodeType::Smooth,
                                      node.tan_in.has_value() || node.tan_out.has_value(),
                                      !rail.closed});
                for (const bool isOut : {false, true}) {
                    const auto& tan = isOut ? node.tan_out : node.tan_in;
                    if (tan) {
                        const Vec2um abs{Micrometers{node.pos.x.value + tan->x.value},
                                         Micrometers{node.pos.y.value + tan->y.value}};
                        view.tangents.push_back({{side, 0, n}, isOut, toMm(abs), toMm(node.pos)});
                    }
                }
            }
        }
        bool first = true;
        for (const auto& rung : satin->rungs) {
            view.rungs.emplace_back(toMm(rung.a), toMm(rung.b));
            const double w =
                std::hypot(rung.a.x.value - rung.b.x.value, rung.a.y.value - rung.b.y.value) /
                1000.0;
            view.widthMinMm = first ? w : std::min(view.widthMinMm, w);
            view.widthMaxMm = first ? w : std::max(view.widthMaxMm, w);
            first = false;
        }
        return view;
    }
    const auto* vector = sourceOf(project, objectId);
    if (vector == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Cet objet n'a pas de contour éditable",
                    "Seuls les objets issus d'une forme vectorielle exposent leurs nœuds.");
    }
    NodeView view;
    view.vectorId = vector->id.value;
    for (std::size_t s = 0; s < vector->paths.size(); ++s) {
        const auto& set = vector->paths[s];
        for (std::size_t p = 0; p <= set.holes.size(); ++p) {
            const auto& path = p == 0 ? set.outer : set.holes[p - 1];
            view.outlines.push_back(outline(path));
            for (std::size_t n = 0; n < path.nodes.size(); ++n) {
                const auto& node = path.nodes[n];
                view.nodes.push_back({{s, p, n},
                                      toMm(node.pos),
                                      node.type == geometry::NodeType::Smooth,
                                      node.tan_in.has_value() || node.tan_out.has_value(),
                                      !path.closed});
                for (const bool isOut : {false, true}) {
                    const auto& tan = isOut ? node.tan_out : node.tan_in;
                    if (tan) {
                        const Vec2um abs{Micrometers{node.pos.x.value + tan->x.value},
                                         Micrometers{node.pos.y.value + tan->y.value}};
                        view.tangents.push_back({{s, p, n}, isOut, toMm(abs), toMm(node.pos)});
                    }
                }
            }
        }
    }
    return view;
}

std::string satinWidthSummary(const NodeView& view) {
    if (!view.satin || view.rungs.empty()) {
        return {};
    }
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "Largeur du satin : %.1f à %.1f mm", view.widthMinMm,
                  view.widthMaxMm);
    std::string out = buffer;
    if (view.recommendedMaxMm > 0.0 && view.widthMaxMm > view.recommendedMaxMm) {
        std::snprintf(buffer, sizeof buffer,
                      " (au-delà de %.1f mm : le fil risque de ballonner, un tatami convient "
                      "mieux)",
                      view.recommendedMaxMm);
        out += buffer;
    }
    return out;
}

std::optional<std::vector<std::vector<PointMm>>> previewNodeMove(const document::Project& project,
                                                                 std::uint64_t objectId,
                                                                 const std::vector<NodeRef>& nodes,
                                                                 PointMm deltaMm, bool soft) {
    if (const auto* satin = satinOf(project, objectId); satin != nullptr) {
        if (!sameChain(nodes) || !withinBounds(deltaMm) || nodes.front().set > 1) {
            return std::nullopt;
        }
        geometry::Path rails[2] = {satin->rail_a, satin->rail_b};
        auto& moving = rails[nodes.front().set];
        for (const auto& n : nodes) {
            if (n.node >= moving.nodes.size()) {
                return std::nullopt;
            }
            auto& pos = moving.nodes[n.node].pos;
            pos.x.value += toUm(deltaMm).x.value;
            pos.y.value += toUm(deltaMm).y.value;
        }
        return std::vector<std::vector<PointMm>>{outline(rails[0]), outline(rails[1])};
    }
    const auto* vector = sourceOf(project, objectId);
    if (vector == nullptr || !sameChain(nodes) || !withinBounds(deltaMm)) {
        return std::nullopt;
    }
    const auto& first = nodes.front();
    if (first.set >= vector->paths.size()) {
        return std::nullopt;
    }
    std::vector<std::size_t> indices;
    for (const auto& n : nodes) {
        indices.push_back(n.node);
    }
    geometry::NodeTransform t;
    t.translation = toUm(deltaMm);
    const auto moved =
        geometry::transform_path_set_nodes(vector->paths[first.set], first.path, indices, t,
                                           soft ? kSoftRadiusUm : 0.0, kCrossingTolerance);
    if (!moved) {
        return std::nullopt;
    }
    std::vector<std::vector<PointMm>> out;
    for (std::size_t s = 0; s < vector->paths.size(); ++s) {
        const auto& set = s == first.set ? *moved : vector->paths[s];
        out.push_back(outline(set.outer));
        for (const auto& hole : set.holes) {
            out.push_back(outline(hole));
        }
    }
    return out;
}

ServiceResult moveNodes(DocumentSession& session, std::uint64_t objectId,
                        const std::vector<NodeRef>& nodes, PointMm deltaMm, bool soft) {
    if (const auto* satin = satinOf(session.project(), objectId); satin != nullptr) {
        if (!sameChain(nodes) || nodes.front().set > 1) {
            return {false, "Sélectionnez des nœuds d'un même rail."};
        }
        if (!withinBounds(deltaMm)) {
            return {false, "Déplacement hors de la zone de travail."};
        }
        const auto& rail = railOf(*satin, nodes.front().set);
        const Vec2um shift = toUm(deltaMm);
        auto composite = std::make_unique<commands::CompositeCommand>(
            nodes.size() == 1 ? "Déplacer un nœud de rail satin" : "Déplacer des nœuds de rail");
        for (const auto& n : nodes) {
            if (n.node >= rail.nodes.size()) {
                return {false, "Nœud inconnu."};
            }
            const Vec2um from = rail.nodes[n.node].pos;
            const Vec2um to{Micrometers{from.x.value + shift.x.value},
                            Micrometers{from.y.value + shift.y.value}};
            composite->add(std::make_unique<commands::MoveSatinRailNodeCommand>(
                ObjectId{objectId}, sideOf(nodes.front().set), n.node, from, to));
        }
        session.execute(std::move(composite));
        return {true, {}};
    }
    const auto* vector = sourceOf(session.project(), objectId);
    if (vector == nullptr) {
        return {false, "Cet objet n'a pas de contour éditable."};
    }
    if (!sameChain(nodes)) {
        return {false, "Sélectionnez des nœuds d'un même contour."};
    }
    if (!withinBounds(deltaMm)) {
        return {false, "Déplacement hors de la zone de travail."};
    }
    geometry::NodeTransform t;
    t.translation = toUm(deltaMm);
    std::vector<std::size_t> indices;
    for (const auto& n : nodes) {
        indices.push_back(n.node);
    }
    auto made = commands::make_transform_nodes_command(session.project(), vector->id,
                                                       nodes.front().set, nodes.front().path,
                                                       indices, t, soft ? kSoftRadiusUm : 0.0);
    if (!made.command) {
        return {false, made.error.empty() ? "Déplacement impossible." : made.error};
    }
    session.execute(std::move(made.command));
    return {true, {}};
}

std::optional<SegmentHit> nearestSegment(const NodeView& view, PointMm at, double maxDistMm) {
    std::optional<SegmentHit> best;
    double bestDist = maxDistMm;
    for (std::size_t i = 0; i < view.nodes.size(); ++i) {
        const auto& a = view.nodes[i];
        // Nœud suivant du même chemin, ou premier du chemin pour le dernier nœud.
        const NodeHandle* b = nullptr;
        if (i + 1 < view.nodes.size() && view.nodes[i + 1].ref.set == a.ref.set &&
            view.nodes[i + 1].ref.path == a.ref.path) {
            b = &view.nodes[i + 1];
        } else if (a.open) {
            continue; // dernier nœud d'un rail ouvert : pas de segment de retour
        } else {
            std::size_t k = i;
            while (k > 0 && view.nodes[k - 1].ref.set == a.ref.set &&
                   view.nodes[k - 1].ref.path == a.ref.path) {
                --k;
            }
            b = &view.nodes[k];
        }
        if (b == &a) {
            continue;
        }
        const double dx = b->pos.x - a.pos.x;
        const double dy = b->pos.y - a.pos.y;
        const double len2 = dx * dx + dy * dy;
        if (len2 <= 0.0) {
            continue;
        }
        const double t =
            std::clamp(((at.x - a.pos.x) * dx + (at.y - a.pos.y) * dy) / len2, 0.0, 1.0);
        const double d = std::hypot(at.x - (a.pos.x + t * dx), at.y - (a.pos.y + t * dy));
        if (d <= bestDist && t > 0.02 && t < 0.98) {
            bestDist = d;
            best = SegmentHit{a.ref, t};
        }
    }
    return best;
}

ServiceResult insertNode(DocumentSession& session, std::uint64_t objectId, const SegmentHit& hit) {
    if (satinOf(session.project(), objectId) != nullptr) {
        return insertSatinRailNode(session, objectId,
                                   hit.start.set == 0 ? RailSide::A : RailSide::B, hit.start.node,
                                   hit.t);
    }
    const auto* vector = sourceOf(session.project(), objectId);
    if (vector == nullptr) {
        return {false, "Cet objet n'a pas de contour éditable."};
    }
    auto made = commands::make_insert_node_command(session.project(), vector->id, hit.start.set,
                                                   hit.start.path, hit.start.node, hit.t);
    if (!made.command) {
        return {false, made.error.empty() ? "Insertion impossible." : made.error};
    }
    session.execute(std::move(made.command));
    return {true, {}};
}

std::optional<SegmentHit> curveSegmentAt(const document::Project& project, std::uint64_t objectId,
                                         PointMm at, double maxDistMm) {
    if (satinOf(project, objectId) != nullptr) {
        return std::nullopt;
    }
    const auto* vector = sourceOf(project, objectId);
    if (vector == nullptr) {
        return std::nullopt;
    }
    std::optional<SegmentHit> best;
    double bestDistance = maxDistMm * 1000.0;
    for (std::size_t s = 0; s < vector->paths.size(); ++s) {
        const auto& set = vector->paths[s];
        for (std::size_t p = 0; p <= set.holes.size(); ++p) {
            const auto& path = p == 0 ? set.outer : set.holes[p - 1];
            const auto hit = geometry::nearest_segment(path, toUm(at), bestDistance);
            if (hit && hit->distance_um <= bestDistance) {
                bestDistance = hit->distance_um;
                best = SegmentHit{{s, p, hit->segment}, hit->t};
            }
        }
    }
    return best;
}

std::optional<std::vector<std::vector<PointMm>>>
previewSegmentReshape(const document::Project& project, std::uint64_t objectId,
                      const SegmentHit& hit, PointMm deltaMm) {
    const auto* vector = sourceOf(project, objectId);
    if (vector == nullptr || hit.start.set >= vector->paths.size() || !withinBounds(deltaMm)) {
        return std::nullopt;
    }
    const auto moved = geometry::reshape_path_set_segment(vector->paths[hit.start.set],
                                                          hit.start.path, hit.start.node, hit.t,
                                                          toUm(deltaMm), kCrossingTolerance);
    if (!moved) {
        return std::nullopt;
    }
    std::vector<std::vector<PointMm>> out;
    for (std::size_t s = 0; s < vector->paths.size(); ++s) {
        const auto& set = s == hit.start.set ? *moved : vector->paths[s];
        out.push_back(outline(set.outer));
        for (const auto& hole : set.holes) {
            out.push_back(outline(hole));
        }
    }
    return out;
}

namespace {
[[nodiscard]] std::vector<ObjectId> vectorIdsOf(const document::Project& project,
                                                const std::vector<std::uint64_t>& objectIds) {
    std::vector<ObjectId> out;
    for (const auto id : objectIds) {
        const auto* vector = sourceOf(project, id);
        if (vector != nullptr && std::find(out.begin(), out.end(), vector->id) == out.end()) {
            out.push_back(vector->id);
        }
    }
    return out;
}
} // namespace

ServiceResult convertToCurves(DocumentSession& session,
                              const std::vector<std::uint64_t>& objectIds) {
    const auto ids = vectorIdsOf(session.project(), objectIds);
    if (ids.empty()) {
        return {false, "Sélectionnez au moins un objet issu d'une forme vectorielle."};
    }
    auto made = commands::make_convert_to_curves_command(session.project(), ids);
    if (!made.command) {
        return {false, made.error.empty() ? "Conversion impossible." : made.error};
    }
    session.execute(std::move(made.command));
    return {true, {}};
}

ServiceResult smoothContours(DocumentSession& session,
                             const std::vector<std::uint64_t>& objectIds) {
    const auto ids = vectorIdsOf(session.project(), objectIds);
    if (ids.empty()) {
        return {false, "Sélectionnez au moins un objet issu d'une forme vectorielle."};
    }
    auto made = commands::make_smooth_command(session.project(), ids);
    if (!made.command) {
        return {false, made.error.empty() ? "Lissage impossible." : made.error};
    }
    session.execute(std::move(made.command));
    return {true, {}};
}

std::optional<std::vector<std::vector<PointMm>>>
previewTangentDrag(const document::Project& project, std::uint64_t objectId, const NodeRef& node,
                   bool isOut, PointMm toMm) {
    if (!withinBounds(toMm)) {
        return std::nullopt;
    }
    if (const auto* satin = satinOf(project, objectId); satin != nullptr) {
        if (node.set > 1) {
            return std::nullopt;
        }
        geometry::Path rails[2] = {satin->rail_a, satin->rail_b};
        auto& rail = rails[node.set];
        if (node.node >= rail.nodes.size()) {
            return std::nullopt;
        }
        auto& n = rail.nodes[node.node];
        const Vec2um rel{Micrometers{toUm(toMm).x.value - n.pos.x.value},
                         Micrometers{toUm(toMm).y.value - n.pos.y.value}};
        const auto h = handlesAfter(n, isOut, rel);
        n.tan_in = h.in;
        n.tan_out = h.out;
        return std::vector<std::vector<PointMm>>{outline(rails[0]), outline(rails[1])};
    }
    const auto* vector = sourceOf(project, objectId);
    if (vector == nullptr || node.set >= vector->paths.size()) {
        return std::nullopt;
    }
    std::vector<geometry::PathSet> sets = vector->paths;
    auto& set = sets[node.set];
    auto* path = node.path == 0
                     ? &set.outer
                     : (node.path - 1 < set.holes.size() ? &set.holes[node.path - 1] : nullptr);
    if (path == nullptr || node.node >= path->nodes.size()) {
        return std::nullopt;
    }
    auto& n = path->nodes[node.node];
    const Vec2um rel{Micrometers{toUm(toMm).x.value - n.pos.x.value},
                     Micrometers{toUm(toMm).y.value - n.pos.y.value}};
    const auto h = handlesAfter(n, isOut, rel);
    n.tan_in = h.in;
    n.tan_out = h.out;
    std::vector<std::vector<PointMm>> out;
    for (const auto& s : sets) {
        out.push_back(outline(s.outer));
        for (const auto& hole : s.holes) {
            out.push_back(outline(hole));
        }
    }
    return out;
}

ServiceResult moveTangent(DocumentSession& session, std::uint64_t objectId, const NodeRef& node,
                          bool isOut, PointMm toMm) {
    if (!withinBounds(toMm)) {
        return {false, "Poignée hors de la zone de travail."};
    }
    auto composite = std::make_unique<commands::CompositeCommand>("Déplacer une poignée Bézier");
    if (const auto* satin = satinOf(session.project(), objectId); satin != nullptr) {
        if (node.set > 1 || node.node >= railOf(*satin, node.set).nodes.size()) {
            return {false, "Nœud inconnu."};
        }
        const auto& n = railOf(*satin, node.set).nodes[node.node];
        const Vec2um rel{Micrometers{toUm(toMm).x.value - n.pos.x.value},
                         Micrometers{toUm(toMm).y.value - n.pos.y.value}};
        const auto h = handlesAfter(n, isOut, rel);
        if (h.in != n.tan_in) {
            composite->add(std::make_unique<commands::MoveSatinRailHandleCommand>(
                ObjectId{objectId}, sideOf(node.set), node.node, false, n.tan_in, h.in));
        }
        if (h.out != n.tan_out) {
            composite->add(std::make_unique<commands::MoveSatinRailHandleCommand>(
                ObjectId{objectId}, sideOf(node.set), node.node, true, n.tan_out, h.out));
        }
    } else {
        const auto* vector = sourceOf(session.project(), objectId);
        if (vector == nullptr || node.set >= vector->paths.size()) {
            return {false, "Cet objet n'a pas de contour éditable."};
        }
        const auto& set = vector->paths[node.set];
        const geometry::Path* path =
            node.path == 0
                ? &set.outer
                : (node.path - 1 < set.holes.size() ? &set.holes[node.path - 1] : nullptr);
        if (path == nullptr || node.node >= path->nodes.size()) {
            return {false, "Nœud inconnu."};
        }
        const auto& n = path->nodes[node.node];
        const Vec2um rel{Micrometers{toUm(toMm).x.value - n.pos.x.value},
                         Micrometers{toUm(toMm).y.value - n.pos.y.value}};
        const auto h = handlesAfter(n, isOut, rel);
        const document::NodeRef ref{node.set, node.path, node.node};
        if (h.in != n.tan_in) {
            composite->add(std::make_unique<commands::SetNodeHandleCommand>(vector->id, ref, false,
                                                                            n.tan_in, h.in));
        }
        if (h.out != n.tan_out) {
            composite->add(std::make_unique<commands::SetNodeHandleCommand>(vector->id, ref, true,
                                                                            n.tan_out, h.out));
        }
    }
    if (composite->empty()) {
        return {true, {}}; // la poignée n'a pas bougé : aucun pas d'annulation inutile
    }
    session.execute(std::move(composite));
    return {true, {}};
}

ServiceResult deleteNodes(DocumentSession& session, std::uint64_t objectId,
                          const std::vector<NodeRef>& nodes) {
    if (satinOf(session.project(), objectId) != nullptr) {
        return {false, "Les rails d'une colonne satin ne perdent pas de nœud ici."};
    }
    const auto* vector = sourceOf(session.project(), objectId);
    if (vector == nullptr) {
        return {false, "Cet objet n'a pas de contour éditable."};
    }
    if (nodes.empty()) {
        return {false, "Sélectionnez d'abord les nœuds à supprimer."};
    }
    // Simulation sur une copie : minimum de nœuds par contour, aucun croisement introduit.
    std::vector<geometry::PathSet> sets = vector->paths;
    auto sorted = nodes;
    std::sort(sorted.begin(), sorted.end(), [](const NodeRef& a, const NodeRef& b) {
        return std::tie(a.set, a.path, b.node) < std::tie(b.set, b.path, a.node);
    });
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    for (const auto& n : sorted) {
        if (n.set >= sets.size()) {
            return {false, "Nœud inconnu."};
        }
        auto& set = sets[n.set];
        auto* path = n.path == 0
                         ? &set.outer
                         : (n.path - 1 < set.holes.size() ? &set.holes[n.path - 1] : nullptr);
        if (path == nullptr || n.node >= path->nodes.size()) {
            return {false, "Nœud inconnu."};
        }
        const std::size_t minNodes = path->closed ? 3 : 2;
        if (path->nodes.size() <= minNodes) {
            return {false, "Un contour fermé garde au moins trois nœuds."};
        }
        path->nodes.erase(path->nodes.begin() + static_cast<std::ptrdiff_t>(n.node));
    }
    for (std::size_t s = 0; s < sets.size(); ++s) {
        if (geometry::path_set_has_crossings(sets[s], kCrossingTolerance) &&
            !geometry::path_set_has_crossings(vector->paths[s], kCrossingTolerance)) {
            return {false, "Supprimer ces nœuds ferait croiser le contour."};
        }
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        sorted.size() == 1 ? "Supprimer un nœud" : "Supprimer des nœuds");
    for (const auto& n : sorted) { // ordre décroissant : les indices restent valables
        composite->add(std::make_unique<commands::RemoveNodeCommand>(
            vector->id, document::NodeRef{n.set, n.path, n.node}));
    }
    session.execute(std::move(composite));
    return {true, {}};
}

ServiceResult reshapeSegment(DocumentSession& session, std::uint64_t objectId,
                             const SegmentHit& hit, PointMm deltaMm) {
    const auto* vector = sourceOf(session.project(), objectId);
    if (vector == nullptr) {
        return {false, "Cet objet n'a pas de contour éditable."};
    }
    if (!withinBounds(deltaMm)) {
        return {false, "Déplacement hors de la zone de travail."};
    }
    auto made = commands::make_reshape_segment_command(session.project(), vector->id, hit.start.set,
                                                       hit.start.path, hit.start.node, hit.t,
                                                       toUm(deltaMm));
    if (!made.command) {
        return {false, made.error.empty() ? "Remodelage impossible." : made.error};
    }
    session.execute(std::move(made.command));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
