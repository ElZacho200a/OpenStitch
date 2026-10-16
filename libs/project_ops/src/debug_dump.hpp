// SPDX-License-Identifier: Apache-2.0
// Interne à project_ops : écrit les images et décisions de diagnostic d'une numérisation
// (`digitize --debug-dir`).
#pragma once

#include <filesystem>

#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/factory.hpp"

namespace openstitch::project_ops::detail {

// Fichiers : `regions.png` (segmentation), `traits.png` (décisions de la couche de traits sur la
// source atténuée : rouge = trait cousu, bleu = barre, vert = séparation non cousue, orange =
// fragment trop court), `couverture.png` (fils réellement posés), `fidelite.png` (carte d'erreurs :
// rouge = tissu visible), `decisions.json` (chiffres et seuils effectifs).
[[nodiscard]] Result<void> write_debug_dir(const std::filesystem::path& dir,
                                           const document::Project& project,
                                           const DigitizeReport& report,
                                           const DigitizeSettings& settings,
                                           const autodigitize::LineLayerMasks& masks);

} // namespace openstitch::project_ops::detail
