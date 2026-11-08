// SPDX-License-Identifier: Apache-2.0
// Famille A (fichiers, imports, exports) de Studio : parcours complets sur de vraies données, avec
// la vraie interface QML (QT_QPA_PLATFORM=offscreen), sans simulation du moteur.
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>
#include <fstream>
#include <memory>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
constexpr std::uint64_t firstId = (1ULL << 54) + 17;
constexpr std::uint64_t secondId = (1ULL << 54) + 19;

document::Project studioFixture() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    for (const auto id : {firstId, secondId}) {
        const std::int32_t y = id == firstId ? 10'000 : -10'000;
        document::VectorObject vector;
        vector.id = ObjectId{id - 1};
        geometry::Path path;
        path.closed = false;
        path.nodes = {{{Micrometers{-15'000}, Micrometers{y}}},
                      {{Micrometers{15'000}, Micrometers{y}}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{id};
        object.source_vector = vector.id;
        object.name = id == firstId ? "Raspberry thread" : "Mint thread";
        object.rgb = id == firstId ? std::array<std::uint8_t, 3>{180, 17, 65}
                                   : std::array<std::uint8_t, 3>{21, 180, 151};
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(secondId);
    return project;
}

QString osp(const QTemporaryDir& dir, const char* name, const document::Project& project) {
    const QString path = dir.filePath(QString::fromLatin1(name));
    const auto saved =
        project_io::save_project(std::filesystem::path(path.toStdWString()), project);
    return saved ? path : QString();
}

QUrl url(const QTemporaryDir& dir, const char* name) {
    return QUrl::fromLocalFile(dir.filePath(QString::fromLatin1(name)));
}

QByteArray bytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Session + contrôleurs sur la vraie scène QML.
struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    std::unique_ptr<FileController> files;
    QQmlApplicationEngine engine;
    QQuickWindow* window{nullptr};
    QStringList warnings;

    explicit Rig(const QString& stateDir = {}, bool withQml = false) {
        files = std::make_unique<FileController>(studio, &actions, stateDir);
        if (!withQml) {
            return;
        }
        QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                         [this](const QList<QQmlError>& errors) {
                             for (const auto& e : errors) {
                                 warnings.push_back(e.toString());
                             }
                         });
        engine.rootContext()->setContextProperty(QStringLiteral("studio"), &studio);
        engine.rootContext()->setContextProperty(QStringLiteral("actions"), &actions);
        engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
        engine.rootContext()->setContextProperty(QStringLiteral("files"), files.get());
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        window = engine.rootObjects().isEmpty()
                     ? nullptr
                     : qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    }

    void load(const QString& path) {
        files->openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && !files->busy() && studio.scene() != nullptr,
                                 15'000);
    }
    void makeDirty() {
        studio.selectObject(QString::number(firstId));
        QVERIFY(studio.moveSelection(3, 1));
        QVERIFY(studio.dirty());
    }
    bool waitIdle() {
        return QTest::qWaitFor([this] { return !files->busy(); }, 30'000);
    }
};
} // namespace

class StudioFilesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void fileMenuEntriesCloseAfterRealMouseClick();
    void comboPopupOpensAboveAtWindowBottom();
    void unsavedGuardNewOpenAndQuit();
    void guardSaveWithAndWithoutPath();
    void guardDialogIsShownByTheRealQml();
    void recentFilesPersistPruneAndReopen();
    void dropAndCommandLineDispatchByExtension();
    void machineExportImportRoundTrip();
    void exportsAreDeterministicAndRepeatable();
    void documentExportsWriteRealFiles();
    void exportOptionsAreOneUndoStepAndRefusedWhenInvalid();
    void imageAndVectorImports();
    void autosaveRecoveryAndDiscard();
    void hoopMachineAndUserHoops();
};

void StudioFilesTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
}

void StudioFilesTest::fileMenuEntriesCloseAfterRealMouseClick() {
    Rig rig({}, true);
    QVERIFY(rig.window);
    QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
    auto* button = rig.window->findChild<QQuickItem*>(QStringLiteral("fileMenuButton"));
    auto* menu = rig.window->findChild<QObject*>(QStringLiteral("fileMenu"));
    auto* entry = rig.window->findChild<QQuickItem*>(QStringLiteral("newFileMenuEntry"));
    QVERIFY(button);
    QVERIFY(menu);
    QVERIFY(entry);
    const auto center = [](QQuickItem* item) {
        return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
    };
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(button));
    QTRY_VERIFY_WITH_TIMEOUT(menu->property("visible").toBool(), 3'000);
    // A physical click must both execute the action and release the modal popup;
    // otherwise the invisible/leftover overlay swallows clicks on the rest of Studio.
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(entry));
    QTRY_VERIFY_WITH_TIMEOUT(!menu->property("visible").toBool(), 3'000);
    QVERIFY(rig.files->pendingReason().isEmpty());
}

void StudioFilesTest::comboPopupOpensAboveAtWindowBottom() {
    QQmlApplicationEngine engine;
    const QByteArray qml = R"qml(
        import QtQuick
        import QtQuick.Window
        import "."
        Window {
            width: 400
            height: 300
            visible: true
            StudioComboBox {
                objectName: "edgeCombo"
                x: 230
                y: 250
                width: 155
                model: ["Premier", "Deuxième", "Troisième", "Quatrième", "Cinquième"]
                currentIndex: 4
            }
        }
    )qml";
    engine.loadData(qml, QUrl(QStringLiteral("qrc:/studio/qml/studio_combo_edge_test.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "The StudioComboBox fixture must load");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5'000);
    auto* combo = window->findChild<QQuickItem*>(QStringLiteral("edgeCombo"));
    QVERIFY(combo);
    auto* popup = combo->property("popup").value<QObject*>();
    QVERIFY(popup);

    const QPoint pos = combo->mapToScene(QPointF(combo->width() / 2.0, combo->height() / 2.0)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos);
    QTRY_VERIFY_WITH_TIMEOUT(popup->property("visible").toBool(), 3'000);
    const double x = popup->property("x").toDouble();
    const double y = popup->property("y").toDouble();
    const double w = popup->property("width").toDouble();
    const double h = popup->property("height").toDouble();
    QVERIFY2(y >= 7 && y + h <= window->height() - 7, "Drop-down extends beyond vertical window bounds");
    QVERIFY2(x >= 7 && x + w <= window->width() - 7, "Drop-down extends beyond horizontal window bounds");
    QVERIFY(w >= combo->width());
    QVERIFY(h >= 32.0);

    auto* list = popup->property("contentItem").value<QQuickItem*>();
    QVERIFY(list);
    // The item is displayed above the field, and it must remain physically clickable.
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      list->mapToScene(QPointF(24.0, 14.0)).toPoint());
    QTRY_COMPARE_WITH_TIMEOUT(combo->property("currentIndex").toInt(), 0, 3'000);
    QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 3'000);
}

void StudioFilesTest::unsavedGuardNewOpenAndQuit() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    const auto b = osp(dir, "b.osp", studioFixture());
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    Rig rig;
    rig.load(a);
    QVERIFY(!rig.studio.dirty());
    // Document propre : « Nouveau » s'exécute tout de suite, sans question.
    QSignalSpy guard(rig.files.get(), &FileController::guardChanged);
    rig.makeDirty();
    rig.files->requestNew();
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("nouveau"));
    QVERIFY(rig.studio.dirty()); // rien n'a été remplacé tant qu'on n'a pas répondu
    QVERIFY(!rig.session.project().embroidery_objects.empty());
    // Annuler : on garde le document modifié.
    rig.files->resolveGuard(QStringLiteral("cancel"));
    QVERIFY(rig.files->pendingReason().isEmpty());
    QVERIFY(rig.studio.dirty());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
    // Ignorer : les modifications sont perdues, le nouveau projet est installé.
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("discard"));
    QVERIFY(rig.session.project().embroidery_objects.empty());
    QVERIFY(!rig.studio.dirty());
    // Ouvrir un autre fichier est protégé de la même façon.
    rig.load(a);
    rig.makeDirty();
    rig.files->openPath(b);
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("ouvrir"));
    rig.files->resolveGuard(QStringLiteral("cancel"));
    QCOMPARE(rig.studio.path(), a);
    // Quitter : propre -> immédiat ; modifié -> boîte, puis quitApproved.
    QSignalSpy quit(rig.files.get(), &FileController::quitApproved);
    QVERIFY(!rig.files->requestQuit());
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("quitter"));
    QVERIFY(quit.isEmpty());
    rig.files->resolveGuard(QStringLiteral("cancel"));
    QVERIFY(quit.isEmpty());
    QVERIFY(!rig.files->requestQuit());
    rig.files->resolveGuard(QStringLiteral("discard"));
    QCOMPARE(quit.count(), 1);
    rig.studio.undo();
    QVERIFY(!rig.studio.dirty());
    QVERIFY(rig.files->requestQuit());
    QVERIFY(guard.count() >= 4);
}

void StudioFilesTest::guardSaveWithAndWithoutPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    Rig rig;
    rig.load(a);
    rig.makeDirty();
    const auto before = bytes(a);
    // Avec un chemin : « Enregistrer » écrit puis poursuit.
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("save"));
    QVERIFY(rig.files->pendingReason().isEmpty());
    QVERIFY(bytes(a) != before);                               // le projet modifié a été écrit
    QVERIFY(rig.session.project().embroidery_objects.empty()); // puis « Nouveau » exécuté
    // Sans chemin : l'enregistrement sous est demandé, la suite attend le fichier.
    rig.session.replaceProject(studioFixture(), true);
    rig.makeDirty();
    QSignalSpy saveAs(rig.files.get(), &FileController::saveAsRequested);
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("save"));
    QCOMPARE(saveAs.count(), 1);
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("nouveau"));
    rig.files->guardSaveAs(url(dir, "nouveau.osp"));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("nouveau.osp"))));
    QVERIFY(rig.files->pendingReason().isEmpty());
    QVERIFY(rig.session.project().embroidery_objects.empty());
    // Une sauvegarde qui échoue n'efface rien : l'action est abandonnée.
    rig.session.replaceProject(studioFixture(), true);
    rig.makeDirty();
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("save"));
    rig.files->guardSaveAs(url(dir, "a.osp/dossier/x.osp"));
    QVERIFY(rig.files->messageIsError());
    QVERIFY(rig.studio.dirty());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
}

