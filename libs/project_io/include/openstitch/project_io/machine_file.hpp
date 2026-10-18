// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

#include "openstitch/core/error.hpp"
#include "openstitch/document/imported_design.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/formats/color_companion.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/stitch_generation/finish.hpp"

namespace openstitch::project_io {

// AD-03/AI-03b : composition « projet -> fichier machine » / « fichier
// machine -> design importé », réutilisée à l'identique par le desktop
// (menus Importer/Exporter) et la CLI -- la SEULE logique de ce genre,
// jamais dupliquée. `formats` reste sans dépendance sur `document` (AD-02) ;
// cette composition est ce qui les relie. Générique sur
// `formats::registered_formats()` : S2b/S2c (PES, JEF, EXP) n'ont besoin
// d'aucune modification ici, seulement d'une ligne de registre (cf.
// `libs/formats/src/format_registry.cpp`).
//
// `format_id` nomme une entrée du registre : "dst", "pes", "jef", "exp".

// Exporte `project` (sa séquence EFFECTIVE -- retouches manuelles incluses,
// cf. `stitch_generation::effective_sequence`, jamais `generate_sequence`
// directement) vers un fichier du format `format_id`, à `path`.
//
// `options` (HP-FMT-002..005) : coupes, arrêts, changements de couleur, nom du motif. Si
// `options.block_colors` est vide, les couleurs des blocs sont celles des objets du projet
// (`stitch_analysis::color_blocks`) ; elles ne servent qu'aux formats qui portent des couleurs
// (PES, JEF). Sans effet sur le DST (octets inchangés).
// Optimisations demandées pour CET export (HP-SEQ-006) : appliquées en toute fin, sur la séquence
// effective, sans modifier le projet. `reduce_trims` : remplace des coupes par des sauts ou des
// ponts cousus prouvés cachés (`document::HiddenTravel` ; `enabled` est forcé). Désactivé : les
// réglages de ponts cachés du PROJET s'appliquent seuls, comme pour l'aperçu.
struct ExportOptimizations {
    bool reduce_trims{false};
    document::HiddenTravel hidden_travel{};

    bool operator==(const ExportOptimizations&) const = default;
};

// Séquence exactement exportée pour ces options, avec le bilan des coupes (aperçu avant export).
[[nodiscard]] Result<stitch_generation::FinishResult>
export_sequence(const document::Project& project, const ExportOptimizations& optimizations = {});

// Séquence précédée des deux sauts qui placent l'origine machine sur `start` (voir
// `formats::ExportStart`). Identité pour `StartMode::FirstPoint`. Les commandes d'origine ne sont
// ni modifiées ni réordonnées.
[[nodiscard]] stitch::StitchSequence with_start_point(stitch::StitchSequence sequence,
                                                      const formats::ExportStart& start);

// HP-FMT-009 / HP-FMT-005 : fichier compagnon de couleurs écrit à côté d'un DST ou d'un EXP
// (formats sans couleurs de fil réelles). `write` est l'entrée ; `written` et `error` sont les
// sorties. Le compagnon est écrit APRÈS le fichier machine : un échec ici (`error` non vide)
// n'invalide pas l'export, qui reste réussi. Sans effet pour un format qui porte ses couleurs
// (PES, JEF) : rien n'est écrit, `written` et `error` restent vides.
struct ColorCompanionExport {
    bool write{false};
    formats::CompanionKind kind{formats::CompanionKind::Csv};
    std::filesystem::path written; // sortie : chemin du compagnon écrit
    std::string error;             // sortie : message si l'écriture du compagnon a échoué
};

[[nodiscard]] Result<void> export_machine_file(const document::Project& project,
                                               const std::string& format_id,
                                               const std::filesystem::path& path,
                                               const formats::MachineExportOptions& options = {},
                                               const ExportOptimizations& optimizations = {},
                                               stitch_generation::TrimReport* report = nullptr,
                                               ColorCompanionExport* companion = nullptr);

// Importe un fichier machine comme design importé (AD-04, AI-04) --
// construit la valeur à placer dans `Project::imported_design` (l'appelant
// décide, via une commande, si/comment elle entre dans un document). Pour un format sans couleurs
// de fil (DST, EXP), un fichier compagnon de couleurs voisin (`.inf` puis `.csv`, même nom) fournit
// les couleurs des blocs ; absent ou invalide, les couleurs par défaut sont conservées. Si
// `format_id` est vide, déduit du registre à partir de l'extension de
// `path` (minuscule, sans le point).
[[nodiscard]] Result<document::ImportedDesign>
import_machine_file(const std::filesystem::path& path, std::string format_id = {});

} // namespace openstitch::project_io
