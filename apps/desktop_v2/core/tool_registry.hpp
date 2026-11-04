// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- espaces de travail et registre d'outils. Un outil est déclaré UNE fois (id, espace,
// raccourci) : barre d'outils, palette de commandes, aide et tests lisent ce même registre
// (principe C : une intention = une interaction cohérente). Sans Qt.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openstitch::desktop_v2 {

// Un espace de travail est un FILTRE d'attention sur le même document (même projet, même sélection,
// même historique) : il choisit les panneaux et les outils mis en avant, il n'isole rien. Tout
// outil reste atteignable depuis la palette de commandes dans n'importe quel espace.
enum class Workspace : std::uint8_t { Design, Digitize, Optimize, Produce };

inline constexpr std::uint8_t kAllWorkspaces = 0x0F;
[[nodiscard]] constexpr std::uint8_t workspaceBit(Workspace w) {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(w));
}
[[nodiscard]] std::string_view workspaceId(Workspace w);

// Contexte d'interaction du canevas où l'outil s'applique (comportement souris, voir l'ADR 0003).
enum class Gesture : std::uint8_t {
    None,         // outil sans geste (Sélection, Pan : géré par le canevas)
    DragBox,      // cadre glissé
    ClickSeries,  // clics successifs terminés par Entrée / double-clic
    ClickDrag,    // clic = coin, clic-glisser = nœud lisse
    Freehand,     // main levée
    Stroke,       // coup de pinceau
    Pick,         // un clic échantillonne / agit
    TwoPointLine, // glisser = segment (règle, couteau)
};

struct ToolDescriptor {
    std::string id;       // stable, jamais traduit (« draw.ellipse »)
    std::string label;    // clé de libellé (français = langue source)
    std::string shortcut; // « E », « Maj+Y » ; vide = aucun raccourci
    std::uint8_t workspaces{kAllWorkspaces};
    Gesture gesture{Gesture::None};
    std::string legacyTool; // enumérateur de apps/desktop/tools.hpp couvert (matrice de parité)
};

class ToolRegistry {
public:
    // Registre des outils de Desktop V2 (équivalent de l'enum `Tool` historique).
    [[nodiscard]] static ToolRegistry builtin();

    void add(ToolDescriptor tool);
    [[nodiscard]] const std::vector<ToolDescriptor>& tools() const { return tools_; }
    [[nodiscard]] const ToolDescriptor* find(std::string_view id) const;
    [[nodiscard]] const ToolDescriptor* findByLegacy(std::string_view legacyTool) const;
    // Outils mis en avant dans un espace (ordre du registre).
    [[nodiscard]] std::vector<const ToolDescriptor*> forWorkspace(Workspace w) const;

    // Conflits de raccourci : deux outils visibles ensemble dans au moins un espace avec le même
    // raccourci. Vide = registre cohérent.
    [[nodiscard]] std::vector<std::string> conflicts() const;

private:
    std::vector<ToolDescriptor> tools_;
};

} // namespace openstitch::desktop_v2
