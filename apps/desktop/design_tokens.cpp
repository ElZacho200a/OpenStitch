// SPDX-License-Identifier: Apache-2.0
#include "design_tokens.hpp"

namespace openstitch::desktop {

namespace {

// Applique la métrique de densité, commune aux deux thèmes.
void apply_density(Tokens& t, Density density) {
    t.space1 = 2;
    t.space2 = 4;
    t.space3 = 8;
    t.space4 = 12;
    t.radiusSm = 6;
    t.radiusMd = 8;
    t.radiusLg = 10;
    t.iconSize = 16;
    t.controlHeight = (density == Density::Compact) ? 22 : 28;
}

} // namespace

Tokens light_tokens(Density density) {
    Tokens t;
    t.window = QColor(0xED, 0xE8, 0xE1);
    t.surface = QColor(0xF7, 0xF4, 0xEF);
    t.surfaceRaised = QColor(0xFF, 0xFF, 0xFF);
    t.border = QColor(0xDA, 0xD3, 0xC9);
    t.borderStrong = QColor(0x85, 0x7D, 0x72);
    t.text = QColor(0x2B, 0x27, 0x33);
    t.textSecondary = QColor(0x62, 0x5A, 0x6E);
    t.textDisabled = QColor(0xA3, 0x9B, 0xAD);

    t.accent = QColor(0x4B, 0x3F, 0xD1); // indigo « soie »
    t.accentHover = QColor(0x5E, 0x53, 0xDC);
    t.accentSoft = QColor(0xE4, 0xE1, 0xFA);
    t.selection = QColor(0x4B, 0x3F, 0xD1);
    t.selectionText = QColor(0xFF, 0xFF, 0xFF);
    t.focus = QColor(0x2A, 0x6B, 0xD0);
    // Couleurs d'état utilisées aussi comme TEXTE : contraste >= 4,5:1 sur les trois
    // fonds (fenêtre, surface, surface surélevée), vérifié par test_ui_invariants.
    t.success = QColor(0x1D, 0x6F, 0x48);
    t.warning = QColor(0x8A, 0x5A, 0x06);
    t.error = QColor(0xB5, 0x36, 0x2A);
    t.info = QColor(0x2F, 0x62, 0xA0);

    t.canvasBackground = QColor(0xDD, 0xD7, 0xCE);
    t.canvasGrid = QColor(0, 0, 0, 40);
    t.canvasAxis = QColor(0x78, 0x78, 0xA0, 120);
    t.canvasHoop = QColor(0xC8, 0x3C, 0x3C);
    t.canvasStitch = QColor(0x19, 0x19, 0x2D);
    t.canvasJump = QColor(0xA8, 0x65, 0x12);
    t.canvasNode = QColor(0x2A, 0x6B, 0xD0);
    t.canvasHandle = QColor(0x2A, 0x6B, 0xD0);
    t.canvasSelectionHalo = QColor(0xFF, 0xFF, 0xFF, 220);
    t.canvasSelectionLine = QColor(0x4B, 0x3F, 0xD1);
    t.canvasSelectionRectHalo = QColor(0xFF, 0xFF, 0xFF, 200);
    t.canvasSelectionRectLine = QColor(0x2A, 0x6B, 0xD0);

    apply_density(t, density);
    return t;
}

Tokens dark_tokens(Density density) {
    Tokens t;
    t.window = QColor(0x14, 0x12, 0x2B);
    t.surface = QColor(0x1C, 0x1A, 0x38);
    t.surfaceRaised = QColor(0x26, 0x23, 0x4A);
    t.border = QColor(0x32, 0x2F, 0x5A);
    t.borderStrong = QColor(0x6F, 0x6B, 0xA8);
    t.text = QColor(0xEC, 0xEA, 0xFB);
    t.textSecondary = QColor(0xA5, 0xA1, 0xCC);
    t.textDisabled = QColor(0x6D, 0x6A, 0x9C);

    t.accent = QColor(0xFF, 0x7A, 0x59); // corail « fil »
    t.accentHover = QColor(0xFF, 0x91, 0x74);
    t.accentSoft = QColor(0x3A, 0x2A, 0x4A);
    t.selection = QColor(0xFF, 0x7A, 0x59);
    t.selectionText = QColor(0x1B, 0x0E, 0x0A);
    t.focus = QColor(0x8D, 0xB4, 0xFF);
    t.success = QColor(0x58, 0xD6, 0xA0);
    t.warning = QColor(0xF2, 0xB6, 0x4B);
    t.error = QColor(0xFF, 0x8A, 0x80);
    t.info = QColor(0x8D, 0xB4, 0xFF);

    t.canvasBackground = QColor(0x0E, 0x0D, 0x20);
    // Le tissu reste blanc : une grille blanche y disparaît, quelle que soit
    // son opacité. Ce ton est discernable sur le tissu ET le fond extérieur sombre.
    t.canvasGrid = QColor(0x6F, 0x6B, 0xA8);
    t.canvasAxis = QColor(0x9A, 0x9A, 0xC0, 90);
    t.canvasHoop = QColor(0xD2, 0x5A, 0x5A);
    t.canvasStitch = QColor(0xC8, 0xCA, 0xD8);
    t.canvasJump = QColor(0xD8, 0x92, 0x2E);
    t.canvasNode = QColor(0x4A, 0x8B, 0xE0);
    t.canvasHandle = QColor(0x4A, 0x8B, 0xE0);
    t.canvasSelectionHalo = QColor(0x10, 0x12, 0x16, 220);
    t.canvasSelectionLine = QColor(0xFF, 0x7A, 0x59);
    t.canvasSelectionRectHalo = QColor(0x10, 0x12, 0x16, 200);
    t.canvasSelectionRectLine = QColor(0x4A, 0x8B, 0xE0);

    apply_density(t, density);
    return t;
}

Tokens tokens_for(ThemeMode mode, Density density) {
    return mode == ThemeMode::Dark ? dark_tokens(density) : light_tokens(density);
}

} // namespace openstitch::desktop
