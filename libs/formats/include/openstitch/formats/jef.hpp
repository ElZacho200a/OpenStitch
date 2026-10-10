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

// HP-FMT-004 : codec JEF (Janome), écrit d'après la description publique du format (cf.
// docs/source/formats-pes-jef-exp.md). En-tête de 0x74 octets (décalage des points, nombre de
// couleurs, nombre de paires d'octets, code de cadre, étendues du motif et marges par cadre),
// table de fils (indices de la palette Janome à 78 entrées, 0 = arrêt, puis un mot 0x0D par
// entrée), puis flux de paires d'octets signés (dx, dy) en 0,1 mm, axe Y vers le haut :
// 0x80 0x01 changement de couleur/arrêt, 0x80 0x02 saut, 3 sauts nuls = coupe, 0x80 0x10 fin.
// Le motif est écrit centré sur l'origine de la machine (premier déplacement = positionnement).
[[nodiscard]] MachineConstraints jef_constraints();

[[nodiscard]] Result<std::vector<std::uint8_t>>
encode_jef(const stitch::StitchSequence& sequence, const MachineExportOptions& options = {});
[[nodiscard]] Result<DecodedDesign> decode_jef(std::span<const std::uint8_t> bytes);

} // namespace openstitch::formats
