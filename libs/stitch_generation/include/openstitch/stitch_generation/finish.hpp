// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// Finitions de la séquence (Lots E et F, audit marine plein cadre
// 2026-09-22), réglées par `project.finishing` (`document::SequenceFinishing`).
// Troisième et dernière passe de `effective_sequence`, APRÈS les retouches
// manuelles : les index des retouches visent la vue brute (`raw_slice`), que
// cette passe ne modifie pas.
//
// Un « tracé » est une suite de points cousus consécutifs ; entre deux tracés
// se trouvent des déplacements (Jump), coupes ou changements de fil.
// - Déplacement plus long que `trim_threshold` (ou changement de fil si
//   `trim_before_color_change`) : point d'arrêt de sortie, `Trim`,
//   déplacement, point d'arrêt d'entrée.
// - Déplacement entre deux objets distincts : toujours une coupe, quelle que
//   soit sa longueur (le seuil ne protège que les sauts internes à un objet).
// - Déplacement plus court, dans le même objet : simple saut, sans coupe ni
//   verrou.
// - Changement d'objet : toujours un point d'arrêt de sortie et d'entrée
//   (sauf si l'objet porte déjà les siens, passe `Lock`).
// Les verrous (`lock_stitches`, passe `Lock`) sont posés le long du point
// voisin du tracé, donc sur la matière de l'objet. `enabled == false` :
// séquence rendue telle quelle. Fonction pure et déterministe.
[[nodiscard]] stitch::StitchSequence finish_sequence(const stitch::StitchSequence& sequence,
                                                     const document::Project& project);

// HP-SEQ-006.c -- bilan de la passe de ponts cachés (`project.finishing.hidden_travel`).
// « Écart candidat » : écart entre deux tracés qui aurait reçu une coupe, de même fil et sans coupe
// posée à la main. Les écarts de changement de couleur ou de coupe retouchée sont comptés à part
// et ne sont jamais modifiés.
struct TrimReport {
    std::size_t candidate_gaps{0};
    std::size_t hidden_jumps{0};     // coupe remplacée par un saut recouvert (aucun point ajouté)
    std::size_t bridges{0};          // coupe remplacée par un trajet cousu caché
    std::size_t refused_too_long{0}; // trajet direct ou cousu au-delà de `max_bridge_length`
    std::size_t refused_no_cover{0}; // une extrémité n'est pas recouverte plus tard
    std::size_t refused_no_path{0};  // aucun chemin entièrement recouvert
    std::size_t kept_color_change{0}; // coupe conservée : changement de couleur ou arrêt
    std::size_t kept_manual_trim{0};  // coupe conservée : posée à la main ou déjà présente
    std::size_t bridge_stitches{0};   // points cousus ajoutés
    double bridge_length_um{0.0};     // longueur de fil ajoutée
    std::size_t trims_removed{0};     // coupes supprimées (= saut recouvert + ponts)
    std::size_t skipped_by_scope{
        0}; // écart exclu par le niveau choisi (entre objets / dans un objet)
    std::size_t underlay_blocks{0};   // groupes de passes de sous-couche réordonnés
    double underlay_jump_saved_um{0}; // longueur de saut économisée par ce réordonnancement
    std::size_t forced_trims{0};      // connecteur d'objet « toujours couper » (HP-SEQ-005)
    std::size_t forced_no_trim{0};    // connecteur d'objet « ne jamais couper » (HP-SEQ-005)
};

struct FinishResult {
    stitch::StitchSequence sequence;
    TrimReport report;
};

// Même passe que `finish_sequence`, avec le bilan des ponts cachés.
[[nodiscard]] FinishResult finish_sequence_report(const stitch::StitchSequence& sequence,
                                                  const document::Project& project);

// Idem avec des finitions explicites au lieu de `project.finishing` : l'export peut demander les
// ponts cachés pour UN fichier sans modifier le projet.
[[nodiscard]] FinishResult finish_sequence_report(const stitch::StitchSequence& sequence,
                                                  const document::Project& project,
                                                  const document::SequenceFinishing& finishing);

} // namespace openstitch::stitch_generation
