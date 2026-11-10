// SPDX-License-Identifier: Apache-2.0
// Design-system guard rails for OpenStitch Studio (no GUI needed):
//   1. WCAG contrast of every text / background pair of Theme.qml, in both modes;
//   2. QML lint: no unstyled Qt control, no hex colour and no numeric font.pixelSize outside the
//      theme, so that nobody can reintroduce the white-on-dark controls.
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>
#include <QtTest>

namespace {

struct Rgb {
    double r{0}, g{0}, b{0};
};

Rgb parseHex(const QString& hex) {
    bool ok = false;
    const auto value = hex.mid(1, 6).toUInt(&ok, 16);
    return {static_cast<double>((value >> 16) & 0xFF), static_cast<double>((value >> 8) & 0xFF),
            static_cast<double>(value & 0xFF)};
}

double channel(double v) {
    const double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : qPow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QString& hex) {
    const auto c = parseHex(hex);
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

double contrast(const QString& a, const QString& b) {
    auto x = luminance(a);
    auto y = luminance(b);
    if (x < y) {
        std::swap(x, y);
    }
    return (x + 0.05) / (y + 0.05);
}

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

// token -> {dark, light}
QMap<QString, QStringList> readTokens() {
    QMap<QString, QStringList> tokens;
    const auto theme = readFile(QStringLiteral(OPENSTITCH_STUDIO_QML_DIR "/Theme.qml"));
    static const QRegularExpression re(QStringLiteral(
        "property color (\\w+): light \\? \"(#[0-9a-fA-F]{6})\" : \"(#[0-9a-fA-F]{6})\""));
    auto it = re.globalMatch(theme);
    while (it.hasNext()) {
        const auto m = it.next();
        tokens[m.captured(1)] = {m.captured(3), m.captured(2)};
    }
    return tokens;
}

} // namespace

class StudioDesignTest : public QObject {
    Q_OBJECT

private slots:
    void themeTokensAreReadable() {
        const auto tokens = readTokens();
        QVERIFY2(tokens.size() >= 15, "Theme.qml tokens not parsed");
        // Text-like tokens on every surface they are actually drawn on.
        const QStringList texts{"foreground", "muted",   "faint", "accent",         "error",
                                "warning",    "success", "info",  "secondaryAccent"};
        const QStringList grounds{"background", "surface", "panel",
                                  "raised",     "hover",   "accentSoft"};
        QStringList failures;
        for (int mode = 0; mode < 2; ++mode) {
            const char* name = mode == 0 ? "dark" : "light";
            for (const auto& fg : texts) {
                for (const auto& bg : grounds) {
                    QVERIFY2(tokens.contains(fg) && tokens.contains(bg), qPrintable(fg + "/" + bg));
                    const auto ratio = contrast(tokens[fg][mode], tokens[bg][mode]);
                    if (ratio < 4.5) {
                        failures << QStringLiteral("%1 %2 on %3 = %4 (< 4.5)")
                                        .arg(QLatin1String(name), fg, bg)
                                        .arg(ratio, 0, 'f', 2);
                    }
                }
            }
            // Ink on the accent fills (buttons, selection, checked boxes).
            for (const auto& fill : QStringList{"accent", "accentPressed"}) {
                const auto ratio = contrast(tokens["accentInk"][mode], tokens[fill][mode]);
                if (ratio < 4.5) {
                    failures << QStringLiteral("%1 accentInk on %2 = %3 (< 4.5)")
                                    .arg(QLatin1String(name), fill)
                                    .arg(ratio, 0, 'f', 2);
                }
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    }

    void interfaceElementsHaveThreeToOne() {
        const auto tokens = readTokens();
        QStringList failures;
        for (int mode = 0; mode < 2; ++mode) {
            const char* name = mode == 0 ? "dark" : "light";
            // Outlines of inputs / boxes / sliders against the surfaces around them, and the
            // accent (focus ring, checked fill, slider handle) against every surface.
            for (const auto& bg : QStringList{"background", "surface", "panel"}) {
                const auto ratio = contrast(tokens["outline"][mode], tokens[bg][mode]);
                if (ratio < 3.0) {
                    failures << QStringLiteral("%1 outline on %2 = %3 (< 3)")
                                    .arg(QLatin1String(name), bg)
                                    .arg(ratio, 0, 'f', 2);
                }
            }
            for (const auto& bg :
                 QStringList{"background", "surface", "panel", "raised", "hover"}) {
                const auto ratio = contrast(tokens["accent"][mode], tokens[bg][mode]);
                if (ratio < 3.0) {
                    failures << QStringLiteral("%1 accent on %2 = %3 (< 3)")
                                    .arg(QLatin1String(name), bg)
                                    .arg(ratio, 0, 'f', 2);
                }
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    }

    // Lint over qml/**. Exceptions (kept short on purpose):
    //  - Theme.qml defines the tokens (hex colours, pixel sizes);
    //  - Studio*.qml are the themed wrappers and are the only files allowed to instantiate the Qt
    //    controls they re-skin;
    //  - a line carrying `lint-ok:` is exempt from the hex rule (document data such as a default
    //    thread colour typed in a field, which is not an interface colour).
    void qmlUsesOnlyThemedControlsAndTokens() {
        static const QRegularExpression unstyled(
            QStringLiteral("(?<![\\w])(?:Controls\\.)?(CheckBox|TextField|SpinBox|ComboBox|Slider|"
                           "TextArea|Switch|RadioButton|Button|TextEdit|Dial|RangeSlider)\\s*\\{"));
        static const QRegularExpression bareLabel(QStringLiteral("(?<![\\w.])Label\\s*\\{"));
        static const QRegularExpression attachedTip(
            QStringLiteral("Controls\\.ToolTip\\.(visible|text|delay)"));
        static const QRegularExpression hex(QStringLiteral("[\"']#[0-9a-fA-F]{3,8}[\"']"));
        static const QRegularExpression pixel(
            QStringLiteral("font\\.(pixelSize|pointSize)\\s*:\\s*\\d"));
        static const QRegularExpression comment(QStringLiteral("//.*$"));

        QStringList violations;
        int scanned = 0;
        QDirIterator it(QStringLiteral(OPENSTITCH_STUDIO_QML_DIR), {"*.qml"}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto path = it.next();
            const auto name = QFileInfo(path).fileName();
            if (name == QLatin1String("Theme.qml")) {
                continue;
            }
            ++scanned;
            const bool themedWrapper = name.startsWith(QLatin1String("Studio"));
            const auto text = readFile(path);
            const bool defines_label = text.contains(QLatin1String("component Label"));
            const auto lines = text.split('\n');
            for (int i = 0; i < lines.size(); ++i) {
                const auto raw = lines[i];
                auto code = raw;
                code.remove(comment);
                const auto where = QStringLiteral("%1:%2: ").arg(name).arg(i + 1);
                if (!themedWrapper && unstyled.match(code).hasMatch()) {
                    violations << where + QStringLiteral("unstyled Qt control (use Studio*): ") +
                                      raw.trimmed();
                }
                if (!themedWrapper && !defines_label && bareLabel.match(code).hasMatch()) {
                    violations << where + QStringLiteral("bare Label (use StudioLabel): ") +
                                      raw.trimmed();
                }
                if (!themedWrapper && attachedTip.match(code).hasMatch()) {
                    violations << where + QStringLiteral("attached ToolTip (use StudioToolTip): ") +
                                      raw.trimmed();
                }
                if (!raw.contains(QLatin1String("lint-ok:")) && hex.match(code).hasMatch()) {
                    violations << where + QStringLiteral("hex colour outside Theme: ") +
                                      raw.trimmed();
                }
                if (pixel.match(code).hasMatch()) {
                    violations << where + QStringLiteral("numeric font size outside Theme: ") +
                                      raw.trimmed();
                }
            }
        }
        QVERIFY2(scanned > 20, "QML directory not found or empty");
        QVERIFY2(violations.isEmpty(), qPrintable(violations.join('\n')));
    }

    // Guards for the shared, accessible feedback contract. Runtime loading of Main.qml is
    // exercised by test_studio_shell; this test also works without a display server.
    void interactionFeedbackContracts() {
        const auto dir = QStringLiteral(OPENSTITCH_STUDIO_QML_DIR "/");
        const auto theme = readFile(dir + QStringLiteral("Theme.qml"));
        const auto main = readFile(dir + QStringLiteral("Main.qml"));
        const auto host = readFile(dir + QStringLiteral("spaces/SpaceHost.qml"));
        const auto handles = readFile(dir + QStringLiteral("TransformHandles.qml"));
        const auto files = readFile(dir + QStringLiteral("FilePanel.qml"));
        const auto action = readFile(dir + QStringLiteral("ActionPalette.qml"));

        QVERIFY2(!theme.isEmpty() && !main.isEmpty() && !host.isEmpty(),
                 "Required Studio QML sources are missing");
        for (const auto& token : {"animationMs", "feedbackFastMs", "feedbackSlowMs"}) {
            QVERIFY2(theme.contains(QString::fromLatin1(token) +
                                    QStringLiteral(": reducedMotion ? 0 :")),
                     token);
        }
        // A write failure must never display the saved state.
        QVERIFY(main.contains(QStringLiteral("confirmSave(studio.saveProject())")));
        QVERIFY(main.contains(
            QStringLiteral("window.confirmSave(studio.saveProjectAs(selectedFile))")));
        QVERIFY(main.contains(QStringLiteral("saveConfirmed = ok === true")));
        QVERIFY(main.contains(QStringLiteral("function undoWithFeedback()")));
        QVERIFY(main.contains(QStringLiteral("function redoWithFeedback()")));
        QVERIFY(main.contains(QStringLiteral("onStageFeedback: function(message, tone)")));
        QVERIFY(main.contains(QStringLiteral("studioFeedbackToast")));
        QVERIFY(main.contains(QStringLiteral("onDiagnosticsChanged()")));
        QVERIFY(main.contains(QStringLiteral("geometryBuildCount")));
        QVERIFY(main.contains(QStringLiteral("lastPreviewBuildCount")));
        QVERIFY(files.contains(QStringLiteral("readonly property bool notificationVisible")));
        QVERIFY(host.contains(QStringLiteral("activeSpaceUnderline")));
        QVERIFY(host.contains(QStringLiteral("previous[stage.id] !== stage.state")));
        QVERIFY(host.contains(QStringLiteral("stage.state === \"upToDate\"")));
        QVERIFY(handles.contains(QStringLiteral("root.snapping")));
        QVERIFY(handles.contains(QStringLiteral("canvas.ghostRect.width / canvas.zoom")));
        QVERIFY(action.contains(QStringLiteral("actionSearchFeedback")));
        const auto segmentationQml = readFile(dir + QStringLiteral("spaces/SegmentationSpace.qml"));
        QVERIFY(segmentationQml.contains(QStringLiteral("segmentationBusyBadge")));
        QVERIFY(segmentationQml.contains(QStringLiteral("root.seg.busy")));
        QVERIFY(segmentationQml.contains(QStringLiteral("Theme.reducedMotion")));
        const auto header = readFile(dir + QStringLiteral("../segmentation_space_controller.hpp"));
        QVERIFY(
            header.contains(QStringLiteral("Q_PROPERTY(bool busy READ busy NOTIFY stageChanged)")));
        QVERIFY(header.contains(QStringLiteral("images_.busy() || regions_.busy()")));
        QVERIFY(!host.contains(QStringLiteral("étape bloquée")));
    }

    void everyStudioComponentIsRegistered() {
        const auto qmldir = readFile(QStringLiteral(OPENSTITCH_STUDIO_QML_DIR "/qmldir"));
        for (const auto* name :
             {"StudioLabel", "StudioPanel", "StudioToolTip", "StudioTextField", "StudioCheckBox",
              "StudioSwitch", "StudioSlider", "StudioSpinBox", "StudioComboBox"}) {
            QVERIFY2(qmldir.contains(QLatin1String(name) + QLatin1String(" 1.0 ")), name);
            QVERIFY2(QFile::exists(QStringLiteral(OPENSTITCH_STUDIO_QML_DIR "/") +
                                   QLatin1String(name) + QLatin1String(".qml")),
                     name);
        }
    }
};

QTEST_APPLESS_MAIN(StudioDesignTest)
#include "test_studio_design.moc"
