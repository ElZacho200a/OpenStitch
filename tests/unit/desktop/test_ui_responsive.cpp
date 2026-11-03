// SPDX-License-Identifier: Apache-2.0
// Garde de taille minimale de l'interface (HP-QA-008) et tests du lot B « Responsive et
// panneaux » (HP-UX-024 à 027) : la fenêtre, tous panneaux ouverts, et chaque dialogue doivent
// tenir dans un petit écran ; les docks lourds défilent ; la palette passe sur deux colonnes ;
// la densité suit la hauteur de l'écran ; le mode portable libère le canevas.
// Headless (QT_QPA_PLATFORM=offscreen), sans comparaison de pixels ni sleep. Les mesures sont
// imprimées (qInfo) pour être consignées dans docs/ux/mesures-dialogues.md.
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFormLayout>
#include <QGraphicsView>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <functional>
#include <memory>

#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"
#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "color_blend_dialog.hpp"
#include "empty_state_widget.hpp"
#include "export_image_dialog.hpp"
#include "help_dialogs.hpp"
#include "import_dialog.hpp"
#include "machine_export_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "production_dialog.hpp"
#include "properties_panel.hpp"
#include "realistic_render_dialog.hpp"
#include "segmentation_colors_panel.hpp"
#include "segmentation_dialog.hpp"
#include "settings_dialog.hpp"
#include "text_dialog.hpp"
#include "thread_panel.hpp"
#include "thread_strip.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

