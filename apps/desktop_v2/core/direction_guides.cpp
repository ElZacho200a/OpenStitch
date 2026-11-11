// SPDX-License-Identifier: Apache-2.0
#include "direction_guides.hpp"

#include <cmath>
#include <memory>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr double kMaxCoordMm = 1000.0;
constexpr double kMinStepMm = 0.2; // points plus proches qu'un cinquième de mm : confondus

Vec2um toUm(PointMm p) {
    return {to_micrometers(Millimeters{p.x}), to_micrometers(Millimeters{p.y})};
}

const document::DirectionalFillParams* directionalOf(const document::Project& project,
                                                     std::uint64_t id) {
    const auto* obj = project.findEmbroidery(ObjectId{id});
    return obj != nullptr ? std::get_if<document::DirectionalFillParams>(&obj->params) : nullptr;
}

// Paramètres directionnels de l'objet : les siens, ou ceux obtenus en convertissant son remplissage
// (réglages du tatami repris). Vide si la conversion est impossible (pas de contour source).
std::optional<document::DirectionalFillParams> paramsFor(const document::Project& project,
                                                         const document::EmbroideryObject& emb) {
    if (const auto* dir = std::get_if<document::DirectionalFillParams>(&emb.params)) {
        return *dir;
    }
    const auto* source = project.findObject(emb.source_vector);
    if (source == nullptr || source->paths.empty()) {
        return std::nullopt;
    }
    document::TatamiParams base;
    if (const auto* tatami = std::get_if<document::TatamiParams>(&emb.params)) {
        base = *tatami;
    }
    auto params = stitch_generation::directional_from_tatami(
        base, source->paths, static_cast<std::uint32_t>(emb.id.value));
    return params;
}

} // namespace

int directionGuideCount(const document::Project& project, std::uint64_t objectId) {
    if (const auto* obj = project.findEmbroidery(ObjectId{objectId})) {
        if (const auto* satin = std::get_if<document::AutoSatinParams>(&obj->params)) {
            return static_cast<int>(satin->guides.size());
        }
    }
    const auto* dir = directionalOf(project, objectId);
    return dir != nullptr ? static_cast<int>(dir->guides.size()) : -1;
}

std::vector<std::vector<PointMm>> autoSatinGuidePaths(const document::Project& project,
                                                      std::uint64_t objectId) {
    std::vector<std::vector<PointMm>> out;
    const auto* obj = project.findEmbroidery(ObjectId{objectId});
    const auto* satin = obj ? std::get_if<document::AutoSatinParams>(&obj->params) : nullptr;
    if (!satin) {
        return out;
    }
    for (const auto& guide : satin->guides) {
        std::vector<PointMm> path;
        for (const Vec2um p : guide.path) {
            path.push_back({to_millimeters(p.x).value, to_millimeters(p.y).value});
        }
        // Preserve indices even for legacy single-angle guides with no path.
        out.push_back(std::move(path));
    }
    return out;
}

ServiceResult moveAutoSatinGuideNode(DocumentSession& session, std::uint64_t objectId,
                                     std::size_t guideIndex, std::size_t nodeIndex, PointMm to) {
    const auto* obj = session.project().findEmbroidery(ObjectId{objectId});
    const auto* satin = obj ? std::get_if<document::AutoSatinParams>(&obj->params) : nullptr;
    if (!satin || guideIndex >= satin->guides.size()) {
        return {false, "Sélectionnez un guide de satin valide."};
    }
    if (!std::isfinite(to.x) || !std::isfinite(to.y) || std::fabs(to.x) > kMaxCoordMm ||
        std::fabs(to.y) > kMaxCoordMm) {
        return {false, "Position du guide hors limites."};
    }
    auto params = *satin;
    auto& guide = params.guides[guideIndex];
    if (guide.path.size() < 2 || nodeIndex >= guide.path.size()) {
        return {false, "Nœud de guide inconnu."};
    }
    const Vec2um point = toUm(to);
    if ((nodeIndex > 0 && guide.path[nodeIndex - 1] == point) ||
        (nodeIndex + 1 < guide.path.size() && guide.path[nodeIndex + 1] == point)) {
        return {false, "Deux nœuds voisins ne peuvent pas être confondus."};
    }
    if (guide.path[nodeIndex] == point) {
        return {true, {}};
    }
    guide.path[nodeIndex] = point;
    guide.anchor = guide.path.front();
    const auto dx = static_cast<double>(guide.path[1].x.value) - guide.path[0].x.value;
    const auto dy = static_cast<double>(guide.path[1].y.value) - guide.path[0].y.value;
    guide.angle = Angle{std::atan2(dy, dx)};
    session.execute(std::make_unique<commands::EditAutoSatinCommand>(
        ObjectId{objectId}, std::move(params), "Déplacer un nœud de guide satin"));
    return {true, {}};
}

int breakLineCount(const document::Project& project, std::uint64_t objectId) {
    const auto* dir = directionalOf(project, objectId);
    return dir != nullptr ? static_cast<int>(dir->break_lines.size()) : -1;
}

