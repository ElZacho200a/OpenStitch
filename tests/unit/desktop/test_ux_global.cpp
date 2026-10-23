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
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <variant>

#include "canvas_view.hpp"
#include "design_tokens.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"
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

    void imageOpacitySliderUpdatesOnlyBackgroundAndPersistsAcrossRender() {
        MainWindow window;
        window.applyLoadedProject(imageOnlyProject());
        auto* slider = window.findChild<QSlider*>(QStringLiteral("imageOpacitySlider"));
        QVERIFY(slider != nullptr);
        QCOMPARE(slider->value(), 100);
        auto background = [&window]() -> QGraphicsItem* {
            for (QGraphicsItem* item : window.baseItems_) {
                if (item->data(0).toString() == QStringLiteral("backgroundImage")) {
                    return item;
                }
            }
            return nullptr;
        };
        QVERIFY(background() != nullptr);
        QCOMPARE(background()->opacity(), 1.0);
        slider->setValue(30);
        QCOMPARE(background()->opacity(), 0.3);
        QCOMPARE(QSettings().value(QStringLiteral("ui/imageOpacity")).toDouble(), 0.3);
        window.refreshImage();
        QVERIFY(background() != nullptr);
        QCOMPARE(background()->opacity(), 0.3);
        slider->setValue(0);
        QCOMPARE(background()->opacity(), 0.0);
        slider->setValue(100);
        QCOMPARE(background()->opacity(), 1.0);
        QVERIFY(!window.undoStack_.canUndo()); // réglage visuel, pas une mutation du projet
    }

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
            // Limites des contrôles (WCAG 1.4.11) : >= 3:1 sur les surfaces où elles s'appuient.
            for (const QColor& bg : {t.window, t.surface, t.surfaceRaised}) {
                QVERIFY2(contrast(t.borderStrong, bg) >= 3.0,
                         qPrintable(QStringLiteral("borderStrong sur %1 : %2")
                                        .arg(bg.name())
                                        .arg(contrast(t.borderStrong, bg))));
            }
            // Texte sur l'accent (boutons, sélection) et sur le fond teinté (survol, ligne active).
            QVERIFY(contrast(t.selectionText, t.accent) >= 4.5);
            QVERIFY(contrast(t.text, t.accentSoft) >= 4.5);
            // Le texte désactivé est distinct du texte d'aide.
            QVERIFY(t.textDisabled != t.textSecondary);
        }
    }

    void darkCanvasGridIsVisibleOnFabricAndOutsideTheHoop() {
        const Tokens tokens = dark_tokens();
        // drawBackground peint toujours le tissu en blanc, même en thème sombre.
        // Mesurer les couleurs composées (pas les pixels d'une capture).
        for (const QColor& background : {QColor(Qt::white), tokens.canvasBackground}) {
            for (const bool fine : {false, true}) {
                QColor grid = tokens.canvasGrid;
                if (fine) {
                    grid.setAlpha(grid.alpha() / 2);
                }
                const double alpha = grid.alphaF();
                const QColor composite =
                    QColor::fromRgbF(alpha * grid.redF() + (1.0 - alpha) * background.redF(),
                                     alpha * grid.greenF() + (1.0 - alpha) * background.greenF(),
                                     alpha * grid.blueF() + (1.0 - alpha) * background.blueF());
                QVERIFY2(contrast(composite, background) >= (fine ? 1.25 : 3.0),
                         qPrintable(QStringLiteral("grille %1 sur %2 : %3")
                                        .arg(fine ? "fine" : "majeure", background.name())
                                        .arg(contrast(composite, background))));
            }
        }
    }

    void vectorHoverTooltipContainsSizeStitchTypeAndThreadColor() {
        doc::Project project = imageOnlyProject();
        const ObjectId vector = addSquareVector(project);
        addEmbroidery(project, vector);
        MainWindow window;
        window.applyLoadedProject(project);
        int tooltips = 0;
        for (const QGraphicsItem* item : window.baseItems_) {
            if (item->data(0).toString() != QStringLiteral("vectorShape")) {
                continue;
            }
            const QString info = item->toolTip();
            QVERIFY(info.contains(QStringLiteral("Square")));
            QVERIFY(info.contains(QStringLiteral("10.0")));
            QVERIFY(info.contains(QStringLiteral("mm")));
            QVERIFY(info.contains(QStringLiteral("Point droit")));
            QVERIFY(info.contains(QStringLiteral("RVB")));
            ++tooltips;
        }
        QCOMPARE(tooltips, 1);
        QVERIFY(!window.undoStack_.canUndo());
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
        QVERIFY(window.restoreState(state, 2));
        QVERIFY(!window.restoreState(state, 3)); // version différente : refusée
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

    // ---- HP-UX-017 : Ctrl+O ouvre tout ------------------------------------------------------

    void ctrlOIsTheUniversalOpenAndImportHasItsOwnShortcut() {
        MainWindow window;
        QCOMPARE(window.openAnyAct_->shortcut(), QKeySequence(QKeySequence::Open));
        QCOMPARE(window.openImageAct_->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
        // Aucun autre raccourci d'action ne réutilise Ctrl+O.
        int uses = 0;
        for (const QAction* action : window.findChildren<QAction*>()) {
            for (const QKeySequence& sequence : action->shortcuts()) {
                uses += sequence == QKeySequence(QKeySequence::Open) ? 1 : 0;
            }
        }
        QCOMPARE(uses, 1);
    }

    // ---- HP-UX-018 : Échap remonte d'un niveau -----------------------------------------------

    void escapeDropsToolThenObjectSelection() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId emb = addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        window.setSelection({.region = std::nullopt, .embroidery = emb, .objects = {}});
        window.updateActions();
        window.setTool(Tool::DrawPolygon);

        // 1er Échap : quitte l'outil, la sélection est conservée.
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(window.currentTool_, Tool::Select);
        QVERIFY(window.currentSelection().embroidery.has_value());
        // 2e Échap : plus rien à relâcher, les objets sont désélectionnés.
        QTest::keyClick(&window, Qt::Key_Escape);
        QVERIFY(!window.currentSelection().embroidery.has_value());
        QVERIFY(window.currentSelection().objects.empty());
        // 3e Échap sur une sélection vide : sans effet ni erreur.
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(window.currentTool_, Tool::Select);
    }

    // ---- HP-UX-022 : sortie de simulation ----------------------------------------------------

    void simulationExitButtonAndEndOfSliderLeaveSimulationMode() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        QVERIFY(window.sequence_.has_value());
        const int last = window.simSlider_->maximum();
        QVERIFY(last > 3);

        window.simSlider_->setValue(3); // simulation en cours : le rendu réaliste est coupé
        QVERIFY(window.simulating());
        window.simExitAct_->trigger();
        QVERIFY(!window.simulating());
        QCOMPARE(window.simSlider_->value(), last);

        window.simSlider_->setValue(3);
        QVERIFY(window.simulating());
        window.simSlider_->setValue(last); // curseur tout à droite = couture complète
        QVERIFY(!window.simulating());
    }

    void endOfPlaybackLeavesSimulationMode() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addEmbroidery(project, addSquareVector(project));
        window.applyLoadedProject(project);
        QVERIFY(window.sequence_.has_value());
        window.simSlider_->setValue(2);
        QVERIFY(window.simulating());
        window.simStep_ = static_cast<int>(window.sequence_->commands.size()) - 2;
        window.simSpeedCombo_->setCurrentIndex(3); // ×16 : le tick suivant atteint la fin
        window.simPlayAct_->setChecked(true);
        for (int i = 0; i < 4000 && window.simulating(); ++i) {
            window.onSimTick();
        }
        QVERIFY(!window.simulating());
        QVERIFY(!window.simPlayAct_->isChecked());
    }

    // ---- HP-UX-020 : cyclage de sélection par re-clic ----------------------------------------

    void clickingTheSameSpotCyclesThroughOverlappingObjects() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId a = addSquareVector(project); // dessous
        const ObjectId b = addSquareVector(project); // dessus
        const ObjectId c = addSquareVector(project); // tout dessus
        window.applyLoadedProject(project);
        window.setTool(Tool::Select);
        window.view_->setTransform(QTransform::fromScale(10.0, 10.0)); // 10 px/mm : rayon 0,2 mm
        const QPointF inside(5.0, -5.0);                               // scène : Y vers le bas
        window.onCanvasClicked(inside);
        QCOMPARE(window.currentSelection().objects, std::vector<ObjectId>{c});
        window.onCanvasClicked(inside);
        QCOMPARE(window.currentSelection().objects, std::vector<ObjectId>{b});
        window.onCanvasClicked(inside);
        QCOMPARE(window.currentSelection().objects, std::vector<ObjectId>{a});
        window.onCanvasClicked(inside); // retour en haut
        QCOMPARE(window.currentSelection().objects, std::vector<ObjectId>{c});
        // Un clic ailleurs (hors rayon) remet le cycle à zéro : à nouveau l'objet du dessus.
        window.onCanvasClicked(QPointF(9.0, -9.0));
        QCOMPARE(window.currentSelection().objects, std::vector<ObjectId>{c});
    }

    // ---- HP-UX-023 : Ctrl+A suit la couche, touches de couches, zoom sur la sélection ---------

    void selectAllFollowsTheActiveLayer() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId a = addSquareVector(project);
        const ObjectId b = addSquareVector(project);
        window.applyLoadedProject(project);
        window.updateActions();
        QVERIFY(window.selectAllRegionsAct_->isEnabled()); // pas de segmentation, mais des formes
        window.selectAllRegionsAct_->trigger();
        auto ids = window.currentSelection().objects;
        std::sort(ids.begin(), ids.end());
        QCOMPARE(ids, (std::vector<ObjectId>{a, b}));
    }

    void layerKeysAndZoomShortcutsAreUnique() {
        MainWindow window;
        QCOMPARE(window.showImageAct_->shortcut(), QKeySequence(Qt::Key_1));
        QVERIFY(window.showSegAct_->shortcuts().contains(QKeySequence(Qt::Key_2)));
        QVERIFY(window.showSegAct_->shortcuts().contains(QKeySequence(Qt::Key_W)));
        QCOMPARE(window.showVectorsAct_->shortcut(), QKeySequence(Qt::Key_3));
        QCOMPARE(window.showStitchesAct_->shortcut(), QKeySequence(Qt::Key_4));
        for (const QKeySequence& key :
             {QKeySequence(Qt::Key_Z), QKeySequence(Qt::SHIFT | Qt::Key_Z),
              QKeySequence(Qt::CTRL | Qt::Key_1), QKeySequence(Qt::Key_1),
              QKeySequence(Qt::Key_3)}) {
            int uses = 0;
            for (const QAction* action : window.findChildren<QAction*>()) {
                uses += action->shortcuts().contains(key) ? 1 : 0;
            }
            QVERIFY2(uses == 1, qPrintable(key.toString()));
        }
    }

    void zoomToSelectionFramesTheObjectAndPreviousRestoresTheView() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId a = addSquareVector(project);
        window.applyLoadedProject(project);
        window.resize(900, 700);
        window.show();
        QCoreApplication::processEvents();
        window.view_->fitCanvas();
        const double before = window.view_->pixelsPerMm();
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.zoomSelectionAct_->trigger();
        QVERIFY(window.view_->pixelsPerMm() > before); // carré de 10 mm : zoom avant
        const QPointF centre = window.view_->mapFromScene(QPointF(5.0, -5.0));
        QVERIFY(window.view_->viewport()->rect().contains(centre.toPoint()));
        window.zoomPreviousAct_->trigger();
        QVERIFY(std::abs(window.view_->pixelsPerMm() - before) < 1e-6);
    }

    // ---- HP-UX-034 : mode Isoler (état de session, rien dans le projet) -----------------------

    void isolationHidesAndExcludesOthersWithoutTouchingTheProject() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId a = addSquareVector(project);
        const ObjectId b = addSquareVector(project);
        window.applyLoadedProject(project);
        window.view_->setTransform(QTransform::fromScale(10.0, 10.0));
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.updateActions();
        QVERIFY(window.isolateAct_->isEnabled());

        // Octets du projet avant et pendant l'isolation : identiques (rien n'est persisté).
        QTemporaryDir dir;
        const auto before = (dir.path() + "/before.osp").toStdString();
        const auto during = (dir.path() + "/during.osp").toStdString();
        QVERIFY(openstitch::project_io::save_project(before, window.project_).has_value());
        window.isolateAct_->trigger();
        QVERIFY(window.isolating());
        QVERIFY(openstitch::project_io::save_project(during, window.project_).has_value());
        QFile f1(QString::fromStdString(before));
        QFile f2(QString::fromStdString(during));
        QVERIFY(f1.open(QIODevice::ReadOnly) && f2.open(QIODevice::ReadOnly));
        QCOMPARE(f1.readAll(), f2.readAll());

        // `b` est au-dessus de `a` au même endroit : isolé, seul `a` est saisissable.
        const QPointF inside(5.0, -5.0);
        QCOMPARE(window.selectableObjectsAt(inside), std::vector<ObjectId>{a});
        window.setSelection({});
        window.onCanvasClicked(inside);
        QCOMPARE(window.currentSelection().objects, std::vector<ObjectId>{a});

        // Fin de l'isolation : `b` redevient saisissable ; un nouveau document la remet à zéro.
        window.isolateAct_->trigger();
        QVERIFY(!window.isolating());
        QCOMPARE(window.selectableObjectsAt(inside).front(), b);
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.updateActions();
        window.isolateAct_->trigger();
        QVERIFY(window.isolating());
        window.applyLoadedProject(imageOnlyProject());
        QVERIFY(!window.isolating());
        QVERIFY(!window.isolateAct_->isChecked());
    }

    // ---- HP-UX-036 : sauts de couleur dans la simulation, message de couche masquée -----------

    void simulationColorJumpsLandOnColorBlocks() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId v1 = addSquareVector(project);
        const ObjectId v2 = addSquareVector(project);
        doc::EmbroideryObject first;
        first.id = project.object_ids.next();
        first.name = "A";
        first.source_vector = v1;
        first.rgb = {200, 0, 0};
        first.params = doc::RunningStitchParams{};
        project.embroidery_objects.push_back(first);
        doc::EmbroideryObject second;
        second.id = project.object_ids.next();
        second.name = "B";
        second.source_vector = v2;
        second.rgb = {0, 0, 200};
        second.params = doc::RunningStitchParams{};
        project.embroidery_objects.push_back(second);
        window.applyLoadedProject(project);
        QVERIFY(window.sequence_.has_value());
        const std::vector<int> starts = window.colorBlockStarts();
        QVERIFY2(starts.size() >= 2, "deux fils différents : deux blocs de couleur");
        const int last = static_cast<int>(window.sequence_->commands.size()) - 1;

        window.simSlider_->setValue(1); // simulation au début du premier bloc
        QVERIFY(window.simulating());
        window.simNextColorAct_->trigger();
        QCOMPARE(window.simStep_, starts[1]); // début du bloc suivant
        QVERIFY(window.simObjectLabel_->toolTip().contains(QStringLiteral("fil 2/")));
        window.simPrevColorAct_->trigger();
        QCOMPARE(window.simStep_, starts[0]);
        // Après le dernier bloc, « suivant » va à la fin : on sort de la simulation.
        window.simSlider_->setValue(starts.back());
        window.simNextColorAct_->trigger();
        QVERIFY(!window.simulating());
        QCOMPARE(window.simSlider_->value(), last);
    }

    void hiddenVectorLayerExplainsAClickOnAShape() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        addSquareVector(project);
        window.applyLoadedProject(project);
        window.setTool(Tool::Select);
        window.showVectorsAct_->setChecked(false);
        window.statusBar()->clearMessage();
        window.onCanvasClicked(QPointF(5.0, -5.0));
        QVERIFY2(window.statusBar()->currentMessage().contains(QStringLiteral("masquée")),
                 qPrintable(window.statusBar()->currentMessage()));
    }

    // ---- HP-UX-039 (UX-32) : l'heure d'enregistrement est celle de la sauvegarde --------------

    void savedIndicatorShowsTheSaveTimeNotTheRefreshTime() {
        MainWindow window;
        window.applyLoadedProject(imageOnlyProject());
        window.currentProjectPath_ = QStringLiteral("projet.osp");
        window.savedAt_ = QTime(10, 30);
        window.setWindowModified(false);
        window.updateSavedIndicator();
        QVERIFY2(window.savedLabel_->text().contains(QStringLiteral("10:30")),
                 qPrintable(window.savedLabel_->text()));
        // Modifié puis revenu propre (undo) : toujours l'heure de la sauvegarde, pas l'heure
        // courante.
        window.setWindowModified(true);
        QCOMPARE(window.savedLabel_->text(), tr("Non enregistré"));
        window.setWindowModified(false);
        window.updateSavedIndicator();
        QVERIFY(window.savedLabel_->text().contains(QStringLiteral("10:30")));
    }

    // ---- HP-UX-035 : copier / coller le style --------------------------------------------------

    void pasteStyleCopiesParamsInOneUndoableStepAndSkipsOtherTypes() {
        MainWindow window;
        doc::Project project = imageOnlyProject();
        const ObjectId v1 = addSquareVector(project);
        const ObjectId v2 = addSquareVector(project);
        const ObjectId v3 = addSquareVector(project);
        doc::EmbroideryObject a;
        a.id = project.object_ids.next();
        a.name = "A";
        a.source_vector = v1;
        doc::TatamiParams tuned;
        tuned.row_spacing = Micrometers{250};
        tuned.angle = openstitch::Angle{0.5};
        a.params = tuned;
        project.embroidery_objects.push_back(a);
        doc::EmbroideryObject b = a;
        b.id = project.object_ids.next();
        b.name = "B";
        b.source_vector = v2;
        b.params = doc::TatamiParams{};
        project.embroidery_objects.push_back(b);
        doc::EmbroideryObject c = a;
        c.id = project.object_ids.next();
        c.name = "C";
        c.source_vector = v3;
        c.params = doc::RunningStitchParams{};
        project.embroidery_objects.push_back(c);
        window.applyLoadedProject(project);
        const ObjectId idA = project.embroidery_objects[0].id;
        const ObjectId idB = project.embroidery_objects[1].id;
        const ObjectId idC = project.embroidery_objects[2].id;

        window.setSelection({.region = std::nullopt, .embroidery = idA, .objects = {}});
        window.updateActions();
        QVERIFY(window.copyStyleAct_->isEnabled());
        window.copyStyleAct_->trigger();
        QVERIFY(window.pasteStyleAct_->isEnabled());

        // Colle sur B (même type) : paramètres identiques, un pas d'annulation.
        window.setSelection({.region = std::nullopt, .embroidery = idB, .objects = {}});
        window.updateActions();
        window.pasteStyleAct_->trigger();
        QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(idB)->params), tuned);
        window.undo();
        QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(idB)->params),
                 doc::TatamiParams{});
        window.redo();
        QCOMPARE(std::get<doc::TatamiParams>(window.project_.findEmbroidery(idB)->params), tuned);

        // Sur C (point droit) : autre type, ignoré, aucun changement.
        window.setSelection({.region = std::nullopt, .embroidery = idC, .objects = {}});
        window.updateActions();
        window.pasteStyleAct_->trigger();
        QVERIFY(std::holds_alternative<doc::RunningStitchParams>(
            window.project_.findEmbroidery(idC)->params));
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_ux_global.moc"
