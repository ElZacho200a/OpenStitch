// SPDX-License-Identifier: Apache-2.0
// Tokens du design system v2 : contraste WCAG (table unique contrast_rules()), valeurs,
// métrique, cohérence avec le gabarit QSS. PUR : aucune QApplication.
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <vector>

#include "app_style.hpp"
#include "design_tokens.hpp"
#include "style_assets.hpp"

namespace openstitch::desktop {

namespace {

struct Combo {
    ThemeMode mode;
    Density density;
    const char* label;
};

const std::vector<Combo>& combos() {
    static const std::vector<Combo> kCombos = {
        {ThemeMode::Light, Density::Comfortable, "light-comfortable"},
        {ThemeMode::Light, Density::Compact, "light-compact"},
        {ThemeMode::Dark, Density::Comfortable, "dark-comfortable"},
        {ThemeMode::Dark, Density::Compact, "dark-compact"},
    };
    return kCombos;
}

struct ColorField {
    const char* name;
    QColor Tokens::*ptr;
    bool allowAlpha;
};

#define OS_F(n)                                                                                    \
    { #n, &Tokens::n, false }
#define OS_FA(n)                                                                                   \
    { #n, &Tokens::n, true }
const std::vector<ColorField>& colorFields() {
    static const std::vector<ColorField> kFields = {
        OS_F(window),
        OS_F(surface),
        OS_F(surfaceRaised),
        OS_F(surfaceSunken),
        OS_F(border),
        OS_F(borderStrong),
        OS_F(text),
        OS_F(textSecondary),
        OS_F(textDisabled),
        OS_F(accent),
        OS_F(accentHover),
        OS_F(accentPressed),
        OS_F(onAccent),
        OS_F(tonal),
        OS_F(tonalHover),
        OS_F(tonalPressed),
        OS_F(selection),
        OS_F(selectionText),
        OS_F(textSelection),
        OS_F(textSelectionText),
        OS_F(focus),
        OS_F(icon),
        OS_F(success),
        OS_F(warning),
        OS_F(error),
        OS_F(info),
        OS_F(canvasBackground),
        OS_F(canvasPaper),
        OS_FA(canvasGrid),
        OS_FA(canvasAxis),
        OS_F(canvasHoop),
        OS_F(canvasStitch),
        OS_F(canvasJump),
        OS_F(canvasNode),
        OS_F(canvasHandle),
        OS_F(canvasRailA),
        OS_F(canvasRailB),
        OS_F(canvasSnap),
        OS_F(canvasPreview),
        OS_F(canvasCutLine),
        OS_F(canvasMask),
        OS_FA(canvasSelectionHalo),
        OS_F(canvasSelectionLine),
        OS_FA(canvasSelectionRectHalo),
        OS_F(canvasSelectionRectLine),
    };
    return kFields;
}
#undef OS_F
#undef OS_FA

} // namespace

class DesignTokensTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // QSettings isolé : tokens_for lit `ui/reduceMotion`, jamais le profil réel.
        QVERIFY(settingsDir_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    // Valeurs hexadécimales du plan §1.1 : un échange de teintes qui garderait les ratios
    // doit échouer ici.
    void keyTokenHexValuesMatchTheSpec() {
        struct Row {
            const char* name;
            QColor Tokens::*field;
            const char* light;
            const char* dark;
        };
        const Row rows[] = {
            {"window", &Tokens::window, "#ebedf1", "#1b1d21"},
            {"surface", &Tokens::surface, "#f5f6f8", "#23262b"},
            {"surfaceRaised", &Tokens::surfaceRaised, "#ffffff", "#2c3036"},
            {"surfaceSunken", &Tokens::surfaceSunken, "#e2e5ea", "#17181c"},
            {"border", &Tokens::border, "#d3d7dd", "#363b43"},
            {"borderStrong", &Tokens::borderStrong, "#767d86", "#7f8794"},
            {"text", &Tokens::text, "#1b1e23", "#e8eaee"},
            {"textSecondary", &Tokens::textSecondary, "#50575f", "#b0b7c1"},
            {"textDisabled", &Tokens::textDisabled, "#7a808a", "#7c838d"},
            {"accent", &Tokens::accent, "#b04e3c", "#e0765f"},
            {"accentHover", &Tokens::accentHover, "#9a4130", "#e88b77"},
            {"accentPressed", &Tokens::accentPressed, "#843727", "#cf664f"},
            {"onAccent", &Tokens::onAccent, "#ffffff", "#1c100d"},
            {"tonal", &Tokens::tonal, "#e4e7ec", "#363b44"},
            {"tonalHover", &Tokens::tonalHover, "#d9dde4", "#40464f"},
            {"tonalPressed", &Tokens::tonalPressed, "#cdd2da", "#2f343c"},
            {"selection", &Tokens::selection, "#f2dad3", "#4b302a"},
            {"selectionText", &Tokens::selectionText, "#1b1e23", "#f1f3f6"},
            {"textSelection", &Tokens::textSelection, "#b04e3c", "#e0765f"},
            {"textSelectionText", &Tokens::textSelectionText, "#ffffff", "#1c100d"},
            {"focus", &Tokens::focus, "#1f5fc4", "#6aa3f0"},
            {"icon", &Tokens::icon, "#50575f", "#b0b7c1"},
            {"success", &Tokens::success, "#2b6e3d", "#6fbf86"},
            {"warning", &Tokens::warning, "#8a5a00", "#e0a93f"},
            {"error", &Tokens::error, "#b3261e", "#f07a6e"},
            {"info", &Tokens::info, "#25598f", "#7fb0e8"},
            {"canvasBackground", &Tokens::canvasBackground, "#d5d9df", "#15171a"},
            {"canvasPaper", &Tokens::canvasPaper, "#ffffff", "#e4e7ec"},
            {"canvasStitch", &Tokens::canvasStitch, "#1f2233", "#1f2233"},
            {"canvasHoop", &Tokens::canvasHoop, "#c8383a", "#c8383a"},
            {"canvasJump", &Tokens::canvasJump, "#b35c00", "#b35c00"},
            {"canvasNode", &Tokens::canvasNode, "#2463c6", "#2463c6"},
            {"canvasHandle", &Tokens::canvasHandle, "#2463c6", "#2463c6"},
            {"canvasRailA", &Tokens::canvasRailA, "#c2531f", "#c2531f"},
            {"canvasRailB", &Tokens::canvasRailB, "#1b7f8c", "#1b7f8c"},
            {"canvasSnap", &Tokens::canvasSnap, "#b8239a", "#b8239a"},
            {"canvasPreview", &Tokens::canvasPreview, "#5560cc", "#5560cc"},
            {"canvasCutLine", &Tokens::canvasCutLine, "#c0262d", "#c0262d"},
            {"canvasMask", &Tokens::canvasMask, "#c93f00", "#c93f00"},
            {"canvasSelectionLine", &Tokens::canvasSelectionLine, "#b04e3c", "#b04e3c"},
            {"canvasSelectionRectLine", &Tokens::canvasSelectionRectLine, "#2463c6", "#2463c6"},
        };
        const Tokens light = light_tokens();
        const Tokens dark = dark_tokens();
        for (const Row& r : rows) {
            QVERIFY2(light.*r.field == QColor(QLatin1String(r.light)), r.name);
            QVERIFY2(dark.*r.field == QColor(QLatin1String(r.dark)), r.name);
        }
        QCOMPARE(light.canvasGrid, QColor(0x1F, 0x22, 0x33, 40));
        QCOMPARE(light.canvasAxis, QColor(0x5A, 0x5F, 0x8C, 120));
        QCOMPARE(dark.canvasSelectionHalo, QColor(255, 255, 255, 220));
    }

    void wcagReferenceValues() {
        QCOMPARE(QString::number(contrast_ratio(Qt::black, Qt::white), 'f', 1),
                 QStringLiteral("21.0"));
        QCOMPARE(QString::number(contrast_ratio(Qt::white, Qt::white), 'f', 1),
                 QStringLiteral("1.0"));
        // #767676 sur blanc : 4,54 (seuil AA historique).
        QCOMPARE(QString::number(contrast_ratio(QColor(0x76, 0x76, 0x76), Qt::white), 'f', 2),
                 QStringLiteral("4.54"));
        // Symétrie : l'ordre fg/bg ne change pas le ratio d'opaques.
        QCOMPARE(contrast_ratio(QColor(10, 20, 30), QColor(200, 210, 220)),
                 contrast_ratio(QColor(200, 210, 220), QColor(10, 20, 30)));
        // Alpha composé : noir à 50 % sur blanc = gris 127 (ratio ~4,0).
        const QColor c = composite(QColor(0, 0, 0, 128), Qt::white);
        QVERIFY(std::abs(c.red() - 127) <= 1);
        QCOMPARE(c.alpha(), 255);
        QVERIFY(std::abs(contrast_ratio(QColor(0, 0, 0, 128), Qt::white) - 4.0) < 0.05);
        // Luminances de référence.
        QVERIFY(std::abs(relative_luminance(Qt::white) - 1.0) < 1e-9);
        QVERIFY(std::abs(relative_luminance(Qt::black)) < 1e-9);
    }

    void everyContrastRulePassesInAllFourCombinations() {
        for (const Combo& combo : combos()) {
            const Tokens t = tokens_for(combo.mode, combo.density);
            for (const ContrastRule& rule : contrast_rules()) {
                const double ratio = contrast_ratio(t.*rule.fg, t.*rule.bg);
                if (ratio + 1e-9 < rule.minRatio) {
                    QFAIL(qPrintable(
                        QStringLiteral("%1 [%2] : %3 < %4")
                            .arg(QString::fromLatin1(rule.name), QString::fromLatin1(combo.label))
                            .arg(ratio, 0, 'f', 3)
                            .arg(rule.minRatio, 0, 'f', 1)));
                }
            }
        }
    }

    void contrastRulesCoverTheSpecTable() {
        const auto rules = contrast_rules();
        QCOMPARE(static_cast<int>(rules.size()), 86);
        QSet<QString> names;
        for (const ContrastRule& r : rules) {
            QVERIFY2(!names.contains(QString::fromLatin1(r.name)), r.name); // pas de doublon
            names.insert(QString::fromLatin1(r.name));
            QVERIFY(r.fg != nullptr && r.bg != nullptr);
            QVERIFY(r.minRatio >= 1.3);
        }
        const QStringList required = {
            "textSelection/surfaceRaised",  "textSelectionText/textSelection",
            "textSecondary/tonalHover",     "textSecondary/tonalPressed",
            "canvasMask/canvasPaper",       "canvasMask/canvasBackground",
            "onAccent/accentPressed",       "icon/tonal",
            "textDisabled/surfaceSunken",   "borderStrong/surfaceRaised",
            "canvasPaper/canvasBackground", "selectionText/selection",
        };
        for (const QString& n : required) {
            QVERIFY2(names.contains(n), qPrintable(n));
        }
        // Seuils normatifs : texte 4,5 ; non-texte 3 ; papier/fond 1,3.
        for (const ContrastRule& r : rules) {
            const QString n = QString::fromLatin1(r.name);
            if (n.startsWith(QStringLiteral("text/")) || n.startsWith(QStringLiteral("success/")) ||
                n.startsWith(QStringLiteral("onAccent/"))) {
                QCOMPARE(r.minRatio, 4.5);
            }
            if (n.startsWith(QStringLiteral("canvas")) &&
                n != QStringLiteral("canvasPaper/canvasBackground")) {
                QCOMPARE(r.minRatio, 3.0);
            }
        }
    }

    // Quelques ratios publiés dans la table du plan (§1.4) : prouve que les valeurs
    // des tokens ET l'algorithme sont ceux du plan.
    void ratiosMatchThePublishedTable() {
        struct Expect {
            const char* name;
            double light;
            double dark;
        };
        const Expect expected[] = {
            {"text/window", 14.26, 14.01},
            {"text/surfaceRaised", 16.71, 11.01},
            {"textSecondary/tonalHover", 5.37, 4.71},
            {"accent/surface", 4.85, 5.01},
            {"focus/raised", 6.01, 5.12}, // nom ci-dessous résolu sur surfaceRaised
            {"textSelection/surfaceRaised", 5.25, 4.37},
            {"textSelectionText/textSelection", 5.25, 6.13},
            {"canvasMask/canvasBackground", 3.53, 3.59},
            {"canvasPaper/canvasBackground", 1.42, 14.49},
            {"onAccent/accentPressed", 8.21, 5.02},
        };
        const Tokens light = light_tokens();
        const Tokens dark = dark_tokens();
        int checked = 0;
        for (const Expect& e : expected) {
            QString name = QString::fromLatin1(e.name);
            if (name == QStringLiteral("focus/raised")) {
                name = QStringLiteral("focus/surfaceRaised");
            }
            for (const ContrastRule& r : contrast_rules()) {
                if (name == QLatin1String(r.name)) {
                    QVERIFY2(std::abs(contrast_ratio(light.*r.fg, light.*r.bg) - e.light) < 0.011,
                             qPrintable(name + " light"));
                    QVERIFY2(std::abs(contrast_ratio(dark.*r.fg, dark.*r.bg) - e.dark) < 0.011,
                             qPrintable(name + " dark"));
                    ++checked;
                }
            }
        }
        QCOMPARE(checked, 10);
    }

    void contrastReportIsRegeneratedAndMatchesTheRules() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("contrast.md"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(contrast_report_markdown().toUtf8());
        }
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
        int pairs = 0;
        static const QRegularExpression row(QStringLiteral(
            "^\\| (\\w+/\\w+) \\| (\\d+\\.\\d) \\| (\\d+\\.\\d\\d) \\| (\\d+\\.\\d\\d) \\|$"));
        QStringList seen;
        for (const QString& line : lines) {
            const auto m = row.match(line);
            if (!m.hasMatch()) {
                continue;
            }
            ++pairs;
            seen << m.captured(1);
            QVERIFY2(m.captured(3).toDouble() + 0.005 >= m.captured(2).toDouble(),
                     qPrintable(line));
            QVERIFY2(m.captured(4).toDouble() + 0.005 >= m.captured(2).toDouble(),
                     qPrintable(line));
        }
        QCOMPARE(pairs, 86); // = nombre de paires de la table du plan §1.4
        QCOMPARE(pairs, static_cast<int>(contrast_rules().size()));
        // Déterministe : deux générations identiques octet pour octet.
        QCOMPARE(contrast_report_markdown(), contrast_report_markdown());
    }

    void everyColorTokenIsValidAndOpaqueUnlessListed() {
        for (const Combo& combo : combos()) {
            const Tokens t = tokens_for(combo.mode, combo.density);
            for (const ColorField& f : colorFields()) {
                const QColor c = t.*(f.ptr);
                QVERIFY2(c.isValid(), f.name);
                if (!f.allowAlpha) {
                    QVERIFY2(c.alpha() == 255, f.name);
                } else {
                    QVERIFY2(c.alpha() < 255, f.name);
                }
            }
        }
        // Le tissu reste clair dans les deux thèmes (correctif §0.1).
        QVERIFY(light_tokens().canvasPaper.lightness() > 200);
        QVERIFY(dark_tokens().canvasPaper.lightness() > 200);
        // Les deux thèmes diffèrent réellement.
        QVERIFY(light_tokens().window != dark_tokens().window);
        QVERIFY(light_tokens().text != dark_tokens().text);
    }

    void spacingTypographyAndMetricsFollowTheSpec() {
        for (const Combo& combo : combos()) {
            const Tokens t = tokens_for(combo.mode, combo.density);
            QVERIFY(t.space1 < t.space2 && t.space2 < t.space3 && t.space3 < t.space4 &&
                    t.space4 < t.space5 && t.space5 < t.space6 && t.space6 < t.space7);
            // space1..4 gardent leurs valeurs historiques (aucun appelant cassé).
            QCOMPARE(t.space1, 2);
            QCOMPARE(t.space2, 4);
            QCOMPARE(t.space3, 8);
            QCOMPARE(t.space4, 12);
            QCOMPARE(t.space5, 16);
            QCOMPARE(t.space6, 24);
            QCOMPARE(t.space7, 32);
            QVERIFY(t.controlHeight >= 24);
            QCOMPARE(t.radiusSm, 6);
            QCOMPARE(t.radiusMd, 10);
            QCOMPARE(t.radiusLg, 14);
            QCOMPARE(t.radiusPill, 999);
            QCOMPARE(t.scrollbarWidth, 8);
            QCOMPARE(t.focusRingWidth, 2);
            QCOMPARE(t.fontSmall, 8.0);
            QCOMPARE(t.fontTitle, 11.0);
            QCOMPARE(t.fontHeading, 13.0);
            QCOMPARE(t.weightRegular, 400);
            QCOMPARE(t.weightMedium, 500);
            QCOMPARE(t.weightSemibold, 600);
            QVERIFY(t.fontSmall < t.fontBase + 1e-9 && t.fontBase < t.fontTitle &&
                    t.fontTitle < t.fontHeading);
        }
        const Tokens c = light_tokens(Density::Comfortable);
        const Tokens k = light_tokens(Density::Compact);
        QCOMPARE(c.controlHeight, 32);
        QCOMPARE(k.controlHeight, 26);
        QCOMPARE(c.controlPadX, 12);
        QCOMPARE(k.controlPadX, 8);
        QCOMPARE(c.rowHeight, 28);
        QCOMPARE(k.rowHeight, 24);
        QCOMPARE(c.iconSize, 18);
        QCOMPARE(k.iconSize, 16);
        QCOMPARE(c.toolIconSize, 20);
        QCOMPARE(k.toolIconSize, 18);
        QCOMPARE(c.fontBase, 9.0);
        QCOMPARE(k.fontBase, 8.5);
        QVERIFY(!font_families().isEmpty());
        QVERIFY(!mono_font_families().isEmpty());
        QCOMPARE(font_families().first(), QStringLiteral("Segoe UI Variable Text"));
    }

    void motionHonoursTheReduceMotionVariable() {
        qunsetenv("OPENSTITCH_REDUCE_MOTION");
        QCOMPARE(light_tokens().motionShortMs, 120);
        qputenv("OPENSTITCH_REDUCE_MOTION", "1");
        QCOMPARE(light_tokens().motionShortMs, 0);
        qunsetenv("OPENSTITCH_REDUCE_MOTION");
        // Préférence persistée `ui/reduceMotion`.
        QSettings s(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
        s.setValue(QStringLiteral("ui/reduceMotion"), true);
        s.sync();
        QCOMPARE(light_tokens().motionShortMs, 0);
        s.setValue(QStringLiteral("ui/reduceMotion"), false);
        s.sync();
        QCOMPARE(light_tokens().motionShortMs, 120);
    }

    void legacyTokenNamesStillExist() {
        // Compilation = preuve : anciens noms intacts (main_window.cpp & co en dépendent).
        const Tokens t = light_tokens();
        (void)t.window;
        (void)t.surface;
        (void)t.surfaceRaised;
        (void)t.border;
        (void)t.text;
        (void)t.textSecondary;
        (void)t.accent;
        (void)t.accentHover;
        (void)t.selection;
        (void)t.selectionText;
        (void)t.focus;
        (void)t.success;
        (void)t.warning;
        (void)t.error;
        (void)t.info;
        (void)t.canvasBackground;
        (void)t.canvasGrid;
        (void)t.canvasAxis;
        (void)t.canvasHoop;
        (void)t.canvasStitch;
        (void)t.canvasJump;
        (void)t.canvasNode;
        (void)t.canvasHandle;
        (void)t.canvasSelectionHalo;
        (void)t.canvasSelectionLine;
        (void)t.canvasSelectionRectHalo;
        (void)t.canvasSelectionRectLine;
        (void)(t.space1 + t.space2 + t.space3 + t.space4 + t.controlHeight + t.radiusSm +
               t.radiusMd + t.iconSize);
        QVERIFY(true);
    }

    void everyQssPlaceholderExistsInTheTokenMap() {
        for (const Combo& combo : combos()) {
            const auto map = style_token_map(tokens_for(combo.mode, combo.density));
            const QStringList placeholders = stylesheet_placeholders();
            QVERIFY(placeholders.size() > 30);
            for (const QString& name : placeholders) {
                QVERIFY2(map.contains(name), qPrintable(name));
                QVERIFY2(!map.value(name).isEmpty(), qPrintable(name));
            }
        }
        // Chaque glyphe référencé par le gabarit est produit par style_assets.
        const QStringList produced = style_assets::glyph_names();
        const QStringList referenced = stylesheet_asset_names();
        QVERIFY(!referenced.isEmpty());
        for (const QString& name : referenced) {
            QVERIFY2(produced.contains(name), qPrintable(name));
        }
    }

    void allHexValuesParse() {
        static const QRegularExpression hex(QStringLiteral("^#[0-9a-f]{6}$"));
        static const QRegularExpression rgba(QStringLiteral("^rgba\\(\\d+,\\d+,\\d+,\\d+\\)$"));
        for (const Combo& combo : combos()) {
            const Tokens t = tokens_for(combo.mode, combo.density);
            const auto map = style_token_map(t);
            for (const ColorField& f : colorFields()) {
                if (!map.contains(QString::fromLatin1(f.name))) {
                    continue; // tokens canevas non utilisés par la QSS
                }
                const QString v = map.value(QString::fromLatin1(f.name));
                QVERIFY2(hex.match(v).hasMatch() || rgba.match(v).hasMatch(), qPrintable(v));
                if (hex.match(v).hasMatch()) {
                    QVERIFY2(QColor(v).isValid(), qPrintable(v));
                    QCOMPARE(QColor(v), t.*(f.ptr));
                }
            }
        }
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_APPLESS_MAIN(openstitch::desktop::DesignTokensTest)
#include "test_design_tokens.moc"
