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

// HP-FMT-002 / HP-FMT-003 : codec PES (Brother / Babylock / Bernette), écrit d'après la
// description publique du format (cf. docs/source/formats-pes-jef-exp.md pour les sources et
// licences). Écriture : PES version 1 « complète » (section PES avec blocs CEmbOne/CSewSeg) +
// bloc PEC : en-tête de 512 octets (nom, table des fils = indices de la palette Brother à 64
// entrées, déterminés par le fil le plus proche), flux de points PEC (formes courte 7 bits /
// longue 12 bits, drapeaux saut/coupe, marqueur de changement de couleur, fin 0xFF), vignettes
// 48x38 (une d'ensemble + une par bloc de couleur). 0,1 mm, axe Y vers le BAS dans le fichier
// (converti vers/depuis l'axe Y vers le haut du cœur). Le motif est écrit centré sur l'origine.
// Lecture : PES de toute version (le décalage du bloc PEC est lu dans l'en-tête, la section
// PES elle-même n'est pas interprétée -- les couleurs viennent de la table PEC).
[[nodiscard]] MachineConstraints pes_constraints();

[[nodiscard]] Result<std::vector<std::uint8_t>>
encode_pes(const stitch::StitchSequence& sequence, const MachineExportOptions& options = {});
[[nodiscard]] Result<DecodedDesign> decode_pes(std::span<const std::uint8_t> bytes);

} // namespace openstitch::formats
