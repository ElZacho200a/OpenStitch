// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- rail de bobines : le plan de fils vu comme une rangée de bobines dans l'ordre de
// couture (une bobine par objet de broderie). Sans Qt. Un « changement de fil » est le passage
// entre deux bobines consécutives de couleurs différentes ; le réordonnancement passe par
// `moveInOrder` (une commande annulable, refusée si l'ordre des calques est cassé).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

struct Spool {
    std::uint64_t id{0};
    std::string name;
    std::array<std::uint8_t, 3> rgb{};
    bool threadChangeBefore{false}; // couleur différente de la bobine précédente
};

struct SpoolRail {
    std::vector<Spool> spools;
    int threadChanges{0};
    // « 3 bobines · 2 changements de fil » (français = langue source).
    [[nodiscard]] std::string summary() const;
};

[[nodiscard]] SpoolRail spoolRail(const document::Project& project);

// Place la bobine de l'objet `id` à l'index `toIndex` (0 = cousue en premier).
ServiceResult moveSpool(DocumentSession& session, std::uint64_t id, std::size_t toIndex);

} // namespace openstitch::desktop_v2
