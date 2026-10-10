// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Type de point d'arrêt (mêmes valeurs/ordre que `stitch_generation::LockType`).
enum class LockStitch { None, BackAndForth, Triangle, MicroZigzag };

// Finitions appliquées à la séquence de points APRÈS génération et retouches
// (`stitch_generation::finish_sequence`, Lots E et F de l'audit marine plein
// cadre, 2026-09-22). Réglages de projet : sérialisés dans le .osp, donc le
// même projet produit le même DST sur toute machine.
//
// Compatibilité : un .osp antérieur (sans ce bloc) est relu avec
// `enabled = false` -- séquence strictement identique à avant. Un projet
// NOUVEAU (valeurs par défaut ci-dessous) a les finitions activées.
struct SequenceFinishing {
    bool enabled{true};

    // Lot E -- coupes automatiques : un déplacement plus long que ce seuil
    // devient « point d'arrêt de sortie, coupe, déplacement, point d'arrêt
    // d'entrée ». Plus court : simple saut, sans coupe.
    Micrometers trim_threshold{3'000};
    // Coupe aussi avant chaque changement de fil.
    bool trim_before_color_change{true};

    // Lot E -- points d'arrêt : à l'entrée et à la sortie de chaque objet et
    // autour de chaque coupe (passe `StitchPass::Lock`). Un objet qui porte
    // déjà ses propres verrous (satin `lock_start`/`lock_end`) n'est pas
    // doublé.
    LockStitch lock_type{LockStitch::BackAndForth};
    Micrometers lock_length{800}; // borné à la longueur du point voisin
    int lock_passes{2};

    // Lot F -- points trop courts : un point cousu plus court que ce seuil est
    // fusionné avec le suivant (jamais une passe de verrou, jamais un point
    // retouché à la main, jamais une extrémité de tracé).
    bool filter_short_stitches{true};
    Micrometers min_stitch_length{500};

    // HP-ENG-008 -- longueur maximale de point cousu : un point plus long est découpé en
    // points égaux (même droite, mêmes passes). Désactivé par défaut (`false`) : aucun
    // projet existant ne change ; l'analyse signale de toute façon les points trop longs.
    bool split_long_stitches{false};
    Micrometers max_stitch_length{7'000};

    // HP-ENG-010 -- entrée/sortie automatiques : le sens de couture de chaque objet
    // (remplissage, contour, satin) est choisi pour minimiser le déplacement depuis la fin
    // de l'objet précédent et vers le début du suivant. Désactivé par défaut ; chaque objet
    // peut forcer (`JoinMode::Auto`) ou refuser (`JoinMode::Off`) ce réglage.
    bool auto_join{false};

    bool operator==(const SequenceFinishing&) const = default;

    // Réglage d'un projet antérieur à ces finitions : rien n'est ajouté.
    [[nodiscard]] static SequenceFinishing legacy() {
        SequenceFinishing f;
        f.enabled = false;
        return f;
    }
};

} // namespace openstitch::document