void StudioFilesTest::guardDialogIsShownByTheRealQml() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    Rig rig({}, true);
    QVERIFY2(rig.window != nullptr, qPrintable(rig.warnings.join('\n')));
    rig.load(a);
    auto* guard = rig.window->findChild<QObject*>(QStringLiteral("guardDialog"));
    QVERIFY(guard);
    QVERIFY(!guard->property("visible").toBool());
    rig.makeDirty();
    rig.files->requestNew();
    QTRY_VERIFY(guard->property("visible").toBool());
    // Le bouton « Annuler » de la boîte répond vraiment au contrôleur.
    auto* cancel = rig.window->findChild<QObject*>(QStringLiteral("guardCancel"));
    QVERIFY(cancel);
    QMetaObject::invokeMethod(cancel, "clicked");
    QTRY_VERIFY(!guard->property("visible").toBool());
    QVERIFY(rig.studio.dirty());
    // « Ignorer » remplace le document.
    rig.files->requestNew();
    QTRY_VERIFY(guard->property("visible").toBool());
    QMetaObject::invokeMethod(rig.window->findChild<QObject*>(QStringLiteral("guardDiscard")),
                              "clicked");
    QTRY_VERIFY(rig.session.project().embroidery_objects.empty());
    QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("fileDropArea")));
    QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("fileMenuButton")));
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioFilesTest::recentFilesPersistPruneAndReopen() {
    QTemporaryDir dir;
    QTemporaryDir state;
    QVERIFY(dir.isValid() && state.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    const auto b = osp(dir, "b.osp", studioFixture());
    {
        Rig rig(state.path());
        QVERIFY(rig.files->recent().isEmpty());
        rig.load(a);
        rig.load(b);
        const auto recent = rig.files->recent();
        QCOMPARE(recent.size(), 2);
        QCOMPARE(recent.front().toMap().value(QStringLiteral("path")).toString(), b);
        QCOMPARE(recent.front().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("b.osp"));
        // Rouvrir un récent le remonte sans doublon.
        rig.files->requestOpenRecent(1);
        QTRY_VERIFY_WITH_TIMEOUT(rig.studio.path() == a && !rig.studio.busy(), 15'000);
        QCOMPARE(rig.files->recent().size(), 2);
        QCOMPARE(rig.files->recent().front().toMap().value(QStringLiteral("path")).toString(), a);
    }
    {
        // Persistance entre deux lancements.
        Rig rig(state.path());
        QCOMPARE(rig.files->recent().size(), 2);
        QVERIFY(QFile::remove(b));
        Rig again(state.path()); // fichier disparu : retiré à la lecture
        QCOMPARE(again.files->recent().size(), 1);
        again.files->requestOpenRecent(0);
        QTRY_VERIFY_WITH_TIMEOUT(again.studio.path() == a && !again.studio.busy(), 15'000);
        again.files->clearRecent();
        QVERIFY(again.files->recent().isEmpty());
    }
    Rig reread(state.path());
    QVERIFY(reread.files->recent().isEmpty());
    // Un fichier introuvable est signalé sans toucher au document.
    reread.files->openPath(dir.filePath(QStringLiteral("fantome.osp")));
    QVERIFY(reread.files->messageIsError());
}

void StudioFilesTest::dropAndCommandLineDispatchByExtension() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    Rig rig;
    // Glisser-déposer d'un projet.
    rig.files->openDropped({QUrl::fromLocalFile(a)});
    QTRY_VERIFY_WITH_TIMEOUT(rig.studio.path() == a && rig.studio.scene() != nullptr, 15'000);
    // Une image demande ses options avant toute numérisation.
    QImage image(40, 40, QImage::Format_RGB32);
    image.fill(Qt::white);
    const auto png = dir.filePath(QStringLiteral("dessin.png"));
    QVERIFY(image.save(png));
    QSignalSpy asked(rig.files.get(), &FileController::imageImportRequested);
    rig.files->openDropped({QUrl::fromLocalFile(png)});
    QCOMPARE(asked.count(), 1);
    QCOMPARE(asked.front().front().toString(), png);
    QCOMPARE(rig.studio.path(), a);
    // Format inconnu et URL distante : refus lisible.
    QFile unknown(dir.filePath(QStringLiteral("x.xyz")));
    QVERIFY(unknown.open(QIODevice::WriteOnly));
    unknown.close();
    rig.files->openPath(unknown.fileName());
    QVERIFY(rig.files->messageIsError());
    rig.files->openDropped({QUrl(QStringLiteral("https://exemple.test/a.osp"))});
    QVERIFY(rig.files->messageIsError());
}

void StudioFilesTest::machineExportImportRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    QVERIFY(!rig.files->machineFormats().isEmpty());
    for (const auto* name : {"sortie.dst", "sortie.pes", "sortie.jef", "sortie.exp"}) {
        rig.files->exportMachine(url(dir, name));
        QVERIFY2(rig.waitIdle(), name);
        QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
        QVERIFY2(QFileInfo(dir.filePath(QString::fromLatin1(name))).size() > 0, name);
    }
    QVERIFY(rig.files->hasLastExport());
    // Réimport du DST : broderie machine, projet non enregistré, document propre.
    Rig second;
    second.files->openPath(dir.filePath(QStringLiteral("sortie.dst")));
    QTRY_VERIFY_WITH_TIMEOUT(!second.studio.busy() && !second.files->busy() &&
                                 second.session.project().imported_design.has_value(),
                             15'000);
    QCOMPARE(second.studio.title(), QStringLiteral("sortie.dst"));
    QVERIFY(!second.studio.hasPath());
    QVERIFY(!second.studio.dirty());
    // Une erreur d'écriture est rapportée, jamais silencieuse.
    rig.files->exportMachine(url(dir, "sortie.dst/x.dst"));
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.files->messageIsError());
    // Un format inconnu est refusé avant tout calcul.
    rig.files->exportMachine(url(dir, "x.abc"));
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("x.abc"))));
}

