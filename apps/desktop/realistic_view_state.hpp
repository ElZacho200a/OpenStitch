// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPixmap>
#include <QPointer>

#include <cstdint>

#include "openstitch/stitch_render/segments.hpp"
#include "realistic_preferences.hpp"

class QTimer;

namespace openstitch::desktop {

class RealisticRenderDialog;

// État du rendu réaliste porté par MainWindow : préférences courantes et
// cache du dernier rendu. Le pixmap n'est reconstruit que si les brins, les
// réglages ou la fenêtre visible (zoom/défilement au-delà de la marge)
// changent -- jamais à chaque évènement souris.
struct RealisticViewState {
    RealisticPreferences prefs;
    // Cache.
    QPixmap pixmap;
    stitch_render::RectMm rect{}; // zone couverte par `pixmap`, mm (repère scène)
    double pixmapPpm{0.0};        // résolution réelle de `pixmap`, px/mm
    double requestedPpm{0.0};     // résolution demandée au moment du rendu
    std::uint64_t key{0};         // condensat brins + réglages
    bool valid{false};
    int renderCount{0}; // nombre de rendus réellement calculés (tests)
    // Rafraîchissement différé (zoom, défilement, curseurs de réglage).
    QTimer* timer{nullptr};
    QPointer<RealisticRenderDialog> dialog;
};

} // namespace openstitch::desktop
