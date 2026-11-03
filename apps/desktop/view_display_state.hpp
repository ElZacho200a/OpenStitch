// SPDX-License-Identifier: Apache-2.0
#pragma once

// État d'AFFICHAGE du canevas (HP-VIEW-003 passes, HP-VIEW-010 grille et guides, HP-VIEW-012
// aperçu en direct) mémorisé d'une session à l'autre via QSettings (clés ui/memory/view/...).
// Préférences d'interface seulement : jamais écrites dans le document ni dans le fichier .osp.
// Toute lecture est bornée ; une valeur corrompue retombe sur le défaut.

#include <QColor>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

#include "openstitch/stitch_render/view_display.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

struct ViewDisplayState {
    stitch_render::PassDisplay passes;
    stitch_render::GridSettings grid;
    std::vector<stitch_render::Guide> guides;
    bool guidesVisible{true};
    QColor guideColor{0x00, 0xA8, 0xC8};
    // Aperçu en direct pendant la frappe dans les champs numériques de l'inspecteur.
    bool liveTyping{true};
    // Étalonnage du zoom 100 % (px/mm mesurés) ; 0 = automatique.
    double actualPxPerMm{0.0};

    static ViewDisplayState load() {
        using ui_memory::boolValue;
        using ui_memory::intValue;
        ViewDisplayState s;
        s.passes.underlay = boolValue(QStringLiteral("view/pass/underlay"), true);
        s.passes.top = boolValue(QStringLiteral("view/pass/top"), true);
        s.passes.travel = boolValue(QStringLiteral("view/pass/travel"), true);
        s.passes.lock = boolValue(QStringLiteral("view/pass/lock"), true);
        s.passes.manual = boolValue(QStringLiteral("view/pass/manual"), true);
        s.passes.distinct_colors = boolValue(QStringLiteral("view/pass/distinct"), false);
        s.passes.function_markers = boolValue(QStringLiteral("view/pass/functions"), false);
        s.grid.visible = boolValue(QStringLiteral("view/grid/visible"), true);
        s.grid.step_mm = intValue(QStringLiteral("view/grid/stepUm"), 0, 0, 100'000) / 1000.0;
        s.grid.major_every = intValue(QStringLiteral("view/grid/majorEvery"), 5, 1, 50);
        const int rgba = intValue(QStringLiteral("view/grid/alphaPct"), 0, 0, 100);
        if (rgba > 0) {
            s.grid.rgba = {
                static_cast<std::uint8_t>(intValue(QStringLiteral("view/grid/r"), 0, 0, 255)),
                static_cast<std::uint8_t>(intValue(QStringLiteral("view/grid/g"), 0, 0, 255)),
                static_cast<std::uint8_t>(intValue(QStringLiteral("view/grid/b"), 0, 0, 255)),
                static_cast<std::uint8_t>(std::min(255, rgba * 255 / 100))};
        }
        s.grid = stitch_render::sanitized(s.grid);
        s.guidesVisible = boolValue(QStringLiteral("view/guides/visible"), true);
        const QString text =
            QSettings().value(QStringLiteral("ui/memory/view/guides/list")).toString();
        s.guides = stitch_render::parse_guides(text.left(2048).toStdString());
        const QColor c(
            QSettings().value(QStringLiteral("ui/memory/view/guides/color"), QString()).toString());
        if (c.isValid()) {
            s.guideColor = c;
        }
        s.liveTyping = boolValue(QStringLiteral("view/liveTyping"), true);
        s.actualPxPerMm =
            intValue(QStringLiteral("view/actualPxPerMmMilli"), 0, 0, 100'000) / 1000.0;
        return s;
    }

    void save() const {
        using ui_memory::setBoolValue;
        using ui_memory::setIntValue;
        setBoolValue(QStringLiteral("view/pass/underlay"), passes.underlay);
        setBoolValue(QStringLiteral("view/pass/top"), passes.top);
        setBoolValue(QStringLiteral("view/pass/travel"), passes.travel);
        setBoolValue(QStringLiteral("view/pass/lock"), passes.lock);
        setBoolValue(QStringLiteral("view/pass/manual"), passes.manual);
        setBoolValue(QStringLiteral("view/pass/distinct"), passes.distinct_colors);
        setBoolValue(QStringLiteral("view/pass/functions"), passes.function_markers);
        setBoolValue(QStringLiteral("view/grid/visible"), grid.visible);
        setIntValue(QStringLiteral("view/grid/stepUm"),
                    static_cast<int>(std::lround(grid.step_mm * 1000.0)));
        setIntValue(QStringLiteral("view/grid/majorEvery"), grid.major_every);
        setIntValue(QStringLiteral("view/grid/alphaPct"), grid.rgba[3] * 100 / 255);
        setIntValue(QStringLiteral("view/grid/r"), grid.rgba[0]);
        setIntValue(QStringLiteral("view/grid/g"), grid.rgba[1]);
        setIntValue(QStringLiteral("view/grid/b"), grid.rgba[2]);
        setBoolValue(QStringLiteral("view/guides/visible"), guidesVisible);
        QSettings().setValue(QStringLiteral("ui/memory/view/guides/list"),
                             QString::fromStdString(stitch_render::serialize_guides(guides)));
        QSettings().setValue(QStringLiteral("ui/memory/view/guides/color"), guideColor.name());
        setBoolValue(QStringLiteral("view/liveTyping"), liveTyping);
        setIntValue(QStringLiteral("view/actualPxPerMmMilli"),
                    static_cast<int>(std::lround(actualPxPerMm * 1000.0)));
    }
};

} // namespace openstitch::desktop
