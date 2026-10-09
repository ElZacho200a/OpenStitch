// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/stitch_render/params.hpp"

namespace openstitch::desktop {

// Préférences du rendu réaliste des points (Affichage > Rendu réaliste),
// persistées via QSettings (clés `view/realistic/*`) comme les autres
// préférences d'interface. Le calcul du rendu vit dans libs/stitch_render ;
// ceci ne porte que l'état utilisateur.
struct RealisticPreferences {
    // Désactivé par défaut : l'affichage en lignes reste le mode courant.
    bool enabled{false};
    stitch_render::RenderParams params;

    bool operator==(const RealisticPreferences&) const = default;
};

// Lit les préférences (valeurs hors plage ramenées dans les bornes).
[[nodiscard]] RealisticPreferences loadRealisticPreferences();
void saveRealisticPreferences(const RealisticPreferences& prefs);

} // namespace openstitch::desktop
