// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::formats {

// SVG de DÉCOUPE (HP-SPEC-001) : le contour du tissu d'un appliqué, lisible par un traceur de
// découpe ou un cutter (Brother ScanNCut, Cricut...). Unités : millimètres, axe Y vers le bas
// (convention SVG ; le modèle est Y vers le haut, l'axe est donc inversé), origine ramenée au
// coin supérieur gauche de la boîte englobante. Un trait fin rouge sans remplissage par
// contour ; les courbes sont aplaties (tolérance 0,05 mm). Sortie déterministe.
// `paths` vide : SVG valide sans tracé.
[[nodiscard]] std::string encode_cut_svg(const std::vector<geometry::Path>& paths);

[[nodiscard]] Result<void> write_cut_svg_file(const std::filesystem::path& path,
                                              const std::vector<geometry::Path>& paths);

} // namespace openstitch::formats
