// SPDX-License-Identifier: Apache-2.0
#include "context_commands.hpp"

#include <algorithm>
#include <array>

namespace openstitch::desktop_v2 {

namespace {

// Préfixes (ou identifiants exacts) servis près de la sélection. Ajouter une commande ici la rend
// disponible au clic droit ; elle ne peut plus rester « palette seulement ».
constexpr std::array<std::string_view, 17> kContextMenu = {
    "edit.",         "transform.",   "order.",   "selection.", "action.satin.", "action.fill.",
    "preset.apply.", "preset.save",  "node.",    "text.",      "satin.rung.",   "satin.diagnose",
    "fill.guides.",  "fill.breaks.", "arrange.", "point.",     "optimize.order"};

// Barre flottante : quelques gestes fréquents par contexte (les autres restent au clic droit et
// dans la palette).
constexpr std::array<std::string_view, 5> kObjectBar = {
    "edit.duplicate", "transform.rotate.cw", "transform.mirror.h", "order.front", "edit.delete"};
constexpr std::array<std::string_view, 2> kTextBar = {"text.edit", "text.remove"};
constexpr std::array<std::string_view, 3> kSatinBar = {"satin.rung.insert", "satin.rung.remove",
                                                       "satin.diagnose"};
constexpr std::array<std::string_view, 3> kNodesBar = {"node.smooth", "node.corner",
                                                       "node.hole.remove"};

template <std::size_t N>
void append(std::vector<std::string_view>& out, const std::array<std::string_view, N>& ids) {
    for (const auto id : ids) {
        if (std::find(out.begin(), out.end(), id) == out.end()) {
            out.push_back(id);
        }
    }
}

} // namespace

std::vector<std::string_view> floatingBarCommands(std::span<const CommandContext> active) {
    std::vector<std::string_view> out;
    for (const auto c : active) {
        switch (c) {
        case CommandContext::Object:
            append(out, kObjectBar);
            break;
        case CommandContext::Text:
            append(out, kTextBar);
            break;
        case CommandContext::Satin:
            append(out, kSatinBar);
            break;
        case CommandContext::Nodes:
            append(out, kNodesBar);
            break;
        }
    }
    return out;
}

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
