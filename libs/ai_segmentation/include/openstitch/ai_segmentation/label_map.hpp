// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::ai_segmentation {

// Un masque retenu pour la construction de la carte de labels, avec son
// bitmap déjà chargé depuis le PNG binaire écrit par le worker (0 = hors
// masque, non-zéro = dans le masque) et les choix issus de la revue.
struct LabelMaskInput {
    int mask_id{0};
    std::vector<std::uint8_t> pixels;  // width*height, ligne par ligne
    std::array<std::uint8_t, 3> rgb{};
    // Priorité manuelle (réordonnancement explicite par l'utilisateur dans la
    // revue) : prioritaire sur toute règle automatique si posée.
    bool manual_priority_set{false};
    int manual_priority{0};  // plus petit = prioritaire
    bool is_protected{false};
    double predicted_iou{0.0};
    double stability_score{0.0};
};

struct LabelMapOptions {
    int width{0};
    int height{0};
};

// Résout les recouvrements entre masques retenus et produit une
// `segmentation::Segmentation` SANS chevauchement : chaque pixel appartient
// à zéro ou exactement un label final (invariant). Ordre de résolution des
// conflits, du plus prioritaire au moins prioritaire : priorité manuelle
// explicite, puis stability_score, puis predicted_iou, puis aire (plus
// grande d'abord), puis ordre de `masks` (déterministe). La correction
// manuelle pixel à pixel ("choix manuel" au sens le plus fin) n'est pas
// couverte ici — hors MVP, cf. docs/source pour le suivi.
//
// Un slot de région est créé pour CHAQUE entrée de `masks`, dans son ordre
// d'origine (RegionId stable = index+1) ; une région totalement recouverte
// par une région plus prioritaire obtient un `pixel_count` de zéro et sera
// silencieusement ignorée par la vectorisation, comme pour toute région non
// vectorisable.
[[nodiscard]] Result<segmentation::Segmentation> build_label_map(
    const std::vector<LabelMaskInput>& masks, const LabelMapOptions& options);

}  // namespace openstitch::ai_segmentation
