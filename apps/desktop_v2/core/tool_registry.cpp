// SPDX-License-Identifier: Apache-2.0
#include "tool_registry.hpp"

#include <algorithm>

namespace openstitch::desktop_v2 {

std::string_view workspaceId(Workspace w) {
    switch (w) {
    case Workspace::Design:
        return "design";
    case Workspace::Digitize:
        return "digitize";
    case Workspace::Optimize:
        return "optimize";
    case Workspace::Produce:
        return "produce";
    }
    return "design";
}

void ToolRegistry::add(ToolDescriptor tool) {
    tools_.push_back(std::move(tool));
}

const ToolDescriptor* ToolRegistry::find(std::string_view id) const {
    const auto it = std::ranges::find_if(tools_, [&](const auto& t) { return t.id == id; });
    return it == tools_.end() ? nullptr : &*it;
}

const ToolDescriptor* ToolRegistry::findByLegacy(std::string_view legacyTool) const {
    const auto it =
        std::ranges::find_if(tools_, [&](const auto& t) { return t.legacyTool == legacyTool; });
    return it == tools_.end() ? nullptr : &*it;
}

std::vector<const ToolDescriptor*> ToolRegistry::forWorkspace(Workspace w) const {
    std::vector<const ToolDescriptor*> out;
    for (const auto& t : tools_) {
        if ((t.workspaces & workspaceBit(w)) != 0) {
            out.push_back(&t);
        }
    }
    return out;
}

std::vector<std::string> ToolRegistry::conflicts() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < tools_.size(); ++i) {
        for (std::size_t j = i + 1; j < tools_.size(); ++j) {
            const auto& a = tools_[i];
            const auto& b = tools_[j];
            if (a.id == b.id) {
                out.push_back("identifiant en double : " + a.id);
            }
            if (!a.shortcut.empty() && a.shortcut == b.shortcut &&
                (a.workspaces & b.workspaces) != 0) {
                out.push_back("raccourci « " + a.shortcut + " » : " + a.id + " et " + b.id);
            }
        }
    }
    return out;
}

ToolRegistry ToolRegistry::builtin() {
    constexpr auto D = static_cast<std::uint8_t>(1U << 0); // Conception
    constexpr auto N = static_cast<std::uint8_t>(1U << 1); // Numérisation
    constexpr auto O = static_cast<std::uint8_t>(1U << 2); // Analyse et optimisation
    constexpr auto P = static_cast<std::uint8_t>(1U << 3); // Production
    ToolRegistry r;
    //          id                 libellé                          raccourci  espaces        geste
    //          outil historique
    r.add({"select", "Sélection", "V", kAllWorkspaces, Gesture::None, "Select"});
    r.add({"pan", "Déplacer la vue", "H", kAllWorkspaces, Gesture::None, "Pan"});
    r.add({"crop", "Recadrer / sélection rectangle", "C", D, Gesture::DragBox, "Rect"});
    r.add({"draw.rectangle", "Rectangle", "R", D, Gesture::DragBox, "DrawRectangle"});
    r.add({"draw.ellipse", "Ellipse", "E", D, Gesture::DragBox, "DrawEllipse"});
    r.add({"draw.polygon", "Polygone", "Y", D, Gesture::ClickSeries, "DrawPolygon"});
    r.add({"draw.polygon-regular", "Polygone régulier", "Maj+Y", D, Gesture::DragBox,
           "DrawPolygonRegular"});
    r.add({"draw.bezier", "Plume (courbes)", "P", D, Gesture::ClickDrag, "DrawBezier"});
    r.add({"draw.freeform", "Crayon (main levée)", "N", D, Gesture::Freehand, "DrawFreeform"});
    r.add({"satin.column", "Colonne satin", "S", N, Gesture::ClickSeries, "DrawSatinColumn"});
    r.add({"fill.direction-guide", "Guide de direction", "D", N, Gesture::ClickSeries,
           "DrawDirectionGuide"});
    r.add(
        {"fill.break-line", "Ligne de rupture", "Maj+D", N, Gesture::ClickSeries, "DrawBreakLine"});
    r.add(
        {"knife", "Couteau", "K", static_cast<std::uint8_t>(D | N), Gesture::TwoPointLine, "Cut"});
    r.add({"text", "Texte", "T", static_cast<std::uint8_t>(D | N), Gesture::Pick, "Text"});
    r.add({"measure", "Règle", "M", kAllWorkspaces, Gesture::TwoPointLine, "Measure"});
    r.add({"eyedropper", "Pipette", "I", static_cast<std::uint8_t>(D | N | P), Gesture::Pick,
           "Eyedropper"});
    r.add({"warp-brush", "Pinceau de déformation", "W", D, Gesture::Stroke, "Brush"});
    r.add({"bucket", "Pot de peinture", "G", D, Gesture::Pick, "Bucket"});
    r.add({"click-to-stitch", "Clic-pour-broder", "A", N, Gesture::Pick, "ClickToStitch"});
    r.add({"detail-brush", "Zones de détail", "Q", D, Gesture::Stroke, "DetailBrush"});
    // Nouveau en V2 (pas d'équivalent dans l'ancien enum) : retouche point à point.
    // Édition directe des nœuds du contour (poignées, glisser, insertion par double-clic).
    r.add({"node.edit", "Nœuds du contour", "B", static_cast<std::uint8_t>(D | N), Gesture::Pick,
           ""});
    // Axe d'un fondu de deux fils : début et fin de la transition, déplacement, rotation.
    r.add({"fade.axis", "Axe du fondu", "L", static_cast<std::uint8_t>(D | N), Gesture::Pick, ""});
    // Correction de la segmentation après numérisation : choisir, supprimer, fusionner des régions.
    r.add({"region.edit", "Régions", "X", static_cast<std::uint8_t>(D | N), Gesture::Pick, ""});
    // Retouche d'une frontière de segmentation : peindre la région choisie (Ctrl = effacer).
    r.add({"region.brush", "Pinceau de frontière", "U", static_cast<std::uint8_t>(D | N),
           Gesture::Stroke, ""});
    r.add({"stitch.points", "Retouche de points", "J", static_cast<std::uint8_t>(N | O),
           Gesture::Pick, ""});
    (void)P;
    return r;
}

} // namespace openstitch::desktop_v2