void StudioFilesTest::exportsAreDeterministicAndRepeatable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    rig.files->exportAgain();
    QVERIFY(rig.files->messageIsError()); // aucun export précédent
    rig.files->exportMachine(url(dir, "un.dst"));
    QVERIFY(rig.waitIdle());
    const auto first = bytes(dir.filePath(QStringLiteral("un.dst")));
    QVERIFY(!first.isEmpty());
    QFile::remove(dir.filePath(QStringLiteral("un.dst")));
    rig.files->exportAgain(); // « avec les derniers réglages »
    QVERIFY(rig.waitIdle());
    QCOMPARE(rig.files->lastExportName(), QStringLiteral("un.dst"));
    QCOMPARE(bytes(dir.filePath(QStringLiteral("un.dst"))), first); // octet pour octet
    // L'export ne modifie jamais le document.
    QVERIFY(!rig.studio.dirty());
    QVERIFY(!rig.studio.canUndo());
    // Sans extension, le format demandé est ajouté.
    rig.files->exportMachine(url(dir, "sans"), {{QStringLiteral("format"), QStringLiteral("pes")}});
    QVERIFY(rig.waitIdle());
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("sans.pes"))));
}

void StudioFilesTest::documentExportsWriteRealFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    rig.files->exportThreadList(url(dir, "fils.csv"));
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    const auto csv = bytes(dir.filePath(QStringLiteral("fils.csv")));
    QVERIFY(csv.contains("ordre;"));
    QCOMPARE(csv.count('\n'), 3); // en-tête + deux fils
    rig.files->exportDxf(url(dir, "contours"));
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("contours.dxf"))).contains("SECTION"));
    rig.files->exportSvg(url(dir, "points.svg"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("points.svg"))).contains("<svg"));
    rig.files->exportImage(url(dir, "rendu.png"), 4.0);
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QImage png(dir.filePath(QStringLiteral("rendu.png")));
    QVERIFY(!png.isNull() && png.width() > 10);
    rig.files->exportImage(url(dir, "rendu.jpg"), 4.0, 80);
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("rendu.jpg"))).startsWith("\xFF\xD8"));
    rig.files->exportImage(url(dir, "mauvais.png"), 99.0); // résolution hors bornes
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("mauvais.png"))));
    rig.files->exportProductionSheet(url(dir, "fiche.pdf"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("fiche.pdf"))).startsWith("%PDF"));
    rig.files->exportPlacementTemplate(url(dir, "gabarit.pdf"),
                                       {{QStringLiteral("paper"), QStringLiteral("A4")}});
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    const auto first = bytes(dir.filePath(QStringLiteral("gabarit.pdf")));
    QVERIFY(first.startsWith("%PDF"));
    rig.files->exportPlacementTemplate(url(dir, "gabarit2.pdf"));
    QVERIFY(rig.waitIdle());
    QCOMPARE(bytes(dir.filePath(QStringLiteral("gabarit2.pdf"))), first); // déterministe
    rig.files->exportPlacementTemplate(url(dir, "mauvais.pdf"),
                                       {{QStringLiteral("paper"), QStringLiteral("B7")}});
    QVERIFY(rig.files->messageIsError());
    // Projet vide : les exports refusent avec un message, sans fichier.
    rig.files->requestNew();
    rig.files->exportThreadList(url(dir, "vide.csv"));
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("vide.csv"))));
    QVERIFY(!rig.studio.dirty());
}

