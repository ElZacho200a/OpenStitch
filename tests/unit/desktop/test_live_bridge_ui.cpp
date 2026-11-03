// SPDX-License-Identifier: Apache-2.0
//
// Pont « projet vivant » (ADR 0001) côté desktop : un lot injecté sur le document OUVERT (jamais
// rechargé depuis le disque) modifie le projet, passe par la pile d'annulation en une étape, met à
// jour l'état « modifié », et les remplacements de document ferment la session.
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_ops/errors.hpp"

namespace openstitch::desktop {
namespace {

struct Fixture {
    document::Project project;
    ObjectId first;
    ObjectId second;
};

Fixture twoShapes() {
    Fixture f;
    auto& p = f.project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        v.name = i == 0 ? "A" : "B";
        v.paths = {
            {geometry::rectangle_path(Vec2um{Micrometers{i * 20'000}, Micrometers{-10'000}},
                                      Vec2um{Micrometers{i * 20'000 + 10'000}, Micrometers{0}}),
             {}}};
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = i == 0 ? "Emb A" : "Emb B";
        e.source_vector = v.id;
        e.rgb = {static_cast<std::uint8_t>(60 + i * 100), 30, 30};
        e.params = document::TatamiParams{};
        (i == 0 ? f.first : f.second) = e.id;
        p.embroidery_objects.push_back(e);
    }
    return f;
}

std::string recolor(ObjectId id, const char* hex) {
    // Chaînes ordinaires (pas de littéral brut) : le moc de Qt compte mal les accolades d'un R"()".
    return "{\"schema\":\"openstitch.batch/1\",\"operations\":[{\"op\":\"set_color\",\"object\":" +
           std::to_string(id.value) + ",\"rgb\":\"" + hex + "\"}]}";
}

std::array<std::uint8_t, 3> colorOf(const document::Project& p, ObjectId id) {
    for (const auto& e : p.embroidery_objects) {
        if (e.id == id) {
            return e.rgb;
        }
    }
    return {};
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("LiveBridge"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void commitChangesOpenDocumentInOneUndoStepAndMarksItModified();
    void undoAndRedoRestoreTheCommittedState();
    void manualEditBetweenReadAndCommitIsRefused();
    void newDocumentClosesTheSession();
    void commitWithoutApprovalLeavesDocumentUntouched();
};

void MainWindowTest::commitChangesOpenDocumentInOneUndoStepAndMarksItModified() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    w.undoStack_.markClean();
    w.setWindowModified(false);

    auto& bridge = w.liveBridge();
    const auto snap = bridge.snapshot();
    const auto r = w.commitLiveBatch(snap.session_id, snap.revision, recolor(first, "#FF0000"),
                                     "IA : recoloration", true);
    QVERIFY2(r.ok, r.message.c_str());
    QVERIFY(r.applied);
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{255, 0, 0}));
    QVERIFY(w.isWindowModified());
    QCOMPARE(w.undoStack_.undoNames().size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(w.undoStack_.undoName()), QStringLiteral("IA : recoloration"));
    // Le snapshot lu avant le commit reste figé.
    QCOMPARE(colorOf(*snap.project, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
}

void MainWindowTest::undoAndRedoRestoreTheCommittedState() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    w.undoStack_.markClean();
    w.setWindowModified(false);

    auto& bridge = w.liveBridge();
    QVERIFY(w.commitLiveBatch(bridge.session_id(), bridge.revision(), recolor(first, "#00FF00"),
                              "IA", true)
                .ok);
    w.undoAct_->trigger();
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
    QVERIFY(!w.isWindowModified()); // retour à l'état enregistré
    w.redoAct_->trigger();
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{0, 255, 0}));
    QVERIFY(w.isWindowModified());
}

void MainWindowTest::manualEditBetweenReadAndCommitIsRefused() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));

    auto& bridge = w.liveBridge();
    const auto snap = bridge.snapshot();
    // Modification manuelle concurrente passant par la pile du desktop.
    document::VectorObject extra;
    extra.id = w.project_.object_ids.next();
    w.undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(extra), w.project_);

    const auto r =
        w.commitLiveBatch(snap.session_id, snap.revision, recolor(first, "#FF0000"), "IA", true);
    QVERIFY(!r.ok);
    QCOMPARE(QString::fromStdString(r.code),
             QString::fromLatin1(project_ops::code::kStaleRevision));
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
}

void MainWindowTest::newDocumentClosesTheSession() {
    MainWindow w;
    auto f = twoShapes();
    w.applyLoadedProject(std::move(f.project));
    const std::string oldSession = w.liveBridge().session_id();
    const auto oldRevision = w.liveBridge().revision();

    auto g = twoShapes();
    const ObjectId other = g.first;
    w.applyLoadedProject(std::move(g.project));
    QVERIFY(w.liveBridge().session_id() != oldSession);
    QVERIFY(w.liveBridge().revision() > oldRevision);
    const auto r =
        w.commitLiveBatch(oldSession, oldRevision, recolor(other, "#FF0000"), "IA", true);
    QVERIFY(!r.ok);
    QCOMPARE(QString::fromStdString(r.code),
             QString::fromLatin1(project_ops::code::kSessionClosed));
}

void MainWindowTest::commitWithoutApprovalLeavesDocumentUntouched() {
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    auto& bridge = w.liveBridge();
    const auto r = w.commitLiveBatch(bridge.session_id(), bridge.revision(),
                                     recolor(first, "#FF0000"), "IA", false);
    QVERIFY(!r.ok);
    QCOMPARE(QString::fromLatin1(project_ops::code::kApprovalRequired),
             QString::fromStdString(r.code));
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
    QVERIFY(!w.undoAct_->isEnabled());
}

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_live_bridge_ui.moc"
