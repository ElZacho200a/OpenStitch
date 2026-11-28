// SPDX-License-Identifier: Apache-2.0
// SCRIPT-01 : script Python sur la multi-sélection dans Studio. Vrai moteur (document numérisé
// depuis une vraie image), vrai interpréteur embarqué, vrai ScriptController, vrai
// ScriptPanel.qml. Ni pixels ni sleep.
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QtTest/QtTest>

#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

#include "digitize_pipeline.hpp"
#include "script_controller.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ScriptController script{studio};

    void load() {
        DigitizeRequest request;
        request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 80.0;
        request.settings.max_colors = 4;
        auto result = run_digitize(request, TaskContext{});
        QVERIFY(result.has_value());
        session.replaceProject(std::move(result->project), true);
    }
    void selectAll() {
        std::vector<Target> targets;
        for (const auto& object : session.project().embroidery_objects) {
            targets.push_back({TargetKind::EmbroideryObject, object.id.value});
        }
        session.selection().replace(std::move(targets));
    }
    // Écarts de rangées (µm) des objets qui en ont un, dans l'ordre du document.
    [[nodiscard]] std::vector<int> spacings() const {
        std::vector<int> out;
        for (const auto& object : session.project().embroidery_objects) {
            if (const auto* t = std::get_if<document::TatamiParams>(&object.params)) {
                out.push_back(t->row_spacing.value);
            }
        }
        return out;
    }
};

QQuickItem* findVisual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = findVisual(child, name)) {
            return found;
        }
    }
    return nullptr;
}

const QString kSetAll = QStringLiteral("for o in selection:\n"
                                       "    if \"row_spacing_mm\" in o.params:\n"
                                       "        o.set(row_spacing_mm=0.52)\n");

} // namespace

class StudioScriptTest : public QObject {
    Q_OBJECT
private slots:
    void previewChangesNothingThenApplyIsOneUndoableStep() {
        Rig rig;
        rig.load();
        rig.selectAll();
        const auto original = rig.spacings();
        QVERIFY(!original.empty());
        const auto steps = rig.session.historySize();
        const auto revision = rig.session.revision();

        rig.script.setSource(kSetAll);
        rig.script.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.script.busy(), 30000);
        QVERIFY2(rig.script.lastOk(), qPrintable(rig.script.message()));
        QVERIFY(rig.script.previewReady());
        QVERIFY(!rig.script.changes().isEmpty());
        QCOMPARE(rig.session.revision(), revision); // l'aperçu ne modifie rien
        QCOMPARE(rig.spacings(), original);

