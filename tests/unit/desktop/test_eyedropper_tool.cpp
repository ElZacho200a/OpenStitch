// SPDX-License-Identifier: Apache-2.0
// Outil Pipette (HP-THR-012) : un clic échantillonne la couleur de l'image, propose les fils les
// plus proches et assigne le choix à la sélection par SetObjectThreadCommand (annulable).
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

#include "canvas_view.hpp"
#include "interaction_map.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    // Un objet de broderie rouge (200, 16, 46) sur une forme vectorielle carrée de 20 mm.
    static document::Project projectWithOneObject() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "Carre";
        geometry::Path sq;
        sq.closed = true;
        const auto node = [](std::int32_t x, std::int32_t y) {
            return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        sq.nodes = {node(-10'000, -10'000), node(10'000, -10'000), node(10'000, 10'000),
                    node(-10'000, 10'000)};
        vec.paths.push_back(geometry::PathSet{sq, {}});
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "Carre brode";
        emb.source_vector = vec.id;
        emb.rgb = {200, 16, 46};
        emb.params = document::RunningStitchParams{};
        project.embroidery_objects.push_back(emb);
        return project;
    }

    // Image de travail 10 x 10 px d'une couleur unie ; 1 mm par pixel => 10 mm de côté.
    static void setSolidImage(MainWindow& window, std::array<std::uint8_t, 3> rgb) {
        window.processed_.width = 10;
        window.processed_.height = 10;
        window.processed_.rgba.resize(10 * 10 * 4);
        for (std::size_t i = 0; i < 100; ++i) {
            window.processed_.rgba[i * 4] = rgb[0];
            window.processed_.rgba[i * 4 + 1] = rgb[1];
            window.processed_.rgba[i * 4 + 2] = rgb[2];
            window.processed_.rgba[i * 4 + 3] = 255;
        }
        window.project_.mm_per_px = Millimeters{1.0};
    }

    static QMenu* menuOf(MainWindow& window) {
        return window.findChild<QMenu*>(QStringLiteral("menu_eyedropper"));
    }

    // Première action activable dont le texte contient `needle`.
    static QAction* actionWith(QMenu* menu, const QString& needle) {
        for (QAction* a : menu->actions()) {
            if (a->text().contains(needle) && a->isEnabled()) {
                return a;
            }
        }
        return nullptr;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("EyedropperToolTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void toolIsInThePaletteAndRoutesClicksToTheEyedropper() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithOneObject());
        QVERIFY(window.toolEyedropperAct_ != nullptr);
        QVERIFY(window.toolPalette_->actions().contains(window.toolEyedropperAct_));
        window.toolEyedropperAct_->trigger();
        QCOMPARE(window.currentTool_, Tool::Eyedropper);
        QVERIFY(window.toolEyedropperAct_->isChecked());
        QCOMPARE(InteractionMap::contextFor(Tool::Eyedropper, false, false), Context::Eyedropper);
        QVERIFY(!InteractionMap::hintsFor(Context::Eyedropper, Qt::NoModifier).isEmpty());
        // Un clic ne sélectionne pas l'objet sous le curseur.
        setSolidImage(window, {200, 16, 46});
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QVERIFY(!window.selectedObject_.has_value());
    }

    void clickWithoutImageOrOutsideItOpensNoMenu() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithOneObject());
        window.processed_ = {};
        window.setTool(Tool::Eyedropper);
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QVERIFY(menuOf(window) == nullptr);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("image")));
        setSolidImage(window, {10, 20, 30});
        window.onCanvasClicked(QPointF(50.0, 0.0)); // hors de l'image de 10 mm
        QVERIFY(menuOf(window) == nullptr);
    }

    void clickSamplesTheImageAndOffersNearestThreads() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithOneObject());
        setSolidImage(window, {200, 16, 46});
        window.setTool(Tool::Eyedropper);
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QCOMPARE(window.eyedropperRgb_, (std::array<std::uint8_t, 3>{200, 16, 46}));
        QMenu* menu = menuOf(window);
        QVERIFY(menu != nullptr);
        QVERIFY(menu->actions().first()->text().contains(QStringLiteral("#C8102E")));
        // Jusqu'à 5 fils proposés, plus l'en-tête, la couleur libre et l'objet cliqué.
        int threadActions = 0;
        for (QAction* a : menu->actions()) {
            if (a->text().contains(QStringLiteral("écart"))) {
                ++threadActions;
            }
        }
        QCOMPARE(threadActions, 5);
        QVERIFY(actionWith(menu, QStringLiteral("Couleur libre")) == nullptr); // pas de cible
        menu->close();
    }

    void choosingAThreadAssignsItToTheSelectionAndIsUndoable() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithOneObject());
        setSolidImage(window, {200, 16, 46});
        const ObjectId embId = window.project_.embroidery_objects[0].id;
        window.selectedEmbroidery_ = embId;
        QVERIFY(!window.project_.embroidery_objects[0].thread.has_value());

        window.setTool(Tool::Eyedropper);
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QMenu* menu = menuOf(window);
        QVERIFY(menu != nullptr);
        QAction* pick = actionWith(menu, QStringLiteral("écart"));
        QVERIFY(pick != nullptr);
        pick->trigger();
        const auto* emb = window.project_.findEmbroidery(embId);
        QVERIFY(emb != nullptr);
        QVERIFY(emb->thread.has_value());
        QVERIFY(window.undoStack_.canUndo());

        window.findChild<QAction*>("action_undo")->trigger();
        QApplication::processEvents();
        QVERIFY(!window.project_.findEmbroidery(embId)->thread.has_value());
        QCOMPARE(window.project_.findEmbroidery(embId)->rgb,
                 (std::array<std::uint8_t, 3>{200, 16, 46}));
    }

    void freeColourAndRetakeObjectThreadUseTheExistingCommand() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithOneObject());
        setSolidImage(window, {0, 114, 206});
        const ObjectId embId = window.project_.embroidery_objects[0].id;
        window.selectedEmbroidery_ = embId;
        window.setTool(Tool::Eyedropper);
        // (0, 0) tombe sur la forme vectorielle : l'entrée « reprendre » est proposée.
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QMenu* menu = menuOf(window);
        QVERIFY(menu != nullptr);
        QAction* retake = actionWith(menu, QStringLiteral("Reprendre"));
        QVERIFY(retake != nullptr);
        QAction* freeColour = actionWith(menu, QStringLiteral("Couleur libre"));
        QVERIFY(freeColour != nullptr);
        freeColour->trigger();
        QCOMPARE(window.project_.findEmbroidery(embId)->rgb,
                 (std::array<std::uint8_t, 3>{0, 114, 206}));
        QVERIFY(!window.project_.findEmbroidery(embId)->thread.has_value());
        QVERIFY(window.undoStack_.canUndo());
    }

    void withoutSelectionTheMenuOffersNothingToAssign() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithOneObject());
        setSolidImage(window, {200, 16, 46});
        window.selectedEmbroidery_.reset();
        window.setTool(Tool::Eyedropper);
        window.onCanvasClicked(QPointF(1.0, 1.0));
        QMenu* menu = menuOf(window);
        QVERIFY(menu != nullptr);
        QVERIFY(actionWith(menu, QStringLiteral("écart")) == nullptr); // toutes grisées
        QVERIFY(!window.undoStack_.canUndo());
        menu->close();
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_eyedropper_tool.moc"
