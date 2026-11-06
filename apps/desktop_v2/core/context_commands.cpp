// SPDX-License-Identifier: Apache-2.0
#include "context_commands.hpp"

#include <array>

namespace openstitch::desktop_v2 {

namespace {

// Préfixes (ou identifiants exacts) servis près de la sélection. Ajouter une commande ici la rend
// disponible au clic droit ; elle ne peut plus rester « palette seulement ».
constexpr std::array<std::string_view, 17> kContextMenu = {
    "edit.",         "transform.",   "order.",   "selection.", "action.satin.", "action.fill.",
    "preset.apply.", "preset.save",  "node.",    "text.",      "satin.rung.",   "satin.diagnose",
    "fill.guides.",  "fill.breaks.", "arrange.", "point.",     "optimize.order"};

} // namespace

bool commandBelongsTo(CommandSurface surface, std::string_view id) {
    if (surface != CommandSurface::ContextMenu || id == "edit.undo" || id == "edit.redo") {
        return false;
    }
    for (const auto p : kContextMenu) {
        if (id.substr(0, p.size()) == p) {
            return true;
        }
    }
    return false;
}

} // namespace openstitch::desktop_v2
