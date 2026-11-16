// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- fichiers que la V1 savait lire / écrire : broderies machine (DST, PES, JEF, EXP… :
// tout ce que le registre `formats` sait décoder) et tracés DXF. Sans Qt.

#include <filesystem>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

// Un filtre de sélecteur de fichier : libellé français + motifs « *.dst ».
struct FileFilter {
    std::string label;
    std::vector<std::string> patterns;
};

// Formats machine LISIBLES, d'après le registre (jamais une liste codée en dur). Le premier filtre
// les regroupe tous.
[[nodiscard]] std::vector<FileFilter> readableMachineFormats();
// Vrai si l'extension (avec ou sans point, sans casse) est une broderie machine lisible.
[[nodiscard]] bool isMachineDesignFile(const std::filesystem::path& file);

// Ouvre une broderie machine comme NOUVEAU projet : le design arrive centré dans le cadre
// (origine), comme une broderie importée de la V1. Refus clair si le fichier n'est pas lisible.
[[nodiscard]] Result<document::Project> loadMachineProject(const std::filesystem::path& file);

// --- Insertion d'un design dans le projet ouvert (CX08, HP-FILE-007) -------------------------
// Un projet .osp ou une broderie machine (registre `formats`) est fusionné dans le projet courant
// par `commands::InsertDesignCommand` : une seule étape d'annulation, identifiants neufs, liens
// internes réécrits. Une broderie machine arrive centrée dans le cadre, comme à l'ouverture ; les
// objets d'un .osp gardent leur position. Fichier illisible ou combinaison refusée : message
// clair, document inchangé.
struct DesignInsertPreview {
    bool ok{false};
    std::string message; // refus en clair quand !ok
    bool machine{false}; // broderie machine (séquence brute) plutôt que projet éditable
    std::size_t shapes{0};
    std::size_t embroideries{0};
    std::size_t texts{0};
    std::size_t stitches{0}; // commandes de la séquence brute (broderie machine seulement)
    std::size_t colors{0};   // blocs de couleur (broderie machine seulement)
    double widthMm{0};       // emprise des formes ou des points
    double heightMm{0};
};
// Lit le fichier et dit ce qui serait inséré dans `target`, sans rien modifier.
[[nodiscard]] DesignInsertPreview previewDesignInsert(const document::Project& target,
                                                      const std::filesystem::path& file);
// Insère le design en UNE commande sur la session.
ServiceResult insertDesign(DocumentSession& session, const std::filesystem::path& file);

// Écrit tous les contours vectoriels (extérieurs et trous) en DXF. `count` : nombre de tracés
// écrits.
ServiceResult exportVectorsToDxf(const document::Project& project,
                                 const std::filesystem::path& file, std::size_t* count = nullptr);

// Liste des fils du motif en CSV (UTF-8, séparateur « ; », une ligne par fil, dans l'ordre de
// première couture) :
// ordre;marque;nuancier;reference;nom;couleur;objets;points;longueur_mm;duree_s. Refus clair s'il
// n'y a aucun point ou si le fichier ne s'écrit pas. `rows` : nombre de fils écrits.
ServiceResult exportThreadListCsv(const document::Project& project,
                                  const std::filesystem::path& file, std::size_t* rows = nullptr);

} // namespace openstitch::desktop_v2