void StudioFilesTest::exportOptionsAreOneUndoStepAndRefusedWhenInvalid() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    const auto before = rig.files->exportOptions();
    QVERIFY(rig.files
                ->setExportOptions({{QStringLiteral("trimThresholdMm"), 6.0},
                                    {QStringLiteral("lockKind"), QStringLiteral("triangle")}})
                .isEmpty());
    QVERIFY(rig.studio.canUndo());
    QCOMPARE(rig.files->exportOptions().value(QStringLiteral("trimThresholdMm")).toDouble(), 6.0);
    QCOMPARE(rig.files->exportOptions().value(QStringLiteral("lockKind")).toString(),
             QStringLiteral("triangle"));
    rig.studio.undo();
    QCOMPARE(rig.files->exportOptions(), before);
    QVERIFY(!rig.files->setExportOptions({{QStringLiteral("trimThresholdMm"), -4.0}}).isEmpty());
    QVERIFY(!rig.files->setExportOptions({{QStringLiteral("lockKind"), QStringLiteral("zz")}})
                 .isEmpty());
    QVERIFY(!rig.studio.canUndo());
    // Les réglages passés à l'export sont appliqués au document avant l'écriture (annulable).
    rig.files->exportMachine(url(dir, "reglages.dst"), {{QStringLiteral("trimThresholdMm"), 5.5}});
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.studio.canUndo());
    QCOMPARE(rig.files->exportOptions().value(QStringLiteral("trimThresholdMm")).toDouble(), 5.5);
    QVERIFY(!rig.files->exportCheck(QStringLiteral("dst")).isEmpty());
}

