// SPDX-License-Identifier: Apache-2.0
#include "design_tokens.hpp"

#include <QSettings>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>

namespace openstitch::desktop {

namespace {

// Littéral de couleur 0xRRGGBB (+ alpha). Seule source de littéraux de couleur.
QColor rgb(unsigned hex, int alpha = 255) {
    return QColor(static_cast<int>((hex >> 16) & 0xFF), static_cast<int>((hex >> 8) & 0xFF),
                  static_cast<int>(hex & 0xFF), alpha);
}

// Applique la métrique de densité, commune aux deux thèmes.
void apply_density(Tokens& t, Density density) {
    const bool compact = (density == Density::Compact);
    t.space1 = 2;
    t.space2 = 4;
    t.space3 = 8;
    t.space4 = 12;
    t.space5 = 16;
    t.space6 = 24;
    t.space7 = 32;
    t.radiusSm = 6;
    t.radiusMd = 10;
    t.radiusLg = 14;
    t.radiusPill = 999;
    t.controlHeight = compact ? 26 : 32;
    t.controlPadX = compact ? 8 : 12;
    t.rowHeight = compact ? 24 : 28;
    t.iconSize = compact ? 16 : 18;
    t.toolIconSize = compact ? 18 : 20;
    t.scrollbarWidth = 8;
    t.focusRingWidth = 2;

    t.fontSmall = 8.0;
    t.fontBase = compact ? 8.5 : 9.0;
    t.fontTitle = 11.0;
    t.fontHeading = 13.0;
    t.fontMono = 9.0;
    t.weightRegular = 400;
    t.weightMedium = 500;
    t.weightSemibold = 600;

    // Mouvement réduit : variable d'environnement OU préférence persistée `ui/reduceMotion`.
    const bool reduce = qEnvironmentVariableIsSet("OPENSTITCH_REDUCE_MOTION") ||
                        QSettings(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"))
                            .value(QStringLiteral("ui/reduceMotion"), false)
                            .toBool();
    t.motionShortMs = reduce ? 0 : 120;
}

// Valeurs communes aux deux thèmes (surcouches canevas : bande de luminance
// choisie pour rester lisible sur le papier clair ET sur le fond sombre).
void apply_canvas_common(Tokens& t) {
    t.canvasGrid = rgb(0x1F2233, 40);
    t.canvasAxis = rgb(0x5A5F8C, 120);
    t.canvasStitch = rgb(0x1F2233);
    t.canvasHoop = rgb(0xC8383A);
    t.canvasJump = rgb(0xB35C00);
    t.canvasNode = rgb(0x2463C6);
    t.canvasHandle = rgb(0x2463C6);
    t.canvasRailA = rgb(0xC2531F);
    t.canvasRailB = rgb(0x1B7F8C);
    t.canvasSnap = rgb(0xB8239A);
    t.canvasPreview = rgb(0x5560CC);
    t.canvasCutLine = rgb(0xC0262D);
    t.canvasMask = rgb(0xC93F00);
    t.canvasSelectionHalo = rgb(0xFFFFFF, 220);
    t.canvasSelectionLine = rgb(0xB04E3C);
    t.canvasSelectionRectHalo = rgb(0xFFFFFF, 200);
    t.canvasSelectionRectLine = rgb(0x2463C6);
}

double channel_linear(double c8) {
    const double c = c8 / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

} // namespace

Tokens light_tokens(Density density) {
    Tokens t;
    t.window = rgb(0xEBEDF1);
    t.surface = rgb(0xF5F6F8);
    t.surfaceRaised = rgb(0xFFFFFF);
    t.surfaceSunken = rgb(0xE2E5EA);
    t.border = rgb(0xD3D7DD);
    t.borderStrong = rgb(0x767D86);
    t.text = rgb(0x1B1E23);
    t.textSecondary = rgb(0x50575F);
    t.textDisabled = rgb(0x7A808A);

    t.accent = rgb(0xB04E3C); // rouge-brique « fil », sobre
    t.accentHover = rgb(0x9A4130);
    t.accentPressed = rgb(0x843727);
    t.onAccent = rgb(0xFFFFFF);
    t.tonal = rgb(0xE4E7EC);
    t.tonalHover = rgb(0xD9DDE4);
    t.tonalPressed = rgb(0xCDD2DA);
    t.selection = rgb(0xF2DAD3);
    t.selectionText = rgb(0x1B1E23);
    t.textSelection = rgb(0xB04E3C);
    t.textSelectionText = rgb(0xFFFFFF);
    t.focus = rgb(0x1F5FC4);
    t.icon = rgb(0x50575F);
    t.success = rgb(0x2B6E3D);
    t.warning = rgb(0x8A5A00);
    t.error = rgb(0xB3261E);
    t.info = rgb(0x25598F);

    t.canvasBackground = rgb(0xD5D9DF);
    t.canvasPaper = rgb(0xFFFFFF);
    apply_canvas_common(t);

    apply_density(t, density);
    return t;
}

Tokens dark_tokens(Density density) {
    Tokens t;
    t.window = rgb(0x1B1D21);
    t.surface = rgb(0x23262B);
    t.surfaceRaised = rgb(0x2C3036);
    t.surfaceSunken = rgb(0x17181C);
    t.border = rgb(0x363B43);
    t.borderStrong = rgb(0x7F8794);
    t.text = rgb(0xE8EAEE);
    t.textSecondary = rgb(0xB0B7C1);
    t.textDisabled = rgb(0x7C838D);

    t.accent = rgb(0xE0765F);
    t.accentHover = rgb(0xE88B77);
    t.accentPressed = rgb(0xCF664F);
    t.onAccent = rgb(0x1C100D);
    t.tonal = rgb(0x363B44);
    t.tonalHover = rgb(0x40464F);
    t.tonalPressed = rgb(0x2F343C);
    t.selection = rgb(0x4B302A);
    t.selectionText = rgb(0xF1F3F6);
    t.textSelection = rgb(0xE0765F);
    t.textSelectionText = rgb(0x1C100D);
    t.focus = rgb(0x6AA3F0);
    t.icon = rgb(0xB0B7C1);
    t.success = rgb(0x6FBF86);
    t.warning = rgb(0xE0A93F);
    t.error = rgb(0xF07A6E);
    t.info = rgb(0x7FB0E8);

    t.canvasBackground = rgb(0x15171A);
    t.canvasPaper = rgb(0xE4E7EC); // le tissu reste clair dans les deux thèmes
    apply_canvas_common(t);

    apply_density(t, density);
    return t;
}

Tokens tokens_for(ThemeMode mode, Density density) {
    return mode == ThemeMode::Dark ? dark_tokens(density) : light_tokens(density);
}

QStringList font_families() {
    return {QStringLiteral("Segoe UI Variable Text"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Inter"),
            QStringLiteral("Noto Sans"),
            QStringLiteral("Helvetica Neue"),
            QStringLiteral("Arial")};
}

QStringList mono_font_families() {
    return {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
            QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Courier New")};
}

QColor composite(const QColor& fg, const QColor& bg) {
    const double a = fg.alphaF();
    const auto mix = [a](int f, int b) {
        return static_cast<int>(std::lround(f * a + b * (1.0 - a)));
    };
    return QColor(mix(fg.red(), bg.red()), mix(fg.green(), bg.green()), mix(fg.blue(), bg.blue()));
}

double relative_luminance(const QColor& c) {
    return 0.2126 * channel_linear(c.red()) + 0.7152 * channel_linear(c.green()) +
           0.0722 * channel_linear(c.blue());
}

double contrast_ratio(const QColor& fg, const QColor& bg) {
    const QColor opaqueBg = bg.alpha() == 255 ? bg : composite(bg, QColor(255, 255, 255));
    const QColor flat = composite(fg, opaqueBg);
    const double l1 = relative_luminance(flat);
    const double l2 = relative_luminance(opaqueBg);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

std::span<const ContrastRule> contrast_rules() {
    using T = Tokens;
    // Spec §1.4 : 86 paires. R1 texte 4,5 ; R2 non-texte 3 ; R3 désactivé 3 ; R5 papier 1,3.
#define OS_RULE(fg, bg, min)                                                                       \
    ContrastRule {                                                                                 \
        #fg "/" #bg, &T::fg, &T::bg, min                                                           \
    }
    static const std::array<ContrastRule, 86> kRules = {{
        // text
        OS_RULE(text, window, 4.5),
        OS_RULE(text, surface, 4.5),
        OS_RULE(text, surfaceRaised, 4.5),
        OS_RULE(text, surfaceSunken, 4.5),
        OS_RULE(text, selection, 4.5),
        OS_RULE(text, tonal, 4.5),
        OS_RULE(text, tonalHover, 4.5),
        OS_RULE(text, tonalPressed, 4.5),
        // textSecondary
        OS_RULE(textSecondary, window, 4.5),
        OS_RULE(textSecondary, surface, 4.5),
        OS_RULE(textSecondary, surfaceRaised, 4.5),
        OS_RULE(textSecondary, surfaceSunken, 4.5),
        OS_RULE(textSecondary, tonalHover, 4.5),
        OS_RULE(textSecondary, tonalPressed, 4.5),
        OS_RULE(textSecondary, tonal, 4.5),
        // états
        OS_RULE(success, window, 4.5),
        OS_RULE(success, surface, 4.5),
        OS_RULE(success, surfaceRaised, 4.5),
        OS_RULE(success, surfaceSunken, 4.5),
        OS_RULE(warning, window, 4.5),
        OS_RULE(warning, surface, 4.5),
        OS_RULE(warning, surfaceRaised, 4.5),
        OS_RULE(warning, surfaceSunken, 4.5),
        OS_RULE(error, window, 4.5),
        OS_RULE(error, surface, 4.5),
        OS_RULE(error, surfaceRaised, 4.5),
        OS_RULE(error, surfaceSunken, 4.5),
        OS_RULE(info, window, 4.5),
        OS_RULE(info, surface, 4.5),
        OS_RULE(info, surfaceRaised, 4.5),
        OS_RULE(info, surfaceSunken, 4.5),
        // icon
        OS_RULE(icon, window, 3.0),
        OS_RULE(icon, surface, 3.0),
        OS_RULE(icon, surfaceRaised, 3.0),
        OS_RULE(icon, surfaceSunken, 3.0),
        OS_RULE(icon, tonal, 3.0),
        // textDisabled
        OS_RULE(textDisabled, window, 3.0),
        OS_RULE(textDisabled, surface, 3.0),
        OS_RULE(textDisabled, surfaceRaised, 3.0),
        OS_RULE(textDisabled, surfaceSunken, 3.0),
        // accent
        OS_RULE(accent, window, 3.0),
        OS_RULE(accent, surface, 3.0),
        OS_RULE(accent, surfaceRaised, 3.0),
        OS_RULE(accent, surfaceSunken, 3.0),
        OS_RULE(accent, selection, 3.0),
        // focus
        OS_RULE(focus, window, 3.0),
        OS_RULE(focus, surface, 3.0),
        OS_RULE(focus, surfaceRaised, 3.0),
        OS_RULE(focus, surfaceSunken, 3.0),
        OS_RULE(focus, tonal, 3.0),
        // sélection de texte
        OS_RULE(textSelectionText, textSelection, 4.5),
        OS_RULE(textSelection, surfaceRaised, 3.0),
        OS_RULE(textSelection, surface, 3.0),
        // onAccent
        OS_RULE(onAccent, accent, 4.5),
        OS_RULE(onAccent, accentHover, 4.5),
        OS_RULE(onAccent, accentPressed, 4.5),
        OS_RULE(selectionText, selection, 4.5),
        // borderStrong
        OS_RULE(borderStrong, surfaceRaised, 3.0),
        OS_RULE(borderStrong, surface, 3.0),
        OS_RULE(borderStrong, surfaceSunken, 3.0),
        // canevas
        OS_RULE(canvasStitch, canvasPaper, 3.0),
        OS_RULE(canvasJump, canvasPaper, 3.0),
        OS_RULE(canvasJump, canvasBackground, 3.0),
        OS_RULE(canvasNode, canvasPaper, 3.0),
        OS_RULE(canvasNode, canvasBackground, 3.0),
        OS_RULE(canvasHandle, canvasPaper, 3.0),
        OS_RULE(canvasHandle, canvasBackground, 3.0),
        OS_RULE(canvasRailA, canvasPaper, 3.0),
        OS_RULE(canvasRailA, canvasBackground, 3.0),
        OS_RULE(canvasRailB, canvasPaper, 3.0),
        OS_RULE(canvasRailB, canvasBackground, 3.0),
        OS_RULE(canvasSnap, canvasPaper, 3.0),
        OS_RULE(canvasSnap, canvasBackground, 3.0),
        OS_RULE(canvasPreview, canvasPaper, 3.0),
        OS_RULE(canvasPreview, canvasBackground, 3.0),
        OS_RULE(canvasCutLine, canvasPaper, 3.0),
        OS_RULE(canvasCutLine, canvasBackground, 3.0),
        OS_RULE(canvasHoop, canvasPaper, 3.0),
        OS_RULE(canvasHoop, canvasBackground, 3.0),
        OS_RULE(canvasSelectionLine, canvasPaper, 3.0),
        OS_RULE(canvasSelectionLine, canvasBackground, 3.0),
        OS_RULE(canvasSelectionRectLine, canvasPaper, 3.0),
        OS_RULE(canvasSelectionRectLine, canvasBackground, 3.0),
        OS_RULE(canvasMask, canvasPaper, 3.0),
        OS_RULE(canvasMask, canvasBackground, 3.0),
        OS_RULE(canvasPaper, canvasBackground, 1.3),
    }};
#undef OS_RULE
    return kRules;
}

QString contrast_report_markdown() {
    const Tokens light = light_tokens();
    const Tokens dark = dark_tokens();
    QString md;
    md += QStringLiteral("# Contraste des tokens (WCAG 2.x)\n\n");
    md += QStringLiteral("| paire | min | clair | sombre |\n|---|---|---|---|\n");
    for (const ContrastRule& r : contrast_rules()) {
        const double rl = contrast_ratio(light.*r.fg, light.*r.bg);
        const double rd = contrast_ratio(dark.*r.fg, dark.*r.bg);
        md += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                  .arg(QString::fromLatin1(r.name))
                  .arg(r.minRatio, 0, 'f', 1)
                  .arg(rl, 0, 'f', 2)
                  .arg(rd, 0, 'f', 2);
    }
    return md;
}

} // namespace openstitch::desktop
