// SPDX-License-Identifier: Apache-2.0
#include "direction_guides.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr double kMaxCoordMm = 1000.0;
constexpr double kMinStepMm = 0.2; // points plus proches qu'un cinquième de mm : confondus
constexpr std::int32_t kStrokeSimplifyUm = 250; // écart toléré en simplifiant un tracé

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

PointMm toMm(Vec2um p) {
    return {to_millimeters(p.x).value, to_millimeters(p.y).value};
}

bool validPoint(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) <= kMaxCoordMm &&
           std::fabs(p.y) <= kMaxCoordMm;
}

// Distance au carré de `p` au segment [a ; b] (mm).
double segmentDistance2(PointMm p, PointMm a, PointMm b) {
    const double vx = b.x - a.x;
    const double vy = b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    double t = len2 > 0.0 ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double dx = a.x + t * vx - p.x;
    const double dy = a.y + t * vy - p.y;
    return dx * dx + dy * dy;
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
        const double dy = static_cast<double>(delta.y.value);
        const double dx = static_cast<double>(delta.x.value);
        guide.angle = Angle{std::atan2(dy, dx)};
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
    // Un tracé à main levée donne un point par échantillon de souris : Douglas-Peucker à
    // 0,25 mm ne garde que les nœuds utiles (peu de poignées à glisser, même forme).
    geometry::Path line;
    line.closed = false;
    for (const Vec2um p : pts) {
        line.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    line = geometry::simplify(line, Micrometers{kStrokeSimplifyUm});
    if (isBreak) {
        params->break_lines.push_back(std::move(line));
    } else {
        std::vector<Vec2um> kept;
        kept.reserve(line.nodes.size());
        for (const auto& node : line.nodes) {
            kept.push_back(node.pos);
        }
        params->guides.push_back(geometry::smooth_open_path(kept));
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

bool canDirectionGuide(const document::Project& project, std::uint64_t objectId) {
    const auto* emb = project.findEmbroidery(ObjectId{objectId});
    if (emb == nullptr) {
        return false;
    }
    if (std::holds_alternative<document::DirectionalFillParams>(emb->params)) {
        return true;
    }
    // Comme l'opération `directional_guides` : seul un tatami se convertit.
    if (!std::holds_alternative<document::TatamiParams>(emb->params)) {
        return false;
    }
    const auto* source = project.findObject(emb->source_vector);
    return source != nullptr && !source->paths.empty();
}

std::vector<FillGuideCurve> fillGuideCurves(const document::Project& project,
                                            std::uint64_t objectId, bool breakLines) {
    std::vector<FillGuideCurve> out;
    const auto* dir = directionalOf(project, objectId);
    if (dir == nullptr) {
        return out;
    }
    const auto& list = breakLines ? dir->break_lines : dir->guides;
    out.reserve(list.size());
    for (const auto& path : list) {
        FillGuideCurve curve;
        for (const auto& node : path.nodes) {
            curve.nodes.push_back(toMm(node.pos));
        }
        // Tolérance d'aplatissement : 0,05 mm, invisible à l'écran et légère.
        for (const Vec2um p : geometry::flatten(path, Micrometers{50}).points) {
            curve.curve.push_back(toMm(p));
        }
        out.push_back(std::move(curve));
    }
    return out;
}

int fillGuideAt(const document::Project& project, std::uint64_t objectId, bool breakLines,
                PointMm p, double toleranceMm) {
    if (!validPoint(p) || !(toleranceMm > 0.0)) {
        return -1;
    }
    const auto curves = fillGuideCurves(project, objectId, breakLines);
    int best = -1;
    double bestD = toleranceMm * toleranceMm;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const auto& pts = curves[i].curve;
        for (std::size_t k = 0; k < pts.size(); ++k) {
            const double d = k + 1 < pts.size() ? segmentDistance2(p, pts[k], pts[k + 1])
                                                : segmentDistance2(p, pts[k], pts[k]);
            if (d <= bestD) {
                // Égalité : la courbe tracée en dernier (dessinée au-dessus) l'emporte.
                bestD = d;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

ServiceResult moveFillGuideNode(DocumentSession& session, std::uint64_t objectId, bool breakLines,
                                std::size_t guideIndex, std::size_t nodeIndex, PointMm to) {
    const auto* dir = directionalOf(session.project(), objectId);
    if (dir == nullptr) {
        return {false, "Cet objet n'est pas un remplissage directionnel."};
    }
    if (!validPoint(to)) {
        return {false, "Position hors du motif."};
    }
    auto params = *dir;
    auto& list = breakLines ? params.break_lines : params.guides;
    if (guideIndex >= list.size() || nodeIndex >= list[guideIndex].nodes.size()) {
        return {false, breakLines ? "Ligne de rupture inconnue." : "Guide inconnu."};
    }
    auto& nodes = list[guideIndex].nodes;
    const Vec2um point = toUm(to);
    if (nodes[nodeIndex].pos == point) {
        return {true, {}};
    }
    const auto tooClose = [&](std::size_t other) {
        const PointMm o = toMm(nodes[other].pos);
        return std::hypot(o.x - to.x, o.y - to.y) < kMinStepMm;
    };
    if ((nodeIndex > 0 && tooClose(nodeIndex - 1)) ||
        (nodeIndex + 1 < nodes.size() && tooClose(nodeIndex + 1))) {
        return {false, "Deux nœuds voisins ne peuvent pas être confondus."};
    }
    nodes[nodeIndex].pos = point; // tangentes relatives : la courbe suit le nœud
    session.execute(std::make_unique<commands::EditDirectionalFillCommand>(
        ObjectId{objectId}, std::move(params),
        breakLines ? "Déplacer un nœud de rupture" : "Déplacer un nœud de guide"));
    return {true, {}};
}

ServiceResult removeFillGuide(DocumentSession& session, std::uint64_t objectId, bool breakLines,
                              std::size_t guideIndex) {
    const auto* dir = directionalOf(session.project(), objectId);
    if (dir == nullptr) {
        return {false, "Cet objet n'est pas un remplissage directionnel."};
    }
    auto params = *dir;
    auto& list = breakLines ? params.break_lines : params.guides;
    if (guideIndex >= list.size()) {
        return {false, breakLines ? "Ligne de rupture inconnue." : "Guide inconnu."};
    }
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(guideIndex));
    session.execute(std::make_unique<commands::EditDirectionalFillCommand>(
        ObjectId{objectId}, std::move(params),
        breakLines ? "Retirer une ligne de rupture" : "Retirer un guide de direction"));
    return {true, {}};
}

ServiceResult autoDirectionGuides(DocumentSession& session, std::uint64_t objectId) {
    if (!canDirectionGuide(session.project(), objectId)) {
        return {false, "Sélectionnez un tatami ou un remplissage directionnel à guider."};
    }
    // Même algorithme et même opération que l'action « Guides de direction automatiques »
    // (`directional_guides` : axe médian de la forme, ruptures conservées) : UNE étape.
    // Simplifiés à 1 mm : une poignée par inflexion au lieu d'un nœud tous les 2,5 mm, que
    // l'on ne pourrait ni distinguer ni saisir au zoom « Ajuster ».
    const std::string batch = R"({"schema":"openstitch.batch/1","operations":[)"
                              R"({"op":"directional_guides","object":)" +
                              std::to_string(objectId) + R"(,"simplify_mm":1}]})";
    const auto r = session.applyBatch(batch, "Générer les guides de direction");
    if (!r.committed) {
        return {false, r.report.outcomes.empty() || r.report.outcomes.back().message.empty()
                           ? std::string("Aucun guide proposé pour cette forme : tracez-le à la "
                                         "main.")
                           : r.report.outcomes.back().message};
    }
    return {true, {}};
}

} // namespace openstitch::desktop_v2