void StudioFilesTest::imageAndVectorImports() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(120, 120, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 30, 40));
        painter.drawEllipse(20, 20, 80, 80);
        painter.setBrush(QColor(20, 40, 200));
        painter.drawRect(45, 45, 30, 30);
    }
    const auto png = dir.filePath(QStringLiteral("logo.png"));
    QVERIFY(image.save(png));
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    rig.makeDirty();
    // Hors bornes : refus avant toute numérisation.
    rig.files->importImage(png, {{QStringLiteral("heightMm"), 2.0}});
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!rig.files->busy());
    // L'import remplace le document : protégé par la question « Enregistrer ? ».
    auto options = rig.files->imageImportDefaults();
    options[QStringLiteral("heightMm")] = 40.0;
    options[QStringLiteral("maxColors")] = 3;
    rig.files->importImage(png, options);
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("importer"));
    rig.files->resolveGuard(QStringLiteral("discard"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(!rig.session.project().embroidery_objects.empty());
    QCOMPARE(rig.studio.title(), QStringLiteral("logo.png"));
    QVERIFY(!rig.studio.hasPath());
    QVERIFY(rig.studio.dirty()); // à enregistrer
    // SVG et DXF s'ajoutent au projet courant, en une étape annulable.
    const auto svgPath = dir.filePath(QStringLiteral("forme.svg"));
    QFile svg(svgPath);
    QVERIFY(svg.open(QIODevice::WriteOnly));
    svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40mm\" height=\"30mm\" "
              "viewBox=\"0 0 40 30\"><rect x=\"5\" y=\"5\" width=\"30\" height=\"20\" "
              "fill=\"#ff0000\"/></svg>");
    svg.close();
    const auto before = rig.session.project().vector_objects.size();
    rig.files->importVector(svgPath);
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(rig.session.project().vector_objects.size() > before);
    rig.studio.undo();
    QCOMPARE(rig.session.project().vector_objects.size(), before);
    rig.files->importVector(dir.filePath(QStringLiteral("logo.png")));
    QVERIFY(rig.files->messageIsError()); // une image ne s'importe pas comme tracé
}

