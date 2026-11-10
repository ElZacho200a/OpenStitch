// SPDX-License-Identifier: Apache-2.0
#include "display_filters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "document_session.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/geometry/boolean.hpp"

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

namespace {
std::string hexOf(const std::array<std::uint8_t, 3>& rgb) {
    char text[8];
    std::snprintf(text, sizeof text, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    return text;
}

ServiceResult applyChanges(DocumentSession& session,
                           const std::vector<std::pair<std::uint64_t, bool>>& changes,
                           const char* label, const char* nothing) {
    if (changes.empty()) {
        return {false, nothing};
    }
    std::string batch = R"({"schema":"openstitch.batch/1","operations":[)";
    bool first = true;
    for (const auto& [id, visible] : changes) {
        batch += std::string(first ? "" : ",") + R"({"op":"set_visible","object":)" +
                 std::to_string(id) + R"(,"visible":)" + (visible ? "true" : "false") + "}";
        first = false;
    }
    batch += "]}";
    const auto r = session.applyBatch(batch, label);
    if (!r.committed) {
        return {false, r.report.outcomes.empty() ? "Changement d'affichage refusé."
                                                 : r.report.outcomes.back().message};
    }
    return {true, {}};
}

std::vector<FilterGroup>
groupsBy(const document::Project& project,
         const std::function<std::string(const document::EmbroideryObject&)>& key) {
    std::vector<FilterGroup> out;
    for (const auto& o : project.embroidery_objects) {
        const auto k = key(o);
        auto it = std::ranges::find_if(out, [&](const FilterGroup& g) { return g.key == k; });
        if (it == out.end()) {
            out.push_back({k, 0, 0});
            it = out.end() - 1;
        }
        ++it->total;
        it->visible += o.visible ? 1 : 0;
    }
    return out;
}

ServiceResult setVisibleWhere(DocumentSession& session,
                              const std::function<bool(const document::EmbroideryObject&)>& match,
                              bool visible, const char* label) {
    std::vector<std::pair<std::uint64_t, bool>> changes;
    bool any = false;
    for (const auto& o : session.project().embroidery_objects) {
        if (match(o)) {
            any = true;
            if (o.visible != visible) {
                changes.emplace_back(o.id.value, visible);
            }
        }
    }
    if (!any) {
        return {false, "Aucun objet ne correspond."};
    }
    return applyChanges(session, changes, label, "L'affichage est déjà celui demandé.");
}
} // namespace

std::vector<FilterGroup> typeGroups(const document::Project& project) {
    return groupsBy(
        project, [](const document::EmbroideryObject& o) { return std::string(stitchTypeId(o)); });
}

std::vector<FilterGroup> colourGroups(const document::Project& project) {
    return groupsBy(project, [](const document::EmbroideryObject& o) { return hexOf(o.rgb); });
}

ServiceResult setTypeVisible(DocumentSession& session, const std::string& typeId, bool visible) {
    return setVisibleWhere(
        session, [&](const document::EmbroideryObject& o) { return stitchTypeId(o) == typeId; },
        visible, visible ? "Afficher ce type de points" : "Masquer ce type de points");
}

ServiceResult setColourVisible(DocumentSession& session, const std::array<std::uint8_t, 3>& rgb,
                               bool visible) {
    return setVisibleWhere(
        session, [&](const document::EmbroideryObject& o) { return o.rgb == rgb; }, visible,
        visible ? "Afficher cette couleur" : "Masquer cette couleur");
}

ServiceResult setAllColours(DocumentSession& session, const std::string& mode) {
    if (mode != "all" && mode != "none" && mode != "invert") {
        return {false, "Mode inconnu : all, none ou invert."};
    }
    std::vector<std::pair<std::uint64_t, bool>> changes;
    for (const auto& o : session.project().embroidery_objects) {
        const bool wanted = mode == "all" ? true : mode == "none" ? false : !o.visible;
        if (wanted != o.visible) {
            changes.emplace_back(o.id.value, wanted);
        }
    }
    const char* label = mode == "all"    ? "Afficher toutes les couleurs"
                        : mode == "none" ? "Masquer toutes les couleurs"
                                         : "Inverser les couleurs affichées";
    return applyChanges(session, changes, label, "L'affichage est déjà celui demandé.");
}

ServiceResult hideSmallerThan(DocumentSession& session, double minAreaMm2) {
    if (!std::isfinite(minAreaMm2) || minAreaMm2 <= 0.0 || minAreaMm2 > 100000.0) {
        return {false, "Taille minimale invalide : plus de 0 mm²."};
    }
    const auto& project = session.project();
    std::vector<std::pair<std::uint64_t, bool>> changes;
    for (const auto& o : project.embroidery_objects) {
        const auto* shape = project.findObject(o.source_vector);
        if (shape == nullptr || !o.visible) {
            continue;
        }
        // Seules les zones fermées ont une surface : un contour ouvert (point de contour) n'est
        // jamais « trop petit » au sens de la V1.
        double um2 = 0.0;
        bool closed = false;
        for (const auto& set : shape->paths) {
            if (set.outer.closed) {
                closed = true;
                um2 += geometry::path_set_area_um2(set);
            }
        }
        if (closed && um2 / 1.0e6 < minAreaMm2) {
            changes.emplace_back(o.id.value, false);
        }
    }
    return applyChanges(session, changes, "Masquer les petites zones",
                        "Aucune zone visible n'est plus petite que cette taille.");
}

std::size_t hiddenObjectCount(const document::Project& project) {
    return static_cast<std::size_t>(
        std::count_if(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                      [](const document::EmbroideryObject& o) { return !o.visible; }));
}

} // namespace openstitch::desktop_v2
