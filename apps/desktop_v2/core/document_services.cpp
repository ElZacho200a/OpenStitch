// SPDX-License-Identifier: Apache-2.0
#include "document_services.hpp"

#include <algorithm>

#include <cmath>

#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/stitch_preset_commands.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::desktop_v2 {

namespace {
ServiceResult ok() {
    return {true, {}};
}
ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}
std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) {
        return {};
    }
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}
const document::EmbroideryObject* findObject(const document::Project& p, std::uint64_t id) {
    const auto it =
        std::ranges::find_if(p.embroidery_objects, [&](const auto& o) { return o.id.value == id; });
    return it == p.embroidery_objects.end() ? nullptr : &*it;
}
bool hasCalque(const document::Project& p, std::uint64_t id) {
    return std::ranges::any_of(p.calques, [&](const auto& c) { return c.id.value == id; });
}
} // namespace

// --- Préréglages ----------------------------------------------------------------------------

std::vector<std::string> presetNames(const document::Project& project) {
    std::vector<std::string> out;
    for (const auto& p : project.stitch_presets) {
        out.push_back(p.name);
    }
    return out;
}

ServiceResult savePreset(DocumentSession& session, const std::string& name,
                         std::uint64_t objectId) {
    const std::string clean = trimmed(name);
    if (clean.empty()) {
        return refuse("Donnez un nom au préréglage.");
    }
    if (clean.size() > 64) {
        return refuse("Nom trop long (64 caractères au plus).");
    }
    const auto* object = findObject(session.project(), objectId);
    if (object == nullptr) {
        return refuse("Sélectionnez l'objet dont les réglages deviennent le préréglage.");
    }
    session.execute(std::make_unique<commands::SaveStitchPresetCommand>(clean, object->params));
    return ok();
}

ServiceResult applyPreset(DocumentSession& session, int index,
                          const std::vector<std::uint64_t>& objectIds) {
    const auto& presets = session.project().stitch_presets;
    if (index < 0 || static_cast<std::size_t>(index) >= presets.size()) {
        return refuse("Préréglage inconnu.");
    }
    std::vector<ObjectId> ids;
    for (const auto id : objectIds) {
        if (findObject(session.project(), id) != nullptr) {
            ids.push_back(ObjectId{id});
        }
    }
    if (ids.empty()) {
        return refuse("Sélectionnez au moins un objet.");
    }
    session.execute(std::make_unique<commands::ApplyStitchPresetCommand>(
        std::move(ids), presets[static_cast<std::size_t>(index)]));
    return ok();
}

ServiceResult removePreset(DocumentSession& session, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= session.project().stitch_presets.size()) {
        return refuse("Préréglage inconnu.");
    }
    session.execute(std::make_unique<commands::RemoveStitchPresetCommand>(index));
    return ok();
}

// --- Calques --------------------------------------------------------------------------------

std::vector<CalqueInfo> calques(const document::Project& project) {
    std::vector<CalqueInfo> out;
    for (const auto& c : project.calques) {
        out.push_back({c.id.value, c.name, c.visible, c.locked});
    }
    return out;
}

ServiceResult addCalque(DocumentSession& session, const std::string& name) {
    const std::string clean = trimmed(name);
    if (clean.empty()) {
        return refuse("Donnez un nom au calque.");
    }
    // Les identifiants ne sont jamais réutilisés : réserver le suivant n'est pas une modification
    // du document (le compteur n'est ni annulable ni visible), c'est la seule écriture hors
    // commande.
    const ObjectId id = session.allocateObjectId();
    const ObjectId first =
        session.project().calques.empty() ? session.allocateObjectId() : ObjectId{};
    session.execute(std::make_unique<commands::AddCalqueCommand>(id, clean, first));
    return ok();
}

ServiceResult renameCalque(DocumentSession& session, std::uint64_t id, const std::string& name) {
    const std::string clean = trimmed(name);
    if (!hasCalque(session.project(), id)) {
        return refuse("Calque inconnu.");
    }
    if (clean.empty()) {
        return refuse("Donnez un nom au calque.");
    }
    session.execute(std::make_unique<commands::RenameCalqueCommand>(ObjectId{id}, clean));
    return ok();
}

ServiceResult removeCalque(DocumentSession& session, std::uint64_t id) {
    if (!hasCalque(session.project(), id)) {
        return refuse("Calque inconnu.");
    }
    if (session.project().calques.size() <= 1) {
        return refuse("Le dernier calque ne peut pas être supprimé.");
    }
    session.execute(std::make_unique<commands::RemoveCalqueCommand>(ObjectId{id}));
    return ok();
}

