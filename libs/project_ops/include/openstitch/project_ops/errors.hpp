// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace openstitch::project_ops {

// Codes d'erreur STABLES de l'interface programmatique (CLI --json, rapports de lot).
// Contrat de stabilité : un code publié n'est jamais renommé ni réutilisé pour autre
// chose ; on peut en ajouter. Le `message` est destiné à l'humain, le `code` aux
// scripts et aux agents. Liste documentée dans docs/source/cli-project.md.
namespace code {
inline constexpr const char* kUsage = "E_USAGE";                  // argument manquant/mal typé
inline constexpr const char* kUnknownOp = "E_UNKNOWN_OP";         // opération inconnue
inline constexpr const char* kUnknownObject = "E_UNKNOWN_OBJECT"; // identifiant introuvable
inline constexpr const char* kUnknownAlias = "E_UNKNOWN_ALIAS";   // alias `$x` jamais défini
inline constexpr const char* kOutOfRange = "E_OUT_OF_RANGE";      // valeur hors plage métier
inline constexpr const char* kWrongType = "E_WRONG_STITCH_TYPE";  // champ absent du type de points
inline constexpr const char* kRefused = "E_REFUSED";              // refus métier (message du moteur)
inline constexpr const char* kSchema = "E_SCHEMA";                // JSON de lot invalide
inline constexpr const char* kIo = "E_IO";                        // fichier illisible/non écrit
inline constexpr const char* kInvalidProject = "E_INVALID_PROJECT"; // .osp invalide
inline constexpr const char* kNothingToDo = "E_NOTHING_TO_DO";    // rien produit (aucun objet)
inline constexpr const char* kInternal = "E_INTERNAL";            // défaut interne
} // namespace code

// Erreur structurée montrable telle quelle (texte) ou en JSON.
struct OpError {
    std::string code;    // `code::k...`
    std::string message; // phrase pour l'humain
    std::string hint;    // piste de correction, vide = aucune
};

} // namespace openstitch::project_ops
