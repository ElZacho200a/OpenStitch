// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Type de point d'arrêt (mêmes valeurs/ordre que `stitch_generation::LockType`).
enum class LockStitch { None, BackAndForth, Triangle, MicroZigzag };

// HP-SEQ-006.a -- ponts cachés : une coupe entre deux tracés est remplacée par un saut sans
// coupe ou par un trajet cousu quand ce trajet est PROUVÉ recouvert par des points de couche
// supérieure cousus plus tard (preuve calculée sur la séquence, jamais sur les objets). Passe
// finale de `finish_sequence` : aucun objet n'est réordonné. Désactivé par défaut -- un projet
// existant produit le même fichier machine.
struct HiddenTravel {
    bool enabled{false};
    // Écart (distance directe) jusqu'auquel un saut recouvert reste un simple saut, sans coupe
    // ni point d'arrêt. 0 : jamais de saut sans coupe.
    Micrometers max_hidden_jump{2'000};
    // Longueur maximale du trajet cousu (mesurée le long du chemin).
    Micrometers max_bridge_length{15'000};
    // Longueur des points du trajet cousu.
    Micrometers bridge_stitch_length{2'500};
    // Largeur de fil supposée de la couche qui recouvre, et marge de sécurité ajoutée de chaque
    // côté du trajet : le trajet doit rester à plus de (largeur/2 + marge) de toute zone non
    // recouverte.
    Micrometers cover_thread_width{400};
    Micrometers cover_margin{150};
    // « Optimiser les trajets » : trois niveaux, tous appliqués en toute fin, sans jamais modifier
    // la couche supérieure (remplissage, satin visible) ni l'ordre des objets.
    //  1. entre deux objets : pont caché si le trajet est recouvert plus tard ;
    //  2. à l'intérieur d'un objet : liens sous-couche <-> remplissage cachés par le remplissage ;
    //  3. ordre et sens des passes de sous-couche d'un même objet (invisible : sous le
    //  remplissage).
    bool between_objects{true};
    bool within_object{true};
    bool reorder_underlay{true};
    // Mode avancé, plus invasif (désactivé par défaut) : réordonner les régions d'une MÊME couleur
    // pour raccourcir les déplacements. L'ordre relatif de deux régions dont les emprises se
    // touchent est conservé, les régions verrouillées restent en place, aucune région n'est
    // modifiée ni retournée. Appliqué avant les autres niveaux.
    bool reorder_regions{false};
    // HP-SEQ-006.b -- une surface déjà cousue plus tôt avec le MÊME fil compte aussi comme cachée
    // (le fil de liaison s'y confond ; léger relief possible). Désactivé par défaut.
    bool allow_same_color_surface{false};

    bool operator==(const HiddenTravel&) const = default;
};

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

    // HP-SEQ-006.a -- ponts cachés (désactivés par défaut).
    HiddenTravel hidden_travel{};

    // HP-SEQ-008 -- points de départ et d'arrivée du design (µm, repère document). Absents :
    // comportement historique. Servent à l'optimisation de l'ordre de couture (premier objet au
    // plus près du départ, dernier au plus près de l'arrivée) ; la séquence de points n'est pas
    // modifiée.
    std::optional<Vec2um> design_start{};
    std::optional<Vec2um> design_end{};

    bool operator==(const SequenceFinishing&) const = default;

    // Réglage d'un projet antérieur à ces finitions : rien n'est ajouté.
    [[nodiscard]] static SequenceFinishing legacy() {
        SequenceFinishing f;
        f.enabled = false;
        return f;
    }
};

} // namespace openstitch::document
