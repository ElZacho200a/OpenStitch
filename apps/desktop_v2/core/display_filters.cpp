// SPDX-License-Identifier: Apache-2.0
#include "display_filters.hpp"

#include <algorithm>
#include <functional>

#include "document_session.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"

namespace openstitch::desktop_v2 {

namespace {

// Applique « visible si `keep`, caché sinon » à tous les objets, en un seul lot annulable.
ServiceResult applyVisibility(DocumentSession& session,
                              const std::function<bool(const document::EmbroideryObject&)>& keep,
                              const char* label, const char* noMatch) {
    std::vector<std::uint64_t> show;
    std::vector<std::uint64_t> hide;
    bool anyMatch = false;
    for (const auto& o : session.project().embroidery_objects) {
        const bool wanted = keep(o);
        anyMatch = anyMatch || wanted;
        if (wanted && !o.visible) {
            show.push_back(o.id.value);
        } else if (!wanted && o.visible) {
            hide.push_back(o.id.value);
        }
    }
    if (!anyMatch) {
        return {false, noMatch};
    }
    if (show.empty() && hide.empty()) {
        return {false, "L'affichage est déjà celui demandé."};
    }
    std::string batch = R"({"schema":"openstitch.batch/1","operations":[)";
    bool first = true;
    const auto add = [&](const std::vector<std::uint64_t>& ids, bool visible) {
        for (const auto id : ids) {
            batch += std::string(first ? "" : ",") + R"({"op":"set_visible","object":)" +
                     std::to_string(id) + R"(,"visible":)" + (visible ? "true" : "false") + "}";
            first = false;
        }
    };
    add(show, true);
    add(hide, false);
    batch += "]}";
    const auto r = session.applyBatch(batch, label);
    if (!r.committed) {
        return {false, r.report.outcomes.empty() ? "Changement d'affichage refusé."
                                                 : r.report.outcomes.back().message};
    }
    return {true, {}};
}

} // namespace

ServiceResult showOnlyColour(DocumentSession& session, const std::array<std::uint8_t, 3>& rgb) {
    return applyVisibility(
        session, [&](const document::EmbroideryObject& o) { return o.rgb == rgb; },
        "Afficher uniquement cette couleur", "Aucun objet n'a cette couleur de fil.");
}

ServiceResult showOnlyType(DocumentSession& session, const std::string& typeId) {
    return applyVisibility(
        session, [&](const document::EmbroideryObject& o) { return stitchTypeId(o) == typeId; },
        "Afficher uniquement ce type de points", "Aucun objet n'a ce type de points.");
}

ServiceResult showAllObjects(DocumentSession& session) {
    if (hiddenObjectCount(session.project()) == 0) {
        return {false, "Tous les objets sont déjà visibles."};
    }
    return applyVisibility(
        session, [](const document::EmbroideryObject&) { return true; }, "Tout afficher",
        "Aucun objet.");
}

std::size_t hiddenObjectCount(const document::Project& project) {
    return static_cast<std::size_t>(
        std::count_if(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                      [](const document::EmbroideryObject& o) { return !o.visible; }));
}

} // namespace openstitch::desktop_v2
