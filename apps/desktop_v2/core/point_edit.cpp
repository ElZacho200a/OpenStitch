// SPDX-License-Identifier: Apache-2.0
#include "point_edit.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/stitch_point_edit_commands.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr double kMaxCoordMm = 1000.0;

ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}

Vec2um toUm(PointMm p) {
    return {to_micrometers(Millimeters{p.x}), to_micrometers(Millimeters{p.y})};
}

bool inRange(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) <= kMaxCoordMm &&
           std::fabs(p.y) <= kMaxCoordMm;
}

// Vue courante + validation commune : objet existant, pas périmé.
struct Ctx {
    stitch_generation::ObjectEditView view;
    ObjectId id;
};

Result<Ctx> context(const document::Project& project, std::uint64_t objectId) {
    const ObjectId id{objectId};
    if (project.findEmbroidery(id) == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Objet introuvable",
                    "Sélectionnez un objet brodé.");
    }
    auto view = stitch_generation::edit_view(project, id);
    if (!view) {
        return std::unexpected(view.error());
    }
    if (view->state == stitch_generation::ObjectEditState::Dirty) {
        return fail(ErrorCategory::OperationImpossible, "Retouches périmées",
                    "La géométrie de l'objet a changé depuis les retouches : annulez ou "
                    "utilisez « Effacer les retouches ».");
    }
    return Ctx{std::move(*view), id};
}

bool isMovableIndex(const Ctx& c, std::size_t i) {
    return i < c.view.raw.size() && c.view.raw[i].pass == stitch::StitchPass::TopStitch &&
           stitch_generation::is_movable_point(c.view.raw[i]);
}
} // namespace

Result<PointView> stitchPoints(const document::Project& project, std::uint64_t objectId) {
    const ObjectId id{objectId};
    const auto* obj = project.findEmbroidery(id);
    if (obj == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Objet introuvable",
                    "Sélectionnez un objet brodé.");
    }
    auto view = stitch_generation::edit_view(project, id);
    if (!view) {
        return std::unexpected(view.error());
    }
    PointView out;
    out.state = static_cast<PointEditState>(view->state);
    out.editedCount = obj->overrides.size();
    for (std::size_t i = 0; i < view->raw.size(); ++i) {
        const auto& cmd = view->raw[i];
        if (cmd.pass != stitch::StitchPass::TopStitch) {
            continue;
        }
        if (cmd.type != stitch::CommandType::Stitch && cmd.type != stitch::CommandType::Jump) {
            continue;
        }
        EditablePoint p;
        p.index = i;
        p.pos = {to_millimeters(cmd.pos.x).value, to_millimeters(cmd.pos.y).value};
        p.movable = stitch_generation::is_movable_point(cmd);
        p.jump = cmd.type == stitch::CommandType::Jump;
        const auto ov =
            std::find_if(obj->overrides.begin(), obj->overrides.end(),
                         [i](const document::StitchOverride& o) { return o.base_index == i; });
        if (ov != obj->overrides.end()) {
            p.edited = true;
            p.deleted = ov->deleted;
            if (ov->moved_to) {
                p.pos = {to_millimeters(ov->moved_to->x).value,
                         to_millimeters(ov->moved_to->y).value};
            }
        }
        out.points.push_back(p);
    }
    return out;
}

ServiceResult moveStitchPoints(DocumentSession& session, std::uint64_t objectId,
                               const std::vector<std::pair<std::size_t, PointMm>>& moves) {
    if (moves.empty()) {
        return refuse("Aucun point à déplacer.");
    }
    auto c = context(session.project(), objectId);
    if (!c) {
        return refuse(c.error().message + ". " + c.error().detail);
    }
    std::vector<commands::EditStitchPointsCommand::Item> items;
    for (const auto& [index, pos] : moves) {
        if (!isMovableIndex(*c, index)) {
            return refuse("Ce point ne peut pas être déplacé.");
        }
        if (!inRange(pos)) {
            return refuse("Position hors limites (±1000 mm).");
        }
        items.push_back({index, toUm(pos)});
    }
    session.execute(std::make_unique<commands::EditStitchPointsCommand>(
        commands::EditStitchPointsCommand::move_points(c->id, std::move(items), c->view.fingerprint,
                                                       c->view.point_count)));
    return {true, {}};
}

ServiceResult deleteStitchPoints(DocumentSession& session, std::uint64_t objectId,
                                 const std::vector<std::size_t>& indices) {
    if (indices.empty()) {
        return refuse("Aucun point à supprimer.");
    }
    auto c = context(session.project(), objectId);
    if (!c) {
        return refuse(c.error().message + ". " + c.error().detail);
    }
    for (const std::size_t i : indices) {
        if (!isMovableIndex(*c, i)) {
            return refuse("Ce point ne peut pas être supprimé.");
        }
    }
    session.execute(std::make_unique<commands::EditStitchPointsCommand>(
        commands::EditStitchPointsCommand::delete_points(c->id, indices, c->view.fingerprint,
                                                         c->view.point_count)));
    return {true, {}};
}

ServiceResult restoreStitchPoints(DocumentSession& session, std::uint64_t objectId,
                                  const std::vector<std::size_t>& indices) {
    if (indices.empty()) {
        return refuse("Aucun point à rétablir.");
    }
    auto c = context(session.project(), objectId);
    if (!c) {
        return refuse(c.error().message + ". " + c.error().detail);
    }
    for (const std::size_t i : indices) {
        if (i >= c->view.raw.size()) {
            return refuse("Point inconnu.");
        }
    }
    session.execute(std::make_unique<commands::EditStitchPointsCommand>(
        commands::EditStitchPointsCommand::restore_points(c->id, indices, c->view.fingerprint,
                                                          c->view.point_count)));
    return {true, {}};
}

ServiceResult insertStitchPoint(DocumentSession& session, std::uint64_t objectId,
                                std::size_t afterIndex, PointMm pos) {
    auto c = context(session.project(), objectId);
    if (!c) {
        return refuse(c.error().message + ". " + c.error().detail);
    }
    if (!isMovableIndex(*c, afterIndex)) {
        return refuse("On ne peut insérer qu'après un point cousu de la passe supérieure.");
    }
    if (!inRange(pos)) {
        return refuse("Position hors limites (±1000 mm).");
    }
    session.execute(std::make_unique<commands::EditStitchPointsCommand>(
        commands::EditStitchPointsCommand::insert_point(c->id, afterIndex, toUm(pos),
                                                        c->view.fingerprint, c->view.point_count)));
    return {true, {}};
}

ServiceResult discardStitchEdits(DocumentSession& session, std::uint64_t objectId) {
    const ObjectId id{objectId};
    const auto* obj = session.project().findEmbroidery(id);
    if (obj == nullptr) {
        return refuse("Objet introuvable.");
    }
    if (obj->overrides.empty()) {
        return refuse("Cet objet n'a aucune retouche.");
    }
    session.execute(std::make_unique<commands::DiscardOverridesCommand>(id));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
