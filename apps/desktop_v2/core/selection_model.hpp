// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- modèle de sélection UNIQUE (principe I de docs/desktop-v2/04-ux-design-system.md).
// Le canevas, l'arbre documentaire, l'inspecteur, les menus contextuels ET le compagnon IA lisent
// et écrivent cet objet, jamais une copie locale. Sans Qt : testable sans fenêtre ni boucle
// d'événements.

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace openstitch::desktop_v2 {

enum class TargetKind : std::uint8_t {
    Region,           // région de segmentation (phase Conception)
    VectorObject,     // objet vectoriel (phase Conception)
    EmbroideryObject, // objet de broderie (phase Numérisation)
};

struct Target {
    TargetKind kind{TargetKind::EmbroideryObject};
    std::uint64_t id{0};
    auto operator<=>(const Target&) const = default;
};

// Ensemble ordonné sans doublon ; la DERNIÈRE cible ajoutée est la cible principale (celle que
// l'inspecteur détaille). Chaque changement effectif incrémente `revision()` et notifie une fois.
class SelectionModel {
public:
    using Listener = std::function<void(const SelectionModel&)>;
    using Token = std::size_t;

    void replace(std::vector<Target> targets);
    void add(Target target);    // déjà présente : devient la principale
    void toggle(Target target); // Ctrl+clic
    void clear();

    // Retire les cibles disparues du document (après undo/redo/ouverture). Notifie si ça change.
    void prune(const std::function<bool(const Target&)>& exists);

    [[nodiscard]] const std::vector<Target>& targets() const { return targets_; }
    [[nodiscard]] std::optional<Target> primary() const;
    [[nodiscard]] bool contains(Target target) const;
    [[nodiscard]] bool empty() const { return targets_.empty(); }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

    [[nodiscard]] Token subscribe(Listener listener);
    void unsubscribe(Token token);

private:
    void commit(std::vector<Target> next);
    std::vector<Target> targets_;
    std::uint64_t revision_{0};
    std::vector<std::pair<Token, Listener>> listeners_;
    Token nextToken_{1};
    bool notifying_{false};
};

} // namespace openstitch::desktop_v2