        QSignalSpy applied(&rig.script, &ScriptController::applied);
        rig.script.apply(); // aperçu à jour : application immédiate
        QCOMPARE(applied.count(), 1);
        QCOMPARE(rig.session.historySize(), steps + 1);
        for (const int spacing : rig.spacings()) {
            QCOMPARE(spacing, 520);
        }
        QVERIFY(!rig.script.previewReady()); // consommé
        QVERIFY(rig.session.undo());
        QCOMPARE(rig.spacings(), original);
    }

    void applyWithoutPreviewRunsTheScriptFirst() {
        Rig rig;
        rig.load();
        rig.selectAll();
        const auto steps = rig.session.historySize();
        QSignalSpy applied(&rig.script, &ScriptController::applied);
        rig.script.setSource(kSetAll);
        rig.script.apply();
        QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, 30000);
        QCOMPARE(rig.session.historySize(), steps + 1);
    }

    void editingTheScriptMakesThePreviewStale() {
        Rig rig;
        rig.load();
        rig.selectAll();
        rig.script.setSource(kSetAll);
        rig.script.preview();
        QTRY_VERIFY_WITH_TIMEOUT(rig.script.previewReady(), 30000);
        QVERIFY(!rig.script.previewStale());
        rig.script.setSource(kSetAll + QStringLiteral("# modifié\n"));
        QVERIFY(rig.script.previewStale());
    }

    void scriptErrorIsShownAndLeavesTheDocumentUntouched() {
        Rig rig;
        rig.load();
        rig.selectAll();
        const auto revision = rig.session.revision();
        rig.script.setSource(
            QStringLiteral("for o in selection:\n    o.set(row_spacing_mm=1/0)\n"));
        rig.script.apply();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.script.busy(), 30000);
        QVERIFY(!rig.script.lastOk());
        QVERIFY(rig.script.message().contains(QStringLiteral("ligne 2")));
        QVERIFY(rig.script.message().contains(QStringLiteral("ZeroDivisionError")));
        QCOMPARE(rig.session.revision(), revision);
    }

    void unknownParameterIsRefusedAtPreview() {
        Rig rig;
        rig.load();
        rig.selectAll();
        const auto revision = rig.session.revision();
        rig.script.setSource(QStringLiteral("for o in selection:\n    o.set(densite_magique=3)\n"));
        rig.script.preview();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.script.busy(), 30000);
        QVERIFY(!rig.script.lastOk());
        QVERIFY(!rig.script.previewReady());
        QCOMPARE(rig.session.revision(), revision);
    }

    void loadingAnExampleKeepsTheEditedScriptRestorable() {
        Rig rig;
        QVERIFY(!rig.script.modified());
        QVERIFY(!rig.script.canRestore());
        const QString mine = QStringLiteral("print('mon script')\n");
        rig.script.setSource(mine);
        QVERIFY(rig.script.modified());
        rig.script.loadExample(2);
        QCOMPARE(rig.script.exampleIndex(), 2);
        QVERIFY(!rig.script.modified());
        QVERIFY(rig.script.canRestore());
        rig.script.restoreSource();
        QCOMPARE(rig.script.source(), mine);
        QVERIFY(!rig.script.canRestore());
        // Passer d'un exemple intact à un autre ne garde rien de côté.
        Rig fresh;
        fresh.script.loadExample(1);
        fresh.script.loadExample(3);
        QVERIFY(!fresh.script.canRestore());
    }

    void emptySelectionIsExplained() {
        Rig rig;
        rig.load();
        rig.session.selection().replace({});
        QCOMPARE(rig.script.selectionCount(), 0);
        rig.script.preview();
        QVERIFY(!rig.script.busy());
        QVERIFY(!rig.script.lastOk());
        QVERIFY(rig.script.message().contains(QStringLiteral("Sélectionnez")));
    }

    void examplesRunOnARealSelection() {
        Rig rig;
        rig.load();
        rig.selectAll();
        const auto examples = rig.script.examples();
        QVERIFY(examples.size() >= 4);
        for (int i = 0; i < examples.size(); ++i) {
            rig.script.loadExample(i);
            rig.script.preview();
            QTRY_VERIFY_WITH_TIMEOUT(!rig.script.busy(), 30000);
            QVERIFY2(rig.script.lastOk(),
                     qPrintable(examples[i].toMap().value(QStringLiteral("title")).toString() +
                                QStringLiteral(" : ") + rig.script.message()));
        }
        // Le dernier exemple liste les objets sans rien changer.
        QVERIFY(rig.script.output().contains(QStringLiteral("mm²")));
    }

    void panelDrivesPreviewAndApply() {
        Rig rig;
        rig.load();
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/studio/qml/ScriptPanel.qml")));
        QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue(&rig.script)}}));
        QVERIFY(object);
        auto* panel = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(panel);
        panel->setSize({340, 480}); // fenêtre basse : pied fixe, milieu qui défile

        // Sans sélection : boutons inactifs et explication visible (pas seulement en infobulle).
        auto* count = findVisual(panel, QStringLiteral("scriptSelectionCount"));
        auto* preview = findVisual(panel, QStringLiteral("scriptPreview"));
        auto* apply = findVisual(panel, QStringLiteral("scriptApply"));
        auto* editor = findVisual(panel, QStringLiteral("scriptEditor"));
        QVERIFY(count && preview && apply && editor);
        QVERIFY(!apply->isEnabled());
        QVERIFY(count->property("text").toString().contains(QStringLiteral("Sélectionnez")));
        QCOMPARE(editor->property("text").toString(), rig.script.source());

        rig.selectAll();
        QTRY_VERIFY(apply->isEnabled());
        editor->setProperty("text", kSetAll); // saisie : le contrôleur suit l'éditeur
        QCOMPARE(rig.script.source(), kSetAll);

        QVERIFY(QMetaObject::invokeMethod(preview, "click"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.script.previewReady(), 30000);
        // Fenêtre basse : Appliquer reste entier dans le panneau, et l'aperçu arrivé est amené
        // dans la zone visible du milieu.
        const auto applyBottom = apply->mapToItem(panel, QPointF(0, apply->height())).y();
        QVERIFY2(applyBottom <= panel->height(), qPrintable(QString::number(applyBottom)));
        auto* scroller = findVisual(panel, QStringLiteral("scriptScroller"));
        auto* results = findVisual(panel, QStringLiteral("scriptResults"));
        QVERIFY(scroller && results);
        QTRY_VERIFY(results->mapToItem(scroller, QPointF(0, 0)).y() >= 0 &&
                    results->mapToItem(scroller, QPointF(0, 0)).y() < scroller->height());
        // Aperçu lisible : nom de l'objet et libellé français du réglage, décimale à virgule.
        QQuickItem* first = nullptr;
        QTRY_VERIFY((first = findVisual(panel, QStringLiteral("scriptChange0"))) != nullptr);
        const auto line = first->property("text").toString();
        QVERIFY2(line.contains(QStringLiteral("Écart entre rangées (mm)")), qPrintable(line));
        QVERIFY2(line.contains(QStringLiteral("0,52")), qPrintable(line));
        QVERIFY2(!line.contains(QStringLiteral("row_spacing_mm")), qPrintable(line));

        const auto steps = rig.session.historySize();
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QCOMPARE(rig.session.historySize(), steps + 1);
        auto* message = findVisual(panel, QStringLiteral("scriptMessage"));
        QVERIFY(message);
        QVERIFY(message->property("text").toString().contains(QStringLiteral("Ctrl+Z")));

        // Un exemple choisi remplace le script de l'éditeur.
        rig.script.loadExample(1);
        QCOMPARE(editor->property("text").toString(), rig.script.source());
    }
};

QTEST_MAIN(StudioScriptTest)
#include "test_studio_script.moc"
