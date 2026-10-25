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

// HP-FMT-006 : codec VP3 (Husqvarna Viking / Pfaff), écrit d'après la structure publique du
// format (cf. docs/source/formats-pes-jef-exp.md, section VP3) sans reprise de code tiers.
// Marqueur « %vsm% », chaînes précédées d'une longueur 16 bits grand-boutiste, entiers
// grand-boutistes, unité native 0,01 mm (10 um), un SEGMENT par couleur : départ, RVB du fil,
// nom, puis flux d'octets signés (dx, dy) ; 0x80 0x01 = point long (deux entiers 16 bits),
// 0x80 0x02 = saut long (convention de cette implémentation, VP3 n'ayant pas de saut distinct),
// 0x80 0x03 = coupe. Axe Y vers le haut. La compatibilité avec un visualiseur tiers ou une
// machine reste à valider par un humain (roadmap HP-FMT-006).
[[nodiscard]] MachineConstraints vp3_constraints();

[[nodiscard]] Result<std::vector<std::uint8_t>>
encode_vp3(const stitch::StitchSequence& sequence, const MachineExportOptions& options = {});
[[nodiscard]] Result<DecodedDesign> decode_vp3(std::span<const std::uint8_t> bytes);

} // namespace openstitch::formats
