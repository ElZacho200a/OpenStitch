// SPDX-License-Identifier: Apache-2.0
#pragma once

// Privé à la bibliothèque : déclare une factory par nuancier, implémentée
// dans libs/thread_palette/data/<nuancier>.cpp. N'est inclus que par
// catalog.cpp (ordre d'enregistrement) et par chaque fichier de données.
// Évite les problèmes d'ordre d'initialisation statique entre unités de
// traduction : le déterminisme du registre (AD-S1-2) vient de l'ordre
// textuel des appels dans catalog.cpp, jamais d'une map auto-enregistrée.

#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette::detail {

ThreadChart make_madeira_polyneon_chart();
ThreadChart make_isacord_40_chart();

} // namespace openstitch::thread_palette::detail
