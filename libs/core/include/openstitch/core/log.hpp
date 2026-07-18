// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spdlog/spdlog.h>

namespace openstitch {

// Initialise le logger global (stderr couleur). À appeler une fois au
// démarrage de chaque exécutable.
void init_logging(bool verbose = false);

}  // namespace openstitch
