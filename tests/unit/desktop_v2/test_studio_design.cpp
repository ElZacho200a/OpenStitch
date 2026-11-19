// SPDX-License-Identifier: Apache-2.0
// Design-system guard rails for OpenStitch Studio (no GUI needed):
//   1. (WCAG contrast: test_desktop_v2_theme, for every builtin theme and variant);
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

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

} // namespace

class StudioDesignTest : public QObject {
    Q_OBJECT

private slots:
    // Contrastes WCAG : vérifiés pour CHAQUE thème livré et chaque variante par
    // test_desktop_v2_theme (« every builtin theme resolves cleanly and is readable »), depuis
    // que les couleurs viennent des fichiers de thème (resources/themes) et non plus de Theme.qml.

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

    // Composition contract (docs/desktop-v2/21-studio-neo-theme.md, « Composition ») : one header
    // bar, one filled action, no decorative filler in the chrome.
    void compositionHierarchyContract() {
        const auto dir = QStringLiteral(OPENSTITCH_STUDIO_QML_DIR "/");
        const auto main = readFile(dir + QStringLiteral("Main.qml"));
        const auto host = readFile(dir + QStringLiteral("spaces/SpaceHost.qml"));
        const auto button = readFile(dir + QStringLiteral("StudioButton.qml"));
        const auto vector = readFile(dir + QStringLiteral("spaces/vector/VectorShapePanel.qml"));
        QVERIFY2(!main.isEmpty() && !host.isEmpty() && !button.isEmpty() && !vector.isEmpty(),
                 "Required Studio QML sources are missing");
        // A single header bar holds brand, document and actions.
        QCOMPARE(main.count(QStringLiteral("objectName: \"studioHeader\"")), 1);
        QVERIFY(main.contains(QStringLiteral("objectName: \"documentTitle\"")));
        QVERIFY(main.contains(QStringLiteral("objectName: \"dirtyIndicator\"")));
        // Save is the only filled header action, and only when there is something to save.
        QVERIFY(main.contains(QStringLiteral("emphasized: studio.dirty")));
        // Debug counters and the slogan do not belong to the chrome.
        QVERIFY(!main.contains(QStringLiteral("qsTr(\" commandes\")")));
        QVERIFY(!main.contains(QStringLiteral("qsTr(\"Géométrie · \")")));
        QVERIFY(!host.contains(QStringLiteral("qsTr(\"UN DOCUMENT")));
        // Default buttons are tonal: the outline is reserved for hover and keyboard focus.
        QVERIFY(
            button.contains(QStringLiteral("button.hovered && button.enabled && !button.quiet")));
        // Focus clavier : anneau du thème, jamais sous 2 px (borne du schéma shape.focusWidth).
        QVERIFY(button.contains(QStringLiteral("focused: button.activeFocus")));
        // The vector space exposes its main action as the filled one.
        QVERIFY(vector.contains(QStringLiteral("emphasized: enabled")));
    }

    void activeStudioButtonStateIsAccessible() {
        const auto button = readFile(QStringLiteral(OPENSTITCH_STUDIO_QML_DIR "/StudioButton.qml"));
        QVERIFY2(!button.isEmpty(), "StudioButton.qml is missing");
        const auto state = QStringLiteral("Accessible.checked: button.checked || button.active");
        QVERIFY(button.contains(state));
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
