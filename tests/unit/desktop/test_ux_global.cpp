// SPDX-License-Identifier: Apache-2.0
// Tests de l'ergonomie transversale (audit « UX globale / Analyse ») : contrastes des jetons,
// raccourcis, ouverture par chemin / dépôt de fichier, disposition de l'interface, panneau
// Analyse et simulation incrémentale. Headless, sans comparaison de pixels ni sleep.
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDropEvent>
#include <QFile>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <cmath>

#include "canvas_view.hpp"
#include "design_tokens.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;
namespace st = openstitch::stitch;

doc::Project imageOnlyProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    return project;
}

ObjectId addSquareVector(doc::Project& project) {
    doc::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Square";
    geo::Path square;
    square.closed = true;
    constexpr std::int32_t s = 10'000;
    for (const auto& [x, y] :
         std::vector<std::pair<std::int32_t, std::int32_t>>{{0, 0}, {s, 0}, {s, s}, {0, s}}) {
        square.nodes.push_back(geo::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                             geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vec.paths.push_back(geo::PathSet{square, {}});
    const ObjectId id = vec.id;
    project.vector_objects.push_back(std::move(vec));
    return id;
}

ObjectId addEmbroidery(doc::Project& project, ObjectId source) {
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "Carre";
    emb.source_vector = source;
    emb.params = doc::RunningStitchParams{};
    const ObjectId id = emb.id;
    project.embroidery_objects.push_back(std::move(emb));
    return id;
}

double luminance(const QColor& c) {
    const auto lin = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Somme des segments dessinés par les tracés cousus (hors marqueur d'aiguille).
int drawnPathElements(const QGraphicsScene& scene) {
    int total = 0;
    for (const QGraphicsItem* item : scene.items()) {
        if (const auto* path = qgraphicsitem_cast<const QGraphicsPathItem*>(item);
            path != nullptr && item->zValue() >= 20.0 && item->zValue() < 25.0) {
            total += path->path().elementCount();
        }
    }
    return total;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("UxGlobalTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void init() { QSettings().clear(); }

    // ---- contrastes ---------------------------------------------------------------

    void textTokensMeetContrastInBothThemes() {
        for (const Tokens& t : {light_tokens(), dark_tokens()}) {
            for (const QColor& bg : {t.window, t.surface, t.surfaceRaised}) {
                for (const QColor& fg :
                     {t.text, t.textSecondary, t.success, t.warning, t.error, t.info}) {
                    QVERIFY2(contrast(fg, bg) >= 4.5, qPrintable(QStringLiteral("%1 sur %2 : %3")
                                                                     .arg(fg.name(), bg.name())
                                                                     .arg(contrast(fg, bg))));
                }
            }
            // Le texte désactivé est distinct du texte d'aide.
            QVERIFY(t.textDisabled != t.textSecondary);
        }
    }

    // ---- raccourcis ---------------------------------------------------------------

    void redoHasTwoShortcutsAndNewShortcutsExist() {
        MainWindow window;
        const QList<QKeySequence> redo = window.redoAct_->shortcuts();
        QVERIFY(redo.contains(QKeySequence(Qt::CTRL | Qt::Key_Y)));
        QVERIFY(redo.contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)));
        // F6 segmente et F8 numérise (workflow de segmentation) : les statistiques sont en F9.
        QCOMPARE(window.statsAct_->shortcut(), QKeySequence(Qt::Key_F9));
        QCOMPARE(window.autoDigitizeAct_->shortcut(), QKeySequence(Qt::Key_F8));
    }

    void enterAndBackspaceShortcutsOnlyWhileDrawing() {
        MainWindow window;
        QVERIFY(!window.drawReturnShortcut_->isEnabled());
        QVERIFY(!window.drawEnterShortcut_->isEnabled());
        QVERIFY(!window.drawBackspaceShortcut_->isEnabled());
        window.setTool(Tool::DrawPolygon);
        window.pendingPolygonVertices_.push_back(Vec2um{Micrometers{0}, Micrometers{0}});
        window.updateDrawActionsState();
        QVERIFY(window.drawReturnShortcut_->isEnabled());
        QVERIFY(window.drawBackspaceShortcut_->isEnabled());
        window.cancelPolygonDraw();
        QVERIFY(!window.drawReturnShortcut_->isEnabled());
    }

    void plainLettersAreNotStolenFromListsAndCombos() {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QComboBox combo(&window);
        combo.addItems({QStringLiteral("a"), QStringLiteral("s")});
        combo.show();
        combo.setFocus();
        QVERIFY(QTest::qWaitFor([&] { return QApplication::focusWidget() == &combo; }));
        QKeyEvent letter(QEvent::ShortcutOverride, Qt::Key_S, Qt::NoModifier, QStringLiteral("s"));
        letter.ignore();
        QApplication::sendEvent(&combo, &letter);
        QVERIFY(letter.isAccepted()); // la liste déroulante garde la lettre (recherche par frappe)
        // Avec Ctrl, le raccourci reste disponible.
        QKeyEvent ctrl(QEvent::ShortcutOverride, Qt::Key_S, Qt::ControlModifier);
        ctrl.ignore();
        QApplication::sendEvent(&combo, &ctrl);
        QVERIFY(!ctrl.isAccepted());
        // Le canevas, lui, ne retient rien : « S » reste l'outil colonne satin.
        window.view_->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return QApplication::focusWidget() == window.view_; }));
        QKeyEvent onCanvas(QEvent::ShortcutOverride, Qt::Key_S, Qt::NoModifier,
                           QStringLiteral("s"));
        onCanvas.ignore();
        QApplication::sendEvent(window.view_, &onCanvas);
        QVERIFY(!onCanvas.isAccepted());
    }

    // ---- ouverture par chemin / dépôt --------------------------------------------

    void firstOpenableFileSkipsUnsupportedAndRemoteUrls() {
        const QList<QUrl> urls{QUrl(QStringLiteral("https://exemple.org/a.osp")),
                               QUrl::fromLocalFile(QStringLiteral("C:/tmp/notes.txt")),
                               QUrl::fromLocalFile(QStringLiteral("C:/tmp/Logo.SVG"))};
        QCOMPARE(MainWindow::firstOpenableLocalFile(urls), QStringLiteral("C:/tmp/Logo.SVG"));
        QVERIFY(MainWindow::firstOpenableLocalFile({}).isEmpty());
    }

    void openPathRoutesSvgByExtensionAndRejectsUnknownFormats() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString svgPath = dir.filePath(QStringLiteral("carre.svg"));
        QFile svg(svgPath);
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' "
                  "viewBox='0 0 20 20'><rect x='2' y='2' width='10' height='10'/></svg>");
        svg.close();
        const QString txtPath = dir.filePath(QStringLiteral("notes.txt"));
        QFile txt(txtPath);
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write("x");
        txt.close();

        MainWindow window;
        window.openPath(txtPath);
        QVERIFY(
            window.statusBar()->currentMessage().contains(QStringLiteral("non pris en charge")));
        QVERIFY(window.project_.vector_objects.empty());
        window.openPath(dir.filePath(QStringLiteral("absent.svg")));
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("introuvable")));

        window.openPath(svgPath);
        QVERIFY(!window.project_.vector_objects.empty());
    }

    void dropEventOpensTheDroppedFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString svgPath = dir.filePath(QStringLiteral("depot.svg"));
        QFile svg(svgPath);
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' "
                  "viewBox='0 0 20 20'><rect x='2' y='2' width='10' height='10'/></svg>");
        svg.close();

        MainWindow window;
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(svgPath)});
        QVERIFY(window.acceptDrops());
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        QTRY_VERIFY(!window.project_.vector_objects.empty());
    }

    // ---- disposition --------------------------------------------------------------

    void savingLayoutWhileHidingPanelsKeepsPanelsVisibleAndRestorable() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.documentDock_->isVisible());
        window.hidePanelsAct_->setChecked(true);
        QVERIFY(!window.documentDock_->isVisible());

        window.saveUiLayout();
        QVERIFY(!window.hidePanelsAct_->isChecked());
        QVERIFY(window.documentDock_->isVisible());

        // L'état enregistré est versionné et se restaure sans repli.
        const QByteArray state = QSettings().value(QStringLiteral("ui/windowState")).toByteArray();
        QVERIFY(!state.isEmpty());
        QVERIFY(window.restoreState(state, 1));
        QVERIFY(!window.restoreState(state, 2)); // version différente : refusée
    }

    void corruptedSavedLayoutFallsBackToDefault() {
        QSettings().setValue(QStringLiteral("ui/windowState"), QByteArray("pas un etat"));
        QSettings().setValue(QStringLiteral("ui/geometry"), QByteArray("pas une geometrie"));
        MainWindow window; // ne plante pas et garde une disposition utilisable
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.centralWidget()->isVisible());
        QVERIFY(!window.saveState().isEmpty());
    }

    // ---- indicateur d'enregistrement ---------------------------------------------

    void savedIndicatorFollowsModifiedState() {
        MainWindow window;
        QCOMPARE(window.savedLabel_->text(), QStringLiteral("Nouveau document"));
        window.setWindowModified(true);
        QCOMPARE(window.savedLabel_->text(), QStringLiteral("Non enregistré"));
        window.currentProjectPath_ = QStringLiteral("C:/tmp/projet.osp");
        window.setWindowModified(false);
        QVERIFY(window.savedLabel_->text().startsWith(QStringLiteral("Enregistré à ")));
    }

    void statusMessagesExpire() {
        MainWindow window;
        window.statusBar()->showMessage(QStringLiteral("message sans délai"));
        QVERIFY(window.statusClearTimer_->isActive());
        QCOMPARE(window.statusClearTimer_->interval(), 10000);
    }

    // ---- Analyse ------------------------------------------------------------------

    void analysisNamesObjectSeverityHintAndSelectsOnActivation() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        st::StitchSequence seq;
        // Point long dont l'arrivée est en (0, 0) : position légitime, pas « absente ».
        seq.commands = {{um(9'500, 0), st::CommandType::Stitch, emb},
                        {um(0, 0), st::CommandType::Stitch, emb}};
        window.sequence_ = seq;
        window.runAnalysis();

        QVERIFY(window.analysisList_->count() >= 1);
        QListWidgetItem* item = window.analysisList_->item(0);
        const QString text = item->text();
        QVERIFY2(text.contains(QStringLiteral("Avertissement")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("« Carre »")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("9,5 mm")), qPrintable(text));
        QVERIFY(!item->data(Qt::UserRole + 3).toString().isEmpty()); // piste de correction
        QVERIFY(item->data(Qt::UserRole + 4).toBool());              // (0, 0) a bien une position

        window.analysisList_->setCurrentItem(item);
        QVERIFY(window.analysisHint_->text().contains(QStringLiteral("Piste")));
        window.activateAnalysisItem(item, true);
        QCOMPARE(window.selectedEmbroidery_, std::optional<ObjectId>(emb));
        QVERIFY(window.analysisSummary_->text().contains(QStringLiteral("avertissement")));
    }

    void analysisFilterAndCapNote() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        st::StitchSequence seq;
        for (int i = 0; i < 80; ++i) {
            seq.commands.push_back({um(i * 9'000, 0), st::CommandType::Stitch, emb});
        }
        window.sequence_ = seq;
        window.runAnalysis();
        // 79 points longs : 50 listés + une note « … et 29 autre(s) ».
        bool note = false;
        int rows = 0;
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            const QString text = window.analysisList_->item(i)->text();
            note = note || text.contains(QStringLiteral("… et 29 autre(s)"));
            rows += window.analysisList_->item(i)->isHidden() ? 0 : 1;
        }
        QVERIFY(note);
        QVERIFY(window.analysisSummary_->text().contains(QStringLiteral("79")));
        // Filtre « Erreurs » : masque les avertissements.
        window.analysisFilter_->setCurrentIndex(1);
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            const QListWidgetItem* item = window.analysisList_->item(i);
            const int rank = item->data(Qt::UserRole + 1).toInt();
            const int shown = rank >= 0 ? rank : item->data(Qt::UserRole + 5).toInt();
            QVERIFY(item->isHidden() || shown == 2);
        }
        QVERIFY(rows > 0);
    }

    void staleAnalysisIsFlaggedAndRefreshedWhenVisible() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.runAnalysis();
        QVERIFY(window.analysisDock_->isVisible());
        QVERIFY(window.analysisHasResult_);
        QVERIFY(!window.analysisIsStale_);
        window.refreshImage(); // le document a changé
        QVERIFY(window.analysisIsStale_);
        QVERIFY(window.analysisTimer_->isActive()); // ré-analyse différée (300 ms)
        QCOMPARE(window.analysisTimer_->interval(), 300);
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisIsStale_, 3000);
    }

    // ---- Simulation ---------------------------------------------------------------

    void incrementalSimulationDrawsWhatAFullRenderWould() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        st::StitchSequence seq;
        for (int i = 0; i < 40; ++i) {
            seq.commands.push_back({um(i * 800, (i % 2) * 600), st::CommandType::Stitch, emb});
        }
        window.sequence_ = seq;
        window.simSlider_->setMaximum(39);

        window.simStep_ = 10;
        window.renderStitches();
        window.appendSimulation(25);
        const int incremental = drawnPathElements(*window.scene_);
        QVERIFY(incremental > 0);

        window.simStep_ = 25;
        window.renderStitches();
        QCOMPARE(drawnPathElements(*window.scene_), incremental);

        // Marche arrière : retombe sur un rendu complet, même résultat qu'un rendu direct.
        window.appendSimulation(12);
        window.simStep_ = 12;
        window.renderStitches();
        const int full12 = drawnPathElements(*window.scene_);
        window.simStep_ = 25;
        window.renderStitches();
        window.simStep_ = 12;
        window.appendSimulation(12);
        QCOMPARE(drawnPathElements(*window.scene_), full12);
    }

    void simulationResetsWhenTheDocumentChanges() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        QVERIFY(window.sequence_.has_value());
        window.simStep_ = 5;
        window.refreshImage();
        QCOMPARE(window.simStep_, -1);
        QVERIFY(!window.simPlayAct_->isChecked());
    }

    void simulationShowsCurrentObjectAndSpeedControl() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        QVERIFY(window.sequence_.has_value());
        QCOMPARE(window.simSpeedCombo_->count(), 4);
        QCOMPARE(window.simSpeedCombo_->itemData(0).toDouble(), 0.25);
        QCOMPARE(window.simSpeedCombo_->itemData(3).toDouble(), 16.0);
        window.simSlider_->setValue(3); // déplace le curseur : objet et couleur courants
        QVERIFY2(window.simObjectLabel_->text().contains(QStringLiteral("Carre")),
                 qPrintable(window.simObjectLabel_->text()));
        QVERIFY(!window.simSwatch_->pixmap(Qt::ReturnByValue).isNull());
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_ux_global.moc"
