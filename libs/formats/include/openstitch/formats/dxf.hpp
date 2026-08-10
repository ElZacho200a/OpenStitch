// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::formats {

// Import/export DXF (interopérabilité esquisses avec les logiciels de CAO
// tiers -- ex. Fusion 360, dont l'utilisateur se sert habituellement pour
// dessiner ses esquisses avant de les reprendre ici). Sous-ensemble
// volontairement restreint, documenté ici plutôt que dans docs/source/ (pas
// de chapitre dédié pour l'instant) :
//
// Import (decode_dxf) : entités LINE, LWPOLYLINE (avec bulge -> arc
// échantillonné en segments de droite), CIRCLE, ARC. Toute autre entité
// (SPLINE, POLYLINE/VERTEX historique, texte, hachures, blocs...) est
// silencieusement ignorée -- jamais d'échec sur un fichier par ailleurs
// valide, même principe de tolérance que le décodeur DST. Unités du fichier
// interprétées comme des millimètres (aucune section HEADER $INSUNITS lue).
//
// Export (encode_dxf) : un LWPOLYLINE par tracé. Les courbes (nœuds Lisses,
// tangentes symétriques) n'ont pas d'équivalent direct en DXF classique
// (pas de SPLINE émise) : elles sont aplaties en polyligne dense via
// geometry::flatten -- interopérabilité fidèle visuellement, pas un
// round-trip exact des nœuds/tangentes.
struct DxfWriteOptions {
    // Tolérance d'aplatissement des courbes en polyligne (cf. geometry::flatten).
    Micrometers flatten_tolerance{Micrometers{50}};  // 0,05 mm
};

[[nodiscard]] Result<std::vector<geometry::Path>> decode_dxf(std::span<const std::uint8_t> bytes);

[[nodiscard]] Result<std::vector<std::uint8_t>> encode_dxf(const std::vector<geometry::Path>& paths,
                                                            const DxfWriteOptions& options = {});

[[nodiscard]] Result<void> write_dxf_file(const std::filesystem::path& path,
                                          const std::vector<geometry::Path>& paths,
                                          const DxfWriteOptions& options = {});
[[nodiscard]] Result<std::vector<geometry::Path>> read_dxf_file(const std::filesystem::path& path);

}  // namespace openstitch::formats
