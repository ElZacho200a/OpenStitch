// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- fiche de production : tout ce qu'un opérateur doit savoir avant de lancer la
// machine (dimensions du motif et du cadre, comptes de points / sauts / coupes, plan de fils dans
// l'ordre de couture, durée estimée). Calculée sur la séquence EFFECTIVE (jamais stockée). Sans
// Qt ; l'interface la met en page en PDF.

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

struct ThreadBlock {
    std::array<std::uint8_t, 3> rgb{0, 0, 0};
    std::size_t stitches{0};
    double lengthMm{0.0};
    std::string objects; // noms des objets de ce bloc, séparés par « , » (au plus 3 puis « … »)
};

struct ProductionSheet {
    std::string title;
    double designWidthMm{0.0};
    double designHeightMm{0.0};
    double hoopWidthMm{0.0};
    double hoopHeightMm{0.0};
    std::size_t objects{0};
    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    std::size_t colorChanges{0};
    double threadMetres{0.0};
    double minutesAt800spm{0.0};
    std::vector<ThreadBlock> blocks;
    bool fitsHoop{true};
};

[[nodiscard]] Result<ProductionSheet> buildProductionSheet(const document::Project& project,
                                                           const TaskContext& task);

} // namespace openstitch::desktop_v2
