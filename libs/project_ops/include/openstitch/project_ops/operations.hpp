// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

inline constexpr const char* kBatchSchema = "openstitch.batch/1";
inline constexpr const char* kBatchReportSchema = "openstitch.batch-report/1";
inline constexpr const char* kCatalogSchema = "openstitch.operations/1";

struct BatchOptions {
    // Valide ET simule le lot sur une copie du projet (mêmes refus qu'une application réelle),
    // sans jamais modifier `project`.
    bool dry_run{false};
};

struct OperationOutcome {
    std::size_t index{}; // position (0-based) dans le lot
    std::string op;      // nom de l'opération (vide si le JSON du lot est invalide)
    bool ok{false};
    std::string code;        // `code::k...` si !ok
    std::string message;     // refus en clair
    std::string hint;        // piste de correction
    std::string result_json; // objet JSON du résultat (ex. {"created":[41]}), "" = aucun
};

struct BatchReport {
    bool ok{false};      // toutes les opérations validées ET (hors dry-run) appliquées
    bool applied{false}; // `project` a effectivement été modifié (faux en dry-run et en échec)
    bool dry_run{false};
    std::size_t operation_count{0};
    std::vector<OperationOutcome> outcomes; // une entrée par opération examinée, dans l'ordre
    // Erreur globale (JSON du lot illisible, schéma non reconnu…) ; vide si ok.
    std::string error_code;
    std::string error_message;

    // JSON du rapport (schéma `openstitch.batch-report/1`), déterministe.
    [[nodiscard]] std::string to_json() const;
};

// Applique un lot d'opérations de façon TRANSACTIONNELLE :
//  - le JSON est validé en entier (schéma, noms d'opérations, types d'arguments) AVANT toute
//    application ;
//  - les opérations sont ensuite jouées dans l'ordre sur une COPIE du projet, chacune par des
//    `commands::ICommand` (jamais de mutation directe) ;
//  - dès qu'une opération est refusée, la copie est abandonnée : `project` reste strictement
//    inchangé (`applied == false`), le rapport désigne l'opération fautive et les suivantes
//    ne sont pas examinées ;
//  - en cas de succès (hors dry-run), `project` reçoit l'état final en une fois.
// La CLI écrit ensuite le .osp de façon atomique (project_io::save_project) ; ce module
// n'écrit jamais de fichier.
//
// Format du lot : {"schema":"openstitch.batch/1","operations":[{"op":"set_color",
// "object":12,"rgb":"#FF0000"}, ...]}. Une opération peut porter "as":"alias" ; les
// opérations suivantes peuvent écrire "$alias" à la place d'un identifiant (objets créés
// dans le même lot : duplicate_object, add_shape, split…). Le catalogue complet (noms,
// arguments, unités) est `operations_catalog_json()`.
[[nodiscard]] BatchReport apply_batch(document::Project& project, std::string_view batch_json,
                                      const BatchOptions& options = {});

// Une seule opération (même chemin que le lot : `{"op":"set_color",...}` sans enveloppe) :
// c'est ce qu'utilisent les commandes individuelles de la CLI (`project op ...`).
[[nodiscard]] BatchReport apply_operation(document::Project& project, std::string_view op_json,
                                          const BatchOptions& options = {});

// Catalogue machine-lisible (schéma `openstitch.operations/1`) : pour chaque opération son
// nom, un résumé, la liste des arguments (nom, type JSON, unité, plage, obligatoire) et un
// exemple. Même source de vérité que le dispatcher (impossible de documenter une opération
// qui n'existe pas, ou l'inverse).
[[nodiscard]] std::string operations_catalog_json();

} // namespace openstitch::project_ops