void StudioFilesTest::autosaveRecoveryAndDiscard() {
    QTemporaryDir state;
    QTemporaryDir dir;
    QVERIFY(state.isValid() && dir.isValid());
    {
        Rig rig(state.path());
        QVERIFY(!rig.files->hasRecovery());
        QVERIFY(!rig.files->autosaveNow()); // document propre : rien à sauvegarder
        rig.session.replaceProject(studioFixture(), true);
        rig.makeDirty();
        QVERIFY(rig.files->autosaveNow());
        // Arrêt anormal : le contrôleur disparaît sans requestQuit().
    }
    {
        Rig rig(state.path());
        QVERIFY(rig.files->hasRecovery());
        QVERIFY(!rig.files->autosaveNow()); // ne pas écraser avant que l'utilisateur ait tranché
        QSignalSpy changed(rig.files.get(), &FileController::recoveryChanged);
        rig.files->recoverAutosave();
        QVERIFY(!rig.files->hasRecovery());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
        QVERIFY(rig.studio.dirty()); // récupéré : reste à enregistrer
        QVERIFY(!rig.studio.hasPath());
        // Enregistrer efface la sauvegarde : plus rien à proposer au lancement suivant.
        QVERIFY(rig.studio.saveProjectAs(url(dir, "recupere.osp")));
    }
    {
        Rig rig(state.path());
        QVERIFY(!rig.files->hasRecovery());
        rig.session.replaceProject(studioFixture(), true);
        rig.makeDirty();
        QVERIFY(rig.files->autosaveNow());
    }
    {
        Rig rig(state.path());
        QVERIFY(rig.files->hasRecovery());
        rig.files->discardRecovery(); // « Ignorer »
        QVERIFY(!rig.files->hasRecovery());
    }
    Rig last(state.path());
    QVERIFY(!last.files->hasRecovery());
    // Quitter proprement ne laisse pas de sauvegarde.
    last.session.replaceProject(studioFixture(), true);
    last.makeDirty();
    QVERIFY(last.files->autosaveNow());
    QVERIFY(!last.files->requestQuit());
    last.files->resolveGuard(QStringLiteral("discard"));
    Rig after(state.path());
    QVERIFY(!after.files->hasRecovery());
}