ServiceResult addDirectionGuide(DocumentSession& session, std::uint64_t objectId,
                                const std::vector<PointMm>& points, bool isBreak) {
    const char* what = isBreak ? "ligne de rupture" : "guide de direction";
    const auto* emb = session.project().findEmbroidery(ObjectId{objectId});
    if (emb == nullptr) {
        return {false, "Sélectionnez d'abord l'objet de broderie à guider."};
    }
    std::vector<Vec2um> pts;
    for (const PointMm p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::fabs(p.x) > kMaxCoordMm ||
            std::fabs(p.y) > kMaxCoordMm) {
            return {false, std::string("Point hors du motif : ") + what + " refusé."};
        }
        if (!pts.empty()) {
            const double dx = p.x - to_millimeters(pts.back().x).value;
            const double dy = p.y - to_millimeters(pts.back().y).value;
            if (std::hypot(dx, dy) < kMinStepMm) {
                continue; // double-clic : on ignore le doublon
            }
        }
        pts.push_back(toUm(p));
    }
    if (pts.size() < 2) {
        return {false, std::string("Il faut au moins deux points distincts pour tracer une ") +
                           what + "."};
    }
    if (pts.size() > 1024) {
        return {false, "Un guide doit contenir au plus 1024 points."};
    }
    // An auto-satin guide must NOT convert the object to directional fill. For
    // the Guided strategy, the curve is the editable central line. Other auto
    // satin strategies retain their existing interpretation of curved guides.
    if (const auto* satin = std::get_if<document::AutoSatinParams>(&emb->params)) {
        if (isBreak) {
            return {false,
                    "Une ligne de rupture n'est pas prise en charge par le satin automatique."};
        }
        if (satin->guides.size() >= 64) {
            return {false, "64 guides au maximum pour un satin automatique."};
        }
        auto params = *satin;
        const Vec2um delta{Micrometers{pts[1].x.value - pts[0].x.value},
                           Micrometers{pts[1].y.value - pts[0].y.value}};
        document::AutoSatinGuide guide;
        guide.anchor = pts.front();
        guide.angle = Angle{std::atan2(static_cast<double>(delta.y.value),
                                       static_cast<double>(delta.x.value))};
        guide.absolute = true;
        guide.path = std::move(pts);
        params.guides.push_back(std::move(guide));
        session.execute(std::make_unique<commands::EditAutoSatinCommand>(
            ObjectId{objectId}, std::move(params), "Tracer un guide de satin"));
        return {true, {}};
    }
    auto params = paramsFor(session.project(), *emb);
    if (!params) {
        return {false, "Cet objet n'a pas de contour source : impossible de le guider."};
    }
    if (isBreak) {
        geometry::Path line;
        line.closed = false;
        for (const Vec2um p : pts) {
            line.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
        }
        params->break_lines.push_back(std::move(line));
    } else {
        params->guides.push_back(geometry::smooth_open_path(pts));
    }
    const std::string label =
        isBreak ? "Tracer une ligne de rupture" : "Tracer un guide de direction";
    if (std::holds_alternative<document::DirectionalFillParams>(emb->params)) {
        session.execute(std::make_unique<commands::EditDirectionalFillCommand>(
            ObjectId{objectId}, std::move(*params), label));
    } else {
        // Première fois : le remplissage devient directionnel, guide compris, en UNE étape.
        session.execute(std::make_unique<commands::ConvertFillGroupCommand>(
            ObjectId{objectId}, std::move(*params), label));
    }
    return {true, {}};
}

ServiceResult clearDirectionGuides(DocumentSession& session, std::uint64_t objectId,
                                   bool breakLines) {
    if (const auto* obj = session.project().findEmbroidery(ObjectId{objectId})) {
        if (const auto* satin = std::get_if<document::AutoSatinParams>(&obj->params)) {
            if (breakLines) {
                return {false, "Ce satin ne contient pas de lignes de rupture."};
            }
            if (satin->guides.empty()) {
                return {false, "Aucun guide de satin à effacer."};
            }
            auto params = *satin;
            params.guides.clear();
            session.execute(std::make_unique<commands::EditAutoSatinCommand>(
                ObjectId{objectId}, std::move(params), "Effacer les guides de satin"));
            return {true, {}};
        }
    }
    const auto* dir = directionalOf(session.project(), objectId);
    if (dir == nullptr) {
        return {false, "Cet objet n'est pas un remplissage directionnel."};
    }
    auto params = *dir;
    auto& list = breakLines ? params.break_lines : params.guides;
    if (list.empty()) {
        return {false, breakLines ? "Aucune ligne de rupture à effacer."
                                  : "Aucun guide de direction à effacer."};
    }
    list.clear();
    session.execute(std::make_unique<commands::EditDirectionalFillCommand>(
        ObjectId{objectId}, std::move(params),
        breakLines ? "Effacer les lignes de rupture" : "Effacer les guides de direction"));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
