// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "openstitch/stitch/color_block.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::document {

// AD-04 : un fichier machine importé (DST aujourd'hui ; PES/JEF/EXP plus
// tard, HP-FMT-002..005) entre dans le document comme DONNÉE SOURCE
// immuable -- remplace l'ancienne exception desktop
// `MainWindow::sequenceImported_` (§17) : persistée (`.osp`, clé optionnelle,
// rétrocompatible), mutée uniquement via `ICommand` (Tier 0,
// `commands::SetImportedDesignCommand`), restituée par
// `stitch_generation::generate_sequence`/`effective_sequence` comme
// n'importe quel autre contenu du projet (inscription S2a dans
// `generate.cpp`) -- jamais une seconde voie de lecture parallèle.
// Non transformable en P0 (AI-04) : aucune commande d'édition ne cible son
// contenu ; seul son remplacement/suppression (nouvel import, nouveau
// projet) change l'état. L'aiguillage par nature pour une future édition
// (AD-05) est hors scope S2a (S3).
struct ImportedDesign {
    // Identifiant du registre de formats (AI-02, ex. "dst") qui a produit
    // cette séquence -- diagnostic seulement (statistiques, messages),
    // jamais relu pour réinterpréter la séquence.
    std::string source_format;
    // Décodée, positions ABSOLUES en µm -- restituée telle quelle, jamais
    // régénérée depuis des objets (il n'y en a pas pour cette nature).
    stitch::StitchSequence sequence;
    // AD-02 ter, table ORDONNÉE. DST ne porte aucune vraie couleur
    // (roadmap §2 FMT-002) : les blocs d'un design importé DST ont tous une
    // couleur par défaut (cf. `stitch_analysis::color_blocks`), jamais une
    // supposition déguisée en fait.
    std::vector<stitch::ColorBlock> color_blocks;

    // Pas de `= default` : `stitch::StitchSequence` (type existant,
    // `libs/stitch`, hors périmètre S2a -- R4) n'a pas d'`operator==` propre.
    // Comparaison par champ, `commands` inclus (`StitchCommand` en a un).
    bool operator==(const ImportedDesign& other) const {
        return source_format == other.source_format &&
               sequence.commands == other.sequence.commands && color_blocks == other.color_blocks;
    }
};

} // namespace openstitch::document