void StudioFilesTest::hoopMachineAndUserHoops() {
    QTemporaryDir state;
    QVERIFY(state.isValid());
    {
        Rig rig(state.path());
        rig.session.replaceProject(studioFixture(), true);
        QVERIFY(!rig.files->hoops().isEmpty());
        QVERIFY(!rig.files->machines().isEmpty());
        const auto hoop = rig.files->hoops().front().toMap();
        QVERIFY(rig.files->applyHoop(hoop.value(QStringLiteral("id")).toString(), 3.0).isEmpty());
        QCOMPARE(rig.files->currentHoop().value(QStringLiteral("id")).toString(),
                 hoop.value(QStringLiteral("id")).toString());
        QVERIFY(rig.studio.canUndo());
        QVERIFY(!rig.files->applyHoop(QStringLiteral("inconnu"), 0.0).isEmpty());
        QVERIFY(!rig.files->applyCustomHoop(5.0, 5.0, 0.0).isEmpty());
        QVERIFY(rig.files->applyCustomHoop(120.0, 80.0, 2.0).isEmpty());
        QVERIFY(rig.files->currentHoop().value(QStringLiteral("id")).toString().isEmpty());
        const auto machine = rig.files->machines().front().toMap();
        QVERIFY(rig.files->applyMachine(machine.value(QStringLiteral("id")).toString()).isEmpty());
        QVERIFY(!rig.files->applyMachine(QStringLiteral("inconnue")).isEmpty());
        QVERIFY(rig.files->saveCurrentHoopAs(QStringLiteral("Mon cadre")).isEmpty());
        QCOMPARE(rig.files->userHoops().size(), 1);
        QVERIFY(!rig.files->saveCurrentHoopAs(QStringLiteral("a;b")).isEmpty());
    }
    Rig rig(state.path()); // « Mes cadres » survit au redémarrage
    QCOMPARE(rig.files->userHoops().size(), 1);
    rig.session.replaceProject(studioFixture(), true);
    QVERIFY(rig.files->applyUserHoop(QStringLiteral("Mon cadre")).isEmpty());
    QVERIFY(rig.files->removeUserHoop(QStringLiteral("Mon cadre")).isEmpty());
    QVERIFY(rig.files->userHoops().isEmpty());
}

QTEST_MAIN(StudioFilesTest)
#include "test_studio_files.moc"
