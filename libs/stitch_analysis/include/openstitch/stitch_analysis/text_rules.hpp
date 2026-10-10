// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"

namespace openstitch::stitch_analysis {

// Règles propres aux objets texte (HP-TXT-009) : « texte trop petit »
// (hauteur de capitale < ~5 mm en satin, < 3 mm quel que soit le point) et lettres
// dont le trait (< 1 mm) ne peut pas être cousu en satin. Ces règles lisent
// l'INTENTION (`Project::text_objects`), pas la séquence : elles préviennent même
// quand les points n'ont pas encore été générés. Une trouvaille par texte, dont
// `object` est la première lettre cousue (sélectionnable) et `location` l'origine.
[[nodiscard]] std::vector<Finding> analyze_text_objects(const document::Project& project);

} // namespace openstitch::stitch_analysis
