// SPDX-License-Identifier: Apache-2.0
#include "node_edit.hpp"

#include <algorithm>
#include <cmath>

#include "document_session.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/node_transform.hpp"
#include "openstitch/geometry/polyline.hpp"

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

[[nodiscard]] bool withinBounds(PointMm d) {
    return std::abs(d.x) <= kMaxCoordMm && std::abs(d.y) <= kMaxCoordMm;
}

} // namespace

Result<NodeView> nodeView(const document::Project& project, std::uint64_t objectId) {
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
                                      node.tan_in.has_value() || node.tan_out.has_value()});
            }
        }
    }
    return view;
}

std::optional<std::vector<std::vector<PointMm>>> previewNodeMove(const document::Project& project,
                                                                 std::uint64_t objectId,
                                                                 const std::vector<NodeRef>& nodes,
                                                                 PointMm deltaMm, bool soft) {
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