doc::Project projectWithSequence() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    doc::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Square";
    geo::Path square;
    square.closed = true;
    constexpr std::int32_t s = 20'000;
    for (const auto& [x, y] :
         std::vector<std::pair<std::int32_t, std::int32_t>>{{0, 0}, {s, 0}, {s, s}, {0, s}}) {
        square.nodes.push_back(geo::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                             geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vec.paths.push_back(geo::PathSet{square, {}});
    const ObjectId vecId = vec.id;
    project.vector_objects.push_back(std::move(vec));
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "Carre";
    emb.source_vector = vecId;
    emb.params = doc::RunningStitchParams{};
    project.embroidery_objects.push_back(std::move(emb));
    return project;
}

// Bornes du budget « petit portable » (HP-QA-008) : fenêtre ≤ 911 × 480 (objectif long terme
// 683 × 360), dialogues ≤ 880 × 440, aide ≤ 460 × 320.
constexpr int kWindowMaxW = 911;
constexpr int kWindowMaxH = 480;
constexpr int kDialogMaxW = 880;
constexpr int kDialogMaxH = 440;
constexpr int kHelpMaxW = 460;
constexpr int kHelpMaxH = 320;

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

    static void showAllDocks(MainWindow& window) {
        window.showColorsPanel();
        for (QDockWidget* dock :
             {window.documentDock_, window.propertiesDock_, window.workflowDock_, window.orderDock_,
              window.filterDock_, window.analysisDock_, window.historyDock_, window.threadDock_,
              window.colorsDock_}) {
            if (dock != nullptr) {
                dock->show();
            }
        }
    }

    static void report(const char* name, const QSize& size) {
        qInfo().noquote() << QStringLiteral("MESURE %1 : %2 x %3")
                                 .arg(QLatin1String(name))
                                 .arg(size.width())
                                 .arg(size.height());
    }

    static QSize usableArea() {
        const QScreen* screen = QGuiApplication::primaryScreen();
        return screen != nullptr ? screen->availableGeometry().size() : QSize(800, 600);
    }

    // Taille minimale réellement imposée par un dialogue : la contrainte du layout (qui honore
    // un `setMinimumSize` explicite), pas seulement `minimumSizeHint()` du contenu.
    static QSize effectiveMinimum(QWidget* widget) {
        if (widget->isWindow() && widget->layout() != nullptr) {
            widget->layout()->activate();
            return widget->minimumSize().expandedTo(QSize(0, 0));
        }
        return widget->minimumSizeHint();
    }

    // Instancie un dialogue, mesure son `minimumSizeHint()` et note les dépassements de budget
    // (le test échoue à la fin, après avoir mesuré TOUS les dialogues).
    template <typename Dialog>
    static void checkDialog(QStringList& failures, const char* name, int maxW, int maxH,
                            std::unique_ptr<Dialog> dialog) {
        const QSize hint = effectiveMinimum(dialog.get());
        report(name, hint);
        if (hint.width() > maxW || hint.height() > maxH) {
            failures << QStringLiteral("%1 : %2 x %3 > %4 x %5")
                            .arg(QLatin1String(name))
                            .arg(hint.width())
                            .arg(hint.height())
                            .arg(maxW)
                            .arg(maxH);
        }
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("UiResponsiveTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void init() { QSettings().clear(); }

    // ---- HP-QA-008 : garde de taille de la fenêtre -------------------------------------

    void windowMinimumSizeWithAllDocksFitsTheBudget() {
        MainWindow window;
        window.applyLoadedProject(projectWithSequence());
        showAllDocks(window);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
            report(qPrintable(QStringLiteral("  dock %1").arg(dock->objectName())),
                   dock->minimumSizeHint());
        }
        for (QToolBar* bar : window.findChildren<QToolBar*>()) {
            report(qPrintable(QStringLiteral("  barre %1 (hint)").arg(bar->objectName())),
                   bar->sizeHint());
            report(qPrintable(QStringLiteral("  barre %1 (min)").arg(bar->objectName())),
                   bar->minimumSizeHint());
        }
        report("  barre d'etat (min)", window.statusBar()->minimumSizeHint());
        report("  menu (hint)", window.menuBar()->sizeHint());
        report("  canevas (min)", window.view_->minimumSizeHint());
        const QSize hint = window.minimumSizeHint();
        report("fenetre min (tous docks)", hint);
        QVERIFY2(hint.width() <= kWindowMaxW, qPrintable(QString::number(hint.width())));
        QVERIFY2(hint.height() <= kWindowMaxH, qPrintable(QString::number(hint.height())));
    }

    void threadDockMinimumHeightIsBounded() {
        MainWindow window;
        window.threadDock_->show();
        report("dock fils min", window.threadDock_->minimumSizeHint());
        QVERIFY(window.threadDock_->minimumSizeHint().height() <= 200);
    }

    void colorsDockMinimumHeightIsBounded() {
        MainWindow window;
        window.showColorsPanel();
        report("dock couleurs min", window.colorsDock_->minimumSizeHint());
        QVERIFY(window.colorsDock_->minimumSizeHint().height() <= 200);
    }

    // ---- HP-QA-008 : garde de taille des dialogues -------------------------------------

    void dialogsMinimumSizeFitsWorkArea() {
        QStringList failures;
        checkDialog<TextDialog>(failures, "dialogue Texte", kDialogMaxW, kDialogMaxH,
                                std::make_unique<TextDialog>(doc::TextObject{}, true));
        checkDialog<MachineExportDialog>(
            failures, "dialogue Export machine", kDialogMaxW, kDialogMaxH,
            std::make_unique<MachineExportDialog>(
                [](const formats::FormatInfo&, const formats::MachineExportOptions&,
                   const project_io::ExportOptimizations&) {
                    return MachineExportDialog::Summary{};
                },
                QStringLiteral("dst"), nullptr));
        checkDialog<ColorBlendDialog>(
            failures, "dialogue Melange de couleurs", kDialogMaxW, kDialogMaxH,
            std::make_unique<ColorBlendDialog>(ColorBlendDialog::Rgb{200, 30, 30},
                                               ColorBlendDialog::Rgb{30, 30, 200}, false, false));
        image::Image img;
        img.width = 4;
        img.height = 4;
        img.rgba.assign(4 * 4 * 4, 255);
        checkDialog<AiSegmentationDialog>(
            failures, "dialogue Segmentation IA", kDialogMaxW, kDialogMaxH,
            std::make_unique<AiSegmentationDialog>(img, Millimeters{0.2}, AiPreferences{}));
        const doc::Project project = projectWithSequence();
        const stitch::StitchSequence sequence;
        checkDialog<ProductionDialog>(
            failures, "dialogue Fiche de production", kDialogMaxW, kDialogMaxH,
            std::make_unique<ProductionDialog>(project, sequence, QStringLiteral("Projet"),
                                               stitch_render::RenderParams{}));
        checkDialog<ImportDialog>(failures, "dialogue Import", kDialogMaxW, kDialogMaxH,
                                  std::make_unique<ImportDialog>(3000, 2000,
                                                                 QImage(4, 4, QImage::Format_RGB32),
                                                                 QSizeF(100.0, 100.0)));
        checkDialog<SegmentationDialog>(failures, "dialogue Segmentation", kDialogMaxW, kDialogMaxH,
                                        std::make_unique<SegmentationDialog>(0.2));
        checkDialog<SettingsDialog>(failures, "dialogue Reglages", kDialogMaxW, kDialogMaxH,
                                    std::make_unique<SettingsDialog>());
        // Contenu défilant (scrollDialogBody) : la hauteur minimale tient dans l'écran de travail.
        checkDialog<AiPreferencesDialog>(failures, "dialogue Preferences IA", kDialogMaxW,
                                         kDialogMaxH, std::make_unique<AiPreferencesDialog>());
        checkDialog<RealisticRenderDialog>(
            failures, "dialogue Rendu realiste", kDialogMaxW, kDialogMaxH,
            std::make_unique<RealisticRenderDialog>(RealisticPreferences{}));
        checkDialog<ExportImageDialog>(
            failures, "dialogue Export image", kDialogMaxW, kDialogMaxH,
            std::make_unique<ExportImageDialog>(100.0, 100.0, stitch_render::RenderParams{}));
        checkDialog<EmptyStateWidget>(failures, "ecran d'accueil", kDialogMaxW, kDialogMaxH,
                                      std::make_unique<EmptyStateWidget>());
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral(" ; "))));
    }

    void emptyStateWithLongRecentNamesStaysBounded() {
        EmptyStateWidget widget;
        QStringList recents;
        for (int i = 0; i < 5; ++i) {
            recents << QStringLiteral("C:/Users/quelquun/Documents/Projets de broderie/"
                                      "un-nom-de-projet-vraiment-tres-long-numero-%1.osp")
                           .arg(i);
        }
        widget.setRecentFiles(recents);
        const QSize min = effectiveMinimum(&widget);
        report("accueil (5 recents longs)", min);
        QVERIFY(min.height() <= kDialogMaxH);
        QVERIFY(min.width() <= kDialogMaxW);
    }

    void helpDialogsMinimumSizeFitsTheHelpBudget() {
        QObject noActions;
        GesturesDialog gestures(&noActions);
        const QSize gesturesMin = effectiveMinimum(&gestures);
        report("aide gestes", gesturesMin);
        QVERIFY(gesturesMin.width() <= kHelpMaxW);
        QVERIFY(gesturesMin.height() <= kHelpMaxH);
        QuickStartDialog quick(&noActions);
        const QSize quickMin = effectiveMinimum(&quick);
        report("aide demarrage rapide", quickMin);
        QVERIFY(quickMin.width() <= kHelpMaxW);
        QVERIFY(quickMin.height() <= kHelpMaxH);
    }

    // ---- HP-UX-025 : dialogues bornés à l'écran ----------------------------------------

    void oversizedDialogsAreClampedToTheScreenOnShow() {
        const QSize area = usableArea();
        const int maxW = static_cast<int>(area.width() * 0.92) + 1;
        const int maxH = static_cast<int>(area.height() * 0.92) + 1;
        const auto clamped = [&](QDialog& dialog, const char* name) {
            dialog.resize(10000, 10000);
            dialog.show();
            QVERIFY2(dialog.width() <= maxW, name);
            QVERIFY2(dialog.height() <= maxH, name);
            dialog.hide();
        };
        TextDialog text(doc::TextObject{}, true);
        clamped(text, "Texte");
        image::Image img;
        img.width = 4;
        img.height = 4;
        img.rgba.assign(4 * 4 * 4, 255);
        AiSegmentationDialog ai(img, Millimeters{0.2}, AiPreferences{});
        clamped(ai, "Segmentation IA");
        ColorBlendDialog blend(ColorBlendDialog::Rgb{200, 30, 30},
                               ColorBlendDialog::Rgb{30, 30, 200}, false, false);
        clamped(blend, "Melange");
        MachineExportDialog exportDialog(
            [](const formats::FormatInfo&, const formats::MachineExportOptions&,
               const project_io::ExportOptimizations&) { return MachineExportDialog::Summary{}; },
            QStringLiteral("dst"), nullptr);
        clamped(exportDialog, "Export machine");
        const doc::Project project = projectWithSequence();
        const stitch::StitchSequence sequence;
        ProductionDialog production(project, sequence, QStringLiteral("Projet"),
                                    stitch_render::RenderParams{});
        clamped(production, "Production");
    }

    // ---- HP-UX-024 : docks en onglets, docks défilants ---------------------------------

    void heavyDocksScrollInsteadOfForcingTheirMinimumSize() {
        MainWindow window;
        window.showColorsPanel();
        QVERIFY(qobject_cast<QScrollArea*>(window.threadDock_->widget()) != nullptr);
        QVERIFY(qobject_cast<QScrollArea*>(window.colorsDock_->widget()) != nullptr);
        QVERIFY(qobject_cast<QScrollArea*>(window.colorsDock_->widget())->widgetResizable());
        QVERIFY(qobject_cast<QScrollArea*>(window.threadDock_->widget())->widgetResizable());
        // Le panneau reste joignable (les tests et le câblage le retrouvent par le membre).
        QVERIFY(window.threadPanel_ != nullptr);
        QVERIFY(window.colorsPanel_ != nullptr);
    }

    void secondaryDocksAreTabbedTogether() {
        MainWindow window;
        showAllDocks(window); // un dock caché n'apparaît pas dans les onglets
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QList<QDockWidget*> tabbed = window.tabifiedDockWidgets(window.analysisDock_);
        QVERIFY(tabbed.contains(window.historyDock_));
        QVERIFY(tabbed.contains(window.threadDock_));
        QVERIFY(tabbed.contains(window.colorsDock_));
    }

    void colorsDockExistsHiddenFromTheStartAndSurvivesALayoutRoundTrip() {
        MainWindow window;
        QVERIFY(window.colorsDock_ != nullptr);
        QVERIFY(!window.colorsDock_->isVisible());
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.showColorsPanel();
        QVERIFY(window.colorsDock_->isVisible());
        window.saveUiLayout();
        window.colorsDock_->hide();
        window.restoreUiLayout();
        QVERIFY(window.colorsDock_->isVisible());
    }

    // ---- HP-UX-026 : barres, palette, densité ------------------------------------------

    void canvasKeepsAUsableSizeOnASmallWindowWithASequence() {
        MainWindow window;
        window.applyLoadedProject(projectWithSequence());
        window.resize(911, 480);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QApplication::processEvents();
        report("canevas a 911x480", window.view_->size());
        QVERIFY2(window.view_->height() >= 260,
                 qPrintable(QString::number(window.view_->height())));
        // Largeur : avec Document (260) et Propriétés (320) ouverts de part et d'autre, 450 px
        // ne sont pas atteignables à 911 px (cf. portableModeLeavesRoomForTheCanvas) ; on
        // garde seulement la non-régression de la mesure d'origine (127 px).
        QVERIFY2(window.view_->width() >= 200, qPrintable(QString::number(window.view_->width())));
    }

    void finishDrawActionStaysVisibleDuringADrawing() {
        MainWindow window;
        window.applyLoadedProject(projectWithSequence());
        window.resize(911, 480);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.setTool(Tool::DrawPolygon);
        QApplication::processEvents();
        QVERIFY(window.finishDrawAct_ != nullptr);
        QVERIFY(!window.finishDrawAct_->associatedObjects().isEmpty());
        bool reachable = false;
        for (QObject* object : window.finishDrawAct_->associatedObjects()) {
            auto* bar = qobject_cast<QToolBar*>(object);
            if (bar == nullptr) {
                continue;
            }
            QWidget* widget = bar->widgetForAction(window.finishDrawAct_);
            // Visible dans la barre elle-même (pas reléguée dans le menu de débordement « » ).
            reachable = reachable || (widget != nullptr && widget->isVisibleTo(bar) &&
                                      bar->contentsRect().contains(widget->geometry()));
        }
        QVERIFY(reachable);
    }

    void toolPaletteSplitsInTwoColumnsOnAShortWindowAndMergesBack() {
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QApplication::processEvents();
        const int toolCount = window.toolPalette_->actions().size();
        auto* second = window.findChild<QToolBar*>(QStringLiteral("toolPalette2"));
        QVERIFY(second == nullptr || !second->isVisible());
        QVERIFY(window.toolPalette_->actions().contains(window.toolCutAct_));

        window.resize(1000, 480);
        QApplication::processEvents();
        second = window.findChild<QToolBar*>(QStringLiteral("toolPalette2"));
        QVERIFY(second != nullptr && second->isVisible());
        QVERIFY(!second->actions().isEmpty());
        QVERIFY(window.toolPalette_->actions().size() < toolCount);
        // Aucune action perdue ni dupliquée : la somme des deux colonnes est la palette d'origine.
        QCOMPARE(window.toolPalette_->actions().size() + second->actions().size(), toolCount);
        QVERIFY(second->actions().contains(window.cancelDrawAct_) ||
                window.toolPalette_->actions().contains(window.cancelDrawAct_));

        window.resize(1000, 700);
        QApplication::processEvents();
        QCOMPARE(window.toolPalette_->actions().size(), toolCount);
        QVERIFY(second->actions().isEmpty());
    }

    void bottomBarsAreVisibleOnlyWhenASequenceExists() {
        MainWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.simToolbar_->isVisible());
        QVERIFY(!window.threadStripBar_->isVisible());
        window.applyLoadedProject(projectWithSequence());
        QApplication::processEvents();
        QVERIFY(window.simToolbar_->isVisible());
        QVERIFY(window.threadStripBar_->isVisible());
        report("bande des fils (taille)", window.threadStrip_->size());
        report("barre simulation (taille)", window.simToolbar_->size());
        // Les deux barres partagent la rangée du bas.
        QCOMPARE(window.toolBarArea(window.simToolbar_), Qt::BottomToolBarArea);
        QCOMPARE(window.toolBarArea(window.threadStripBar_), Qt::BottomToolBarArea);
    }

    void densityDefaultsToCompactOnAShortScreen() {
        QSettings().clear();
        QCOMPARE(AppTheme::densityForScreenHeight(480), Density::Compact);
        QCOMPARE(AppTheme::densityForScreenHeight(799), Density::Compact);
        QCOMPARE(AppTheme::densityForScreenHeight(800), Density::Comfortable);
        QCOMPARE(AppTheme::densityForScreenHeight(1080), Density::Comfortable);
    }

    void statusBarDoesNotForceTheWindowWidth() {
        MainWindow window;
        window.applyLoadedProject(projectWithSequence());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        report("barre d'etat min", window.statusBar()->minimumSizeHint());
        QVERIFY(window.statusBar()->minimumSizeHint().width() <= 450);
    }

    // ---- HP-QA-008 (trou de couverture) : noms accessibles ----------------------------

    void docksAndCanvasExposeAccessibleNames() {
        MainWindow window;
        QVERIFY(!window.view_->accessibleName().isEmpty());
        for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
            QVERIFY2(
                !dock->accessibleName().isEmpty(),
                qPrintable(QStringLiteral("dock sans nom accessible : ") + dock->objectName()));
        }
    }

    // ---- HP-UX-027 : mode portable et inspecteur étroit --------------------------------

    void portableModeLeavesRoomForTheCanvas() {
        MainWindow window;
        window.applyLoadedProject(projectWithSequence());
        window.resize(911, 480);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* portable = window.findChild<QAction*>(QStringLiteral("action_portableMode"));
        QVERIFY(portable != nullptr);
        portable->setChecked(true);
        QApplication::processEvents();
        QVERIFY(window.tabifiedDockWidgets(window.documentDock_).contains(window.propertiesDock_));
        report("canevas a 911x480 (portable)", window.view_->size());
        QVERIFY2(window.view_->width() >= 450, qPrintable(QString::number(window.view_->width())));
        portable->setChecked(false);
        QVERIFY(!window.tabifiedDockWidgets(window.documentDock_).contains(window.propertiesDock_));
    }

    void propertiesDockCanShrinkToANarrowColumn() {
        MainWindow window;
        report("inspecteur min", window.propertiesDock_->minimumSizeHint());
        QVERIFY(window.propertiesDock_->minimumSizeHint().width() <= 260);
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_ui_responsive.moc"
