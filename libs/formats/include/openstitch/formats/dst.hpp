// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

// Codec Tajima DST (ADR-008), implémenté en interne d'après la documentation
// publique du format. Limitations du format (docs/formats/dst.md) :
// pas de couleurs réelles (seulement des arrêts), pas d'objets éditables,
// résolution 0,1 mm, déplacement max ±12,1 mm par enregistrement.
struct DstWriteOptions {
    std::string design_name{"OPENSTITCH"};  // champ LA:, tronqué à 16 caractères
    int trim_jumps{3};  // un Trim logique = N sauts de délta nul (convention machine)
};

// Encode la séquence en octets DST. Déterministe : même séquence -> mêmes
// octets. Les positions sont quantifiées au pas de 0,1 mm SANS dérive
// (deltas dérivés des positions absolues quantifiées), origine au premier
// point du motif.
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_dst(const stitch::StitchSequence& sequence,
                                                           const DstWriteOptions& options = {});

// Décode un fichier DST. Tolérant sur l'en-tête (la vérité est le corps) ;
// ne plante jamais sur un fichier corrompu. Les positions décodées sont
// relatives au point de départ du motif (µm). N sauts consécutifs de délta
// nul sont réinterprétés comme un Trim logique.
[[nodiscard]] Result<stitch::StitchSequence> decode_dst(std::span<const std::uint8_t> bytes);

[[nodiscard]] Result<void> write_dst_file(const std::filesystem::path& path,
                                          const stitch::StitchSequence& sequence,
                                          const DstWriteOptions& options = {});
[[nodiscard]] Result<stitch::StitchSequence> read_dst_file(const std::filesystem::path& path);

}  // namespace openstitch::formats
