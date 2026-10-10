// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/formats/machine.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

// HP-FMT-005 : codec EXP (Melco / Bernina), écrit d'après la description publique du format
// (cf. docs/source/formats-pes-jef-exp.md pour les sources). Flux SANS en-tête ni marqueur de
// fin : paires d'octets signés (dx, dy) en 0,1 mm, axe Y vers le haut (comme le DST) ; code
// 0x80 + octet de commande : 0x04 saut (dx dy), 0x80 coupe (07 00), 0x01 changement de
// couleur/arrêt (00 00). Pas de couleur dans le fichier. Delta max ±127 par enregistrement.
[[nodiscard]] MachineConstraints exp_constraints();

[[nodiscard]] Result<std::vector<std::uint8_t>>
encode_exp(const stitch::StitchSequence& sequence, const MachineExportOptions& options = {});
// Tolérant : ne plante jamais ; un code de commande inconnu après au moins un enregistrement
// termine la lecture (comportement des relecteurs usuels), sans enregistrement -> erreur.
[[nodiscard]] Result<DecodedDesign> decode_exp(std::span<const std::uint8_t> bytes);

} // namespace openstitch::formats
