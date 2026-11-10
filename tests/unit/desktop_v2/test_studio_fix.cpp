// SPDX-License-Identifier: Apache-2.0
// CX09 : « Corriger » et « Réduire les fils » dans Studio. Vrai moteur (document numérisé depuis
// une vraie image), vrai FixController, vrai FixPanel.qml. Ni pixels ni sleep.
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QtTest/QtTest>

#include <filesystem>

#include "digitize_pipeline.hpp"
#include "fix_controller.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "studio_controller.hpp"
#include "vector_space_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

std::vector<std::array<std::uint8_t, 3>> colours(const DocumentSession& session) {
    std::vector<std::array<std::uint8_t, 3>> out;
    for (const auto& object : session.project().embroidery_objects) {
        out.push_back(object.rgb);
    }
    return out;
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    FixController fix{studio};

    void load() {
        DigitizeRequest request;
        request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 80.0;
        request.settings.max_colors = 4;
        auto result = run_digitize(request, TaskContext{});
        QVERIFY(result.has_value());
        session.replaceProject(std::move(result->project), true);
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

} // namespace

class StudioFixTest : public QObject {
    Q_OBJECT
private slots:
    void reductionIsOneUndoableStepAndRestoresColours() {
        Rig rig;
        rig.load();
        const auto original = colours(rig.session);
        const auto stepsBefore = rig.session.historySize();
        rig.fix.previewReduction(2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.reductionReady(), 30000);
        QVERIFY(rig.fix.threadsAfter() <= 2);
        QVERIFY(rig.fix.threadsBefore() > rig.fix.threadsAfter());
        QVERIFY(rig.fix.objectsRecoloured() > 0);
        QCOMPARE(colours(rig.session), original); // l'aperçu ne modifie rien
        QVERIFY(rig.fix.applyReduction());
        QVERIFY(colours(rig.session) != original);
        QCOMPARE(rig.session.historySize(), stepsBefore + 1);
        rig.session.undo();
        QCOMPARE(colours(rig.session), original);
        QVERIFY(!rig.fix.reductionReady()); // consommé
    }

    void staleReductionIsRefusedAndLeavesTheDocumentUntouched() {
        Rig rig;
        rig.load();
        rig.fix.previewReduction(2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.reductionReady(), 30000);
        // Le document change après l'aperçu (n'importe quelle étape annulable).
        rig.session.execute(std::make_unique<commands::SetFinishingCommand>([&] {
            auto f = rig.session.project().finishing;
            f.enabled = !f.enabled;
            return f;
        }()));
        QVERIFY(rig.fix.reductionStale());
        const auto before = colours(rig.session);
        const auto steps = rig.session.historySize();
        QVERIFY(!rig.fix.applyReduction());
        QVERIFY(!rig.fix.lastOk());
        QCOMPARE(colours(rig.session), before);
        QCOMPARE(rig.session.historySize(), steps);
    }

    void staleFixPlanIsRefused() {
        Rig rig;
        rig.load();
        rig.fix.analyze();
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.planReady(), 30000);
        rig.session.execute(std::make_unique<commands::SetFinishingCommand>([&] {
            auto f = rig.session.project().finishing;
            f.split_long_stitches = !f.split_long_stitches;
            return f;
        }()));
        QVERIFY(rig.fix.planStale());
        const auto steps = rig.session.historySize();
        QVERIFY(!rig.fix.applyAllSafe());
        QVERIFY(!rig.fix.applyFix(0));
        QCOMPARE(rig.session.historySize(), steps);
    }

    void panelDrivesAnalysisAndShowsTheReductionPreview() {
        Rig rig;
        rig.load();
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/studio/qml/FixPanel.qml")));
        QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue(&rig.fix)}}));
        QVERIFY(object);
        auto* panel = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(panel);
        panel->setSize({420, 700});
        auto* analyse = findVisual(panel, QStringLiteral("fixAnalyze"));
        auto* preview = findVisual(panel, QStringLiteral("fixPreviewReduction"));
        QVERIFY(analyse && preview);
        QVERIFY(QMetaObject::invokeMethod(analyse, "click"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.planReady(), 30000);
        auto* target = findVisual(panel, QStringLiteral("fixTargetThreads"));
        QVERIFY(target);
        target->setProperty("value", 2);
        QVERIFY(QMetaObject::invokeMethod(preview, "click"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.reductionReady(), 30000);
        auto* summary = findVisual(panel, QStringLiteral("fixReductionSummary"));
        QVERIFY(summary);
        QTRY_VERIFY(summary->isVisible());
        auto* apply = findVisual(panel, QStringLiteral("fixApplyReduction"));
        QVERIFY(apply);
        QTRY_VERIFY(apply->isEnabled());
    }
    // Rejeter un plan de corrections ou un aperçu de réduction : l'état publié repasse à « non
    // prêt » sans toucher au document.
    void discardingAPlanOrAReductionLeavesTheDocumentUntouched() {
        Rig rig;
        rig.load();
        const auto depth = rig.session.undoNames().size();
        rig.fix.analyze();
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.planReady(), 30000);
        QSignalSpy plans(&rig.fix, &FixController::plansChanged);
        rig.fix.discardPlan();
        QVERIFY(!rig.fix.planReady());
        QCOMPARE(plans.count(), 1);
        rig.fix.previewReduction(2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.fix.reductionReady(), 30000);
        rig.fix.discardReduction();
        QVERIFY(!rig.fix.reductionReady());
        QCOMPARE(rig.session.undoNames().size(), depth);
    }
    // Précision de la vectorisation sur une vraie image : une tolérance large donne moins de
    // nœuds qu'une tolérance fine, et les courbes de Bézier produisent des tangentes.
    void vectorizationPrecisionAndCurvesChangeTheContours() {
        Rig rig;
        rig.load();
        VectorSpaceController vec(rig.studio);
        const auto nodes = [&] {
            std::size_t count = 0;
            for (const auto& shape : rig.session.project().vector_objects) {
                for (const auto& set : shape.paths) {
                    count += set.outer.nodes.size();
                    for (const auto& hole : set.holes) {
                        count += hole.nodes.size();
                    }
                }
            }
            return count;
        };
        vec.setPrecisionMm(0.05);
        (void)vec.updateFromSegmentation();
        const auto fine = nodes();
        vec.setPrecisionMm(1.0);
        QVERIFY(vec.updateFromSegmentation());
        const auto coarse = nodes();
        QVERIFY2(coarse < fine, qPrintable(QStringLiteral("%1 >= %2").arg(coarse).arg(fine)));
        vec.setCurves(true);
        vec.setCurveTolerancePx(1.5);
        QVERIFY(vec.updateFromSegmentation());
        bool curved = false;
        for (const auto& shape : rig.session.project().vector_objects) {
            for (const auto& set : shape.paths) {
                for (const auto& node : set.outer.nodes) {
                    curved = curved || node.tan_in.has_value() || node.tan_out.has_value();
                }
            }
        }
        QVERIFY(curved);
    }
};

QTEST_MAIN(StudioFixTest)
#include "test_studio_fix.moc"
