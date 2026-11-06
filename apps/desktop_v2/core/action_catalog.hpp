// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- catalogue d'ACTIONS paramétrées : chaque capacité du moteur exposée par une
// opération de lot (`project_ops`) est déclarée ICI, une fois, avec ses champs (libellé, nature,
// plage, défaut), sa validation et la construction de son lot JSON. L'interface en tire
// automatiquement un dialogue, un menu et une entrée de palette ; Claude et la CLI utilisent les
// mêmes opérations. Ajouter une famille = ajouter une définition + un test. Sans Qt.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "openstitch/document/project.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

enum class FieldKind { Number, Integer, Bool, Text, Choice, File };

struct Choice {
    std::string value; // valeur envoyée au moteur
    std::string label; // libellé affiché
};

struct FieldDef {
    std::string key;
    std::string label;
    FieldKind kind{FieldKind::Number};
    double min{0};
    double max{0};
    std::string def;  // valeur par défaut (texte : nombres écrits en clair, booléens 0 / 1)
    std::string unit; // affichage seulement
    std::vector<Choice> choices; // FieldKind::Choice
    bool required{false};        // Text / File : non vide
};

using Values = std::map<std::string, std::string>;

struct ActionContext {
    std::vector<std::uint64_t> ids; // objets de broderie sélectionnés
    PointMm centre;                 // centre de leur emprise, mm modèle (0, 0 sans emprise)
    const document::Project* project{nullptr};
};

struct ActionDef {
    std::string id;    // stable : « satin.auto », « file.import_svg »…
    std::string label; // français = langue source
    std::string group; // « Fichier », « Satin », « Texte »…
    std::vector<FieldDef> fields;
    bool needsSelection{false};
    // Actions qui passent par une ICommand (et non un lot JSON) : exécutées par cette fonction,
    // après validation des champs. `build` reste vide pour elles.
    std::function<ServiceResult(DocumentSession&, const Values&, const ActionContext&)> run;
    // Construit le lot ; chaîne vide + `error` si le contexte ne convient pas.
    std::function<std::string(const Values&, const ActionContext&, std::string& error)> build;
};

[[nodiscard]] const std::vector<ActionDef>& actionCatalog();
[[nodiscard]] const ActionDef* findAction(const std::string& id);

// Valeurs par défaut de tous les champs de l'action.
[[nodiscard]] Values defaultValues(const ActionDef& action);

// Valide `values` (valeurs absentes = défaut) puis construit le lot. Erreur lisible dans `error`,
// chaîne vide en retour : aucune valeur n'est jamais « corrigée » en silence.
// Valide les champs et le contexte (valeurs absentes = défaut) SANS rien construire ni exécuter.
[[nodiscard]] bool validateAction(const ActionDef& action, Values& values,
                                  const ActionContext& context, std::string& error);

[[nodiscard]] std::string buildAction(const ActionDef& action, Values values,
                                      const ActionContext& context, std::string& error);

// --- Modification d'un texte existant (hors catalogue : elle exige un texte sélectionné) ---------
// Identifiant de l'objet texte qui possède TOUTES les lettres sélectionnées ; nullopt si la
// sélection est vide, mélange plusieurs textes ou contient autre chose que des lettres.
[[nodiscard]] std::optional<std::uint64_t>
selectedTextOwner(const document::Project& project, const std::vector<std::uint64_t>& objectIds);
// Réglages actuels du texte, dans les champs de `textEditAction()`.
[[nodiscard]] Values textEditValues(const document::Project& project, std::uint64_t textId);
// Action « Modifier le texte » : mêmes champs typographiques que « Ajouter du texte », police et
// position conservées ; `build` produit l'opération `set_text` pour le texte de la sélection.
[[nodiscard]] const ActionDef& textEditAction();

// --- Édition d'UNE lettre d'un texte (GlyphEdit du moteur : décalage, rotation, échelle, couleur)
// --- Les lettres n'ont pas d'identifiant propre : le rang d'une lettre (espaces exclus, ordre de
// lecture) se déduit de l'ordre des formes du texte, une lettre pouvant en produire plusieurs
// (« Lettre A », « Lettre A (2) »). Si le compte ne tombe pas juste avec le texte (ligatures,
// caractères combinés ou ambigus, écriture de droite à gauche), la commande REFUSE plutôt que de
// modifier la mauvaise lettre.
struct GlyphTarget {
    bool ok{false};
    std::string message; // refus en clair quand !ok
    std::uint64_t textId{0};
    std::uint32_t index{0};
    std::string letter; // la lettre, pour l'affichage
    bool hasEdit{false};
};
[[nodiscard]] GlyphTarget glyphTargetOf(const document::Project& project,
                                        const std::vector<std::uint64_t>& objectIds);
// Réglages actuels de la lettre (neutres si elle n'a jamais été retouchée).
[[nodiscard]] Values glyphEditValues(const document::Project& project, const GlyphTarget& target);
// Actions « Modifier la lettre » (champs ci-dessus) et « Rétablir la lettre » (sans champ).
[[nodiscard]] const ActionDef& glyphEditAction();
[[nodiscard]] const ActionDef& glyphResetAction();
// Action « Supprimer le texte » : retire l'objet texte de la sélection avec TOUTES ses lettres
// (formes et broderies), en une étape d'annulation.
[[nodiscard]] const ActionDef& textRemoveAction();
// Actions qui exigent un contexte (texte ou lettre sélectionnée) et restent hors catalogue :
// `text.edit`, `text.glyph`, `text.glyph.reset`, `text.remove`. nullptr si l'identifiant n'en est
// pas une.
[[nodiscard]] const ActionDef* contextAction(const std::string& id);

} // namespace openstitch::desktop_v2
