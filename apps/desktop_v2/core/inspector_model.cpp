// SPDX-License-Identifier: Apache-2.0
#include "inspector_model.hpp"

#include <algorithm>
#include <cstdio>

#include "object_labels.hpp"

namespace openstitch::desktop_v2 {

namespace {

template <typename T> void merge(Common<T>& c, const T& v, bool first) {
    if (first) {
        c.value = v;
    } else if (!(c.value == v)) {
        c.mixed = true;
    }
}

std::string batchOf(const std::vector<std::uint64_t>& ids, const std::string& opTail) {
    if (ids.empty()) {
        return {};
    }
    std::string out = R"({"schema":"openstitch.batch/1","operations":[)";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        const auto mark = opTail.find('#'); // marqueur unique, voir le commentaire plus bas
        out += opTail.substr(0, mark) + std::to_string(ids[i]) + opTail.substr(mark + 1);
    }
    return out + "]}";
}

} // namespace

InspectorState inspect(const document::Project& project, const std::vector<Target>& selection) {
    InspectorState s;
    for (const auto& t : selection) {
        if (t.kind != TargetKind::EmbroideryObject) {
            continue;
        }
        const auto it = std::ranges::find_if(project.embroidery_objects,
                                             [&](const auto& o) { return o.id.value == t.id; });
        if (it == project.embroidery_objects.end()) {
            continue;
        }
        const bool first = s.count == 0;
        ++s.count;
        s.ids.push_back(t.id);
        merge(s.name, it->name, first);
        merge(s.typeId, std::string(stitchTypeId(*it)), first);
        merge(s.rgb, it->rgb, first);
        merge(s.visible, it->visible, first);
        merge(s.locked, it->locked, first);
    }
    return s;
}

const std::vector<SettableType>& settableStitchTypes() {
    static const std::vector<SettableType> kTypes{
        {"running", "Contour"},           {"tatami", "Tatami"},
        {"auto_satin", "Satin"},          {"directional", "Directionnel"},
        {"contour_fill", "Concentrique"}, {"radial_fill", "Radial"},
        {"spiral_fill", "Spirale"},       {"crosshatch_fill", "Croisé"},
        {"motif_fill", "À motif"},        {"motif_run", "Motif le long du contour"},
        {"blanket", "Boutonnière"},       {"cross_stitch", "Point de croix"},
        {"applique", "Appliqué"}};
    return kTypes;
}

std::string settableIdOf(std::string_view objectTypeId) {
    // `stitchTypeId` (étiquettes de l'objet) et les identifiants de `set_stitch_type` diffèrent
    // pour trois remplissages : la liste déroulante doit retrouver le bon type courant.
    if (objectTypeId == "radial") {
        return "radial_fill";
    }
    if (objectTypeId == "spiral") {
        return "spiral_fill";
    }
    if (objectTypeId == "crosshatch") {
        return "crosshatch_fill";
    }
    return std::string(objectTypeId);
}

// `batchOf` remplace le marqueur « # » de `opTail` par l'identifiant : le marqueur n'apparaît dans
// aucune valeur (couleurs, types et booléens sont construits ici, jamais saisis).
std::string batchSetColor(const std::vector<std::uint64_t>& ids,
                          const std::array<std::uint8_t, 3>& rgb) {
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    // Le « # » de la couleur ne doit pas être pris pour le marqueur : on place l'id en premier.
    std::string out;
    if (ids.empty()) {
        return out;
    }
    out = R"({"schema":"openstitch.batch/1","operations":[)";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        out += R"({"op":"set_color","object":)" + std::to_string(ids[i]) + R"(,"rgb":")" + hex +
               R"("})";
    }
    return out + "]}";
}

std::string batchSetStitchType(const std::vector<std::uint64_t>& ids, std::string_view type) {
    const auto& types = settableStitchTypes();
    if (std::ranges::none_of(types, [&](const auto& t) { return t.id == type; })) {
        return {};
    }
    if (type == "auto_satin") {
        return batchAutoSatin(ids, 0);
    }
    return batchOf(ids,
                   R"({"op":"set_stitch_type","object":#,"type":")" + std::string(type) + R"("})");
}

namespace {
struct SatinAttempt {
    const char* strategy;
    bool compactAxis;
};
constexpr std::array<SatinAttempt, 5> kSatinAttempts{
    {{"skeleton", false}, {"parallel_sweep", false}, {"medial_axis", false},
     {"guided_satin", false}, {"skeleton", true}}};
} // namespace

std::size_t satinAttemptCount() {
    return kSatinAttempts.size();
}

std::string batchAutoSatin(const std::vector<std::uint64_t>& ids, std::size_t attempt) {
    if (attempt >= kSatinAttempts.size()) {
        return {};
    }
    const auto& a = kSatinAttempts[attempt];
    return batchOf(ids, std::string(R"({"op":"auto_satin","object":#,"strategy":")") + a.strategy +
                            R"(","compact_axis":)" + (a.compactAxis ? "true" : "false") + "}");
}

std::string batchSetVisible(const std::vector<std::uint64_t>& ids, bool visible) {
    return batchOf(ids, std::string(R"({"op":"set_visible","object":#,"visible":)") +
                            (visible ? "true" : "false") + "}");
}

std::string batchSetLocked(const std::vector<std::uint64_t>& ids, bool locked) {
    return batchOf(ids, std::string(R"({"op":"set_locked","object":#,"locked":)") +
                            (locked ? "true" : "false") + "}");
}

} // namespace openstitch::desktop_v2
