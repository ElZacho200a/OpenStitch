// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

// HP-FMT-002..005 : options d'export communes aux codecs machine qui portent des couleurs
// (PES, JEF) ou des commandes natives (coupe, arrêt) -- EXP et PES/JEF les lisent toutes ;
// le DST garde son API propre (`DstWriteOptions`).
//
// Coupe (`Trim`) : `Native` = codage du format (drapeau PEC, 3 sauts nuls JEF, code 0x80 0x80
// EXP) ; `Drop` = la coupe devient un simple saut.
enum class TrimMode : std::uint8_t { Native, Drop };
// Arrêt machine (`Stop`) : `Native` = représentation propre au format (PES : changement de
// fil vers le MÊME fil ; JEF : entrée 0 de la table des couleurs ; EXP : marqueur d'arrêt/
// couleur unique) ; `AsColorChange` = traité comme un changement de couleur ordinaire ;
// `Drop` = supprimé (le déplacement éventuel est conservé en saut).
enum class StopMode : std::uint8_t { Native, AsColorChange, Drop };
// Changements de couleur : `Emit` = écrits ; `Drop` = supprimés (design monochrome).
enum class ColorChangeMode : std::uint8_t { Emit, Drop };

struct MachineExportOptions {
    std::string design_name{"OPENSTITCH"}; // nom du motif (PES : 16 caractères max, ASCII)
    // Couleur RGB de chaque BLOC de couleur, dans l'ordre (cf. `stitch::ColorBlock` /
    // `stitch_analysis::color_blocks`). Vide = couleurs par défaut déterministes ; trop court =
    // la dernière couleur est répétée.
    std::vector<std::array<std::uint8_t, 3>> block_colors;
    TrimMode trims{TrimMode::Native};
    StopMode stops{StopMode::Native};
    ColorChangeMode color_changes{ColorChangeMode::Emit};
    // JEF uniquement : horodatage « AAAAMMJJhhmmss » de l'en-tête. Fixe par défaut : la sortie
    // reste octet-exacte d'une exécution à l'autre (le désactiver est une décision de l'appelant).
    std::string jef_timestamp{"20000101000000"};
};

// Résultat d'un décodage : la séquence + les couleurs RGB des blocs (un élément par segment
// délimité par un changement de couleur ou un arrêt, dans l'ordre ; vide = le format ne porte
// pas de couleur, ex. EXP).
struct DecodedDesign {
    stitch::StitchSequence sequence;
    std::vector<std::array<std::uint8_t, 3>> block_colors;
    std::string name; // vide si le format n'en porte pas
};

using EncodeExFn = Result<std::vector<std::uint8_t>> (*)(const stitch::StitchSequence&,
                                                         const MachineExportOptions&);
using DecodeExFn = Result<DecodedDesign> (*)(std::span<const std::uint8_t>);

} // namespace openstitch::formats
