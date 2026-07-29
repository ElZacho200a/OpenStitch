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
    t.radiusSm = 3;
    t.radiusMd = 5;
    t.iconSize = 16;
    t.controlHeight = (density == Density::Compact) ? 22 : 28;
}

}  // namespace

Tokens light_tokens(Density density) {
    Tokens t;
    t.window = QColor(0xF2, 0xF3, 0xF4);
    t.surface = QColor(0xFA, 0xFB, 0xFC);
    t.surfaceRaised = QColor(0xFF, 0xFF, 0xFF);
    t.border = QColor(0xD4, 0xD7, 0xDB);
    t.text = QColor(0x1E, 0x21, 0x26);
    t.textSecondary = QColor(0x69, 0x6F, 0x78);

    t.accent = QColor(0xB0, 0x4E, 0x3C);       // rouge-brique « fil », sobre
    t.accentHover = QColor(0xC2, 0x5C, 0x48);
    t.selection = QColor(0xB0, 0x4E, 0x3C);
    t.selectionText = QColor(0xFF, 0xFF, 0xFF);
    t.focus = QColor(0x2A, 0x6B, 0xD0);
    t.success = QColor(0x3F, 0x7D, 0x4F);
    t.warning = QColor(0xB4, 0x79, 0x1F);
    t.error = QColor(0xB2, 0x3A, 0x2E);
    t.info = QColor(0x3A, 0x6E, 0xA5);

    t.canvasBackground = QColor(0xEB, 0xEB, 0xEE);
    t.canvasGrid = QColor(0, 0, 0, 40);
    t.canvasAxis = QColor(0x78, 0x78, 0xA0, 120);
    t.canvasHoop = QColor(0xC8, 0x3C, 0x3C);
    t.canvasStitch = QColor(0x19, 0x19, 0x2D);
    t.canvasJump = QColor(0xC8, 0x78, 0x1E);
    t.canvasNode = QColor(0x2A, 0x6B, 0xD0);
    t.canvasHandle = QColor(0x2A, 0x6B, 0xD0);
    t.canvasSelectionHalo = QColor(0xFF, 0xFF, 0xFF, 220);
    t.canvasSelectionLine = QColor(0xB0, 0x4E, 0x3C);

    apply_density(t, density);
    return t;
}

Tokens dark_tokens(Density density) {
    Tokens t;
    t.window = QColor(0x24, 0x26, 0x2B);
    t.surface = QColor(0x2B, 0x2E, 0x34);
    t.surfaceRaised = QColor(0x33, 0x37, 0x3E);
    t.border = QColor(0x3E, 0x43, 0x4B);
    t.text = QColor(0xE6, 0xE8, 0xEC);
    t.textSecondary = QColor(0x9A, 0xA0, 0xA8);

    t.accent = QColor(0xD0, 0x64, 0x50);
    t.accentHover = QColor(0xDE, 0x73, 0x60);
    t.selection = QColor(0xD0, 0x64, 0x50);
    t.selectionText = QColor(0x1A, 0x14, 0x12);
    t.focus = QColor(0x4A, 0x8B, 0xE0);
    t.success = QColor(0x5C, 0xA0, 0x6E);
    t.warning = QColor(0xD2, 0x9A, 0x3E);
    t.error = QColor(0xD2, 0x5A, 0x4E);
    t.info = QColor(0x5A, 0x8E, 0xC5);

    t.canvasBackground = QColor(0x1E, 0x20, 0x24);
    t.canvasGrid = QColor(255, 255, 255, 28);
    t.canvasAxis = QColor(0x9A, 0x9A, 0xC0, 90);
    t.canvasHoop = QColor(0xD2, 0x5A, 0x5A);
    t.canvasStitch = QColor(0xC8, 0xCA, 0xD8);
    t.canvasJump = QColor(0xD8, 0x92, 0x2E);
    t.canvasNode = QColor(0x4A, 0x8B, 0xE0);
    t.canvasHandle = QColor(0x4A, 0x8B, 0xE0);
    t.canvasSelectionHalo = QColor(0x10, 0x12, 0x16, 220);
    t.canvasSelectionLine = QColor(0xD0, 0x64, 0x50);

    apply_density(t, density);
    return t;
}

Tokens tokens_for(ThemeMode mode, Density density) {
    return mode == ThemeMode::Dark ? dark_tokens(density) : light_tokens(density);
}

}  // namespace openstitch::desktop
