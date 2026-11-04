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

} // namespace openstitch::desktop_v2