ServiceResult setCalqueVisible(DocumentSession& session, std::uint64_t id, bool visible) {
    if (!hasCalque(session.project(), id)) {
        return refuse("Calque inconnu.");
    }
    session.execute(std::make_unique<commands::SetCalqueVisibleCommand>(ObjectId{id}, visible));
    return ok();
}

ServiceResult setCalqueLocked(DocumentSession& session, std::uint64_t id, bool locked) {
    if (!hasCalque(session.project(), id)) {
        return refuse("Calque inconnu.");
    }
    session.execute(std::make_unique<commands::SetCalqueLockedCommand>(ObjectId{id}, locked));
    return ok();
}

ServiceResult moveCalque(DocumentSession& session, std::uint64_t id, int direction) {
    const auto& list = session.project().calques;
    const auto it = std::ranges::find_if(list, [&](const auto& c) { return c.id.value == id; });
    if (it == list.end()) {
        return refuse("Calque inconnu.");
    }
    const auto pos = it - list.begin();
    if ((direction < 0 && pos == 0) ||
        (direction > 0 && pos + 1 == static_cast<long>(list.size()))) {
        return refuse("Le calque est déjà au bout de la pile.");
    }
    session.execute(
        std::make_unique<commands::MoveCalqueCommand>(ObjectId{id}, direction < 0 ? -1 : 1));
    return ok();
}

ServiceResult moveObjectsToCalque(DocumentSession& session,
                                  const std::vector<std::uint64_t>& objectIds,
                                  std::uint64_t calqueId) {
    if (!hasCalque(session.project(), calqueId)) {
        return refuse("Calque inconnu.");
    }
    std::vector<ObjectId> shapes;
    for (const auto id : objectIds) {
        if (const auto* o = findObject(session.project(), id);
            o != nullptr && o->source_vector.valid()) {
            shapes.push_back(o->source_vector);
        }
    }
    if (shapes.empty()) {
        return refuse("Sélectionnez au moins un objet.");
    }
    session.execute(
        std::make_unique<commands::SetShapesCalqueCommand>(std::move(shapes), ObjectId{calqueId}));
    return ok();
}

namespace {
// Aplatit les courbes : Clipper2 travaille sur des polygones bruts (même tolérance que l'export
// DXF).
geometry::Path flattenToPath(const geometry::Path& path) {
    constexpr Micrometers kFlattenTolerance{50};
    const geometry::Polyline flat = geometry::flatten(path, kFlattenTolerance);
    geometry::Path out;
    out.closed = flat.closed;
    out.nodes.reserve(flat.points.size());
    for (const Vec2um& p : flat.points) {
        out.nodes.push_back(
            geometry::PathNode{p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return out;
}
} // namespace

ServiceResult offsetShapes(DocumentSession& session, const std::vector<std::uint64_t>& objectIds,
                           double offsetMm) {
    if (!std::isfinite(offsetMm) || offsetMm == 0.0 || std::abs(offsetMm) > 50.0) {
        return refuse("Décalage invalide : entre -50 et 50 mm, non nul.");
    }
    // inset_path_set : delta > 0 = retrait intérieur, l'inverse de « positif agrandit ».
    const Micrometers delta = to_micrometers(Millimeters{-offsetMm});
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
    for (const auto id : objectIds) {
        const auto* source = findObject(session.project(), id);
        const auto* shape = source ? session.project().findObject(source->source_vector) : nullptr;
        if (shape == nullptr) {
            continue;
        }
        document::VectorObject result;
        result.name = shape->name + " (décalé)";
        result.rgb = shape->rgb;
        result.calque = shape->calque;
        for (const auto& set : shape->paths) {
            geometry::PathSet flatSet;
            flatSet.outer = flattenToPath(set.outer);
            for (const auto& hole : set.holes) {
                flatSet.holes.push_back(flattenToPath(hole));
            }
            const auto offset = geometry::inset_path_set(flatSet, delta);
            if (!offset) {
                return refuse("Décalage impossible : " + offset.error().message);
            }
            for (const auto& piece : *offset) {
                result.paths.push_back(piece);
            }
        }
        if (result.paths.empty()) {
            continue; // la forme disparaît : rien à créer pour celle-ci
        }
        result.id = session.allocateObjectId();
        document::EmbroideryObject copy = *source;
        copy.id = session.allocateObjectId();
        copy.name = result.name;
        copy.source_vector = result.id;
        vectors.push_back(std::move(result));
        embroideries.push_back(std::move(copy));
    }
    if (vectors.empty()) {
        return refuse(
            "Rien à décaler : sélectionnez une forme, ou le décalage la fait disparaître.");
    }
    session.execute(std::make_unique<commands::AddObjectBatchCommand>(
        std::move(vectors), std::move(embroideries), "Décaler la forme"));
    return ok();
}

} // namespace openstitch::desktop_v2
