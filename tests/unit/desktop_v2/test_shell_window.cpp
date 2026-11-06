// SPDX-License-Identifier: Apache-2.0
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

#include "action_dialog.hpp"
#include "calibration_dialog.hpp"
#include "calque_panel.hpp"
#include "chat_panel.hpp"
#include "command_palette.hpp"
#include "companion_panel.hpp"
#include "diagnostics_panel.hpp"
#include "digitize_pipeline.hpp"
#include "export_options_dialog.hpp"
#include "export_settings.hpp"
#include "fade_service.hpp"
#include "feedback_cue.hpp"
#include "floating_action_bar.hpp"
#include "import_dialog.hpp"
#include "inspector_model.hpp"
#include "inspector_panel.hpp"
#include "optimize_panel.hpp"
#include "param_spec.hpp"
#include "point_edit.hpp"
#include "preview_canvas.hpp"
#include "region_edit.hpp"
#include "region_edit_controller.hpp"
#include "region_panel.hpp"
#include "satin_column.hpp"
#include "shell_window.hpp"
#include "source_image_tools.hpp"
#include "spool_rail_widget.hpp"
#include "studio_theme.hpp"
#include "thread_palette_dialog.hpp"
#include "thread_panel.hpp"
#include "transform_batch.hpp"
#include <algorithm>
#include <array>

using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
fs::path tentabrode() {
    return fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
}
} // namespace

// Faux fournisseur : répond tout de suite, sans réseau ni clé. `ready` simule un fournisseur non
// configuré ; `hold` garde la demande ouverte jusqu'à `finish()` (pour tester l'annulation).
class FakeChatBackend final : public CompanionBackend {
    Q_OBJECT
public:
    bool ready{true};
    bool hold{false};
    QStringList received;
    int cancels{0};
    [[nodiscard]] QString providerName() const override { return QStringLiteral("Faux agent"); }
    [[nodiscard]] bool available() const override { return ready; }
    void finish() { emit finished(); }
public slots:
    void send(const QString& text) override {
        received << text;
        emit assistantText(QStringLiteral("Reçu : ") + text);
        CompanionToolCall call;
        call.tool = QStringLiteral("project_inspect");
        call.summary = QStringLiteral("1 objet");
        emit toolCalled(call);
        emit usage(120, 45);
        if (!hold) {
            emit finished();
        }
    }
    void cancel() override {
        ++cancels;
        emit failed(QStringLiteral("Demande annulée."));
        emit finished();
    }
    void decide(bool) override {}
};

class ShellWindowTest : public QObject {
    Q_OBJECT
private slots:
    void switchingWorkspacesKeepsSelectionHistoryFocusAndLetsYouFixARegionFromProduction() {
        ShellWindow w;
        w.resize(1100, 800);
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 4;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        const auto& objects = w.session().project().embroidery_objects;
        QVERIFY(objects.size() >= 2);
        const std::vector<Target> chosen{{TargetKind::EmbroideryObject, objects[0].id.value},
                                         {TargetKind::EmbroideryObject, objects[1].id.value}};
        w.session().selection().replace(chosen);
        w.canvas()->setFocus();
        const auto focus = w.focusWidget();
        const auto depth = w.session().undoNames().size();
        const auto revision = w.session().revision();
        // Un tour complet des espaces : rien ne se perd en route.
        for (const auto ws : {Workspace::Zones, Workspace::Digitize, Workspace::Optimize,
                              Workspace::Produce, Workspace::Design}) {
            w.setWorkspace(ws);
            QCOMPARE(w.session().selection().targets(), chosen);
            QCOMPARE(w.session().undoNames().size(), depth);
            QCOMPARE(w.session().revision(), revision);
            QCOMPARE(w.focusWidget(), focus);
        }
        // Depuis Production, on corrige une région puis on revient contrôler et exporter.
        w.setWorkspace(Workspace::Produce);
        QVERIFY(w.runCommand("tool.region.edit")); // bascule seule vers Zones, sans rien casser
        QCOMPARE(w.workspace(), Workspace::Zones);
        const auto regions = allRegionIds(w.session().project());
        QVERIFY(regions.size() >= 3);
        QVERIFY(w.regionController()->recolorSelected({1, 2, 3}) == false); // rien de choisi
        w.regionController()->selectRegions({regions.front()});
        QVERIFY(w.regionController()->recolorSelected({9, 8, 7}));
        w.setWorkspace(Workspace::Produce);
        QVERIFY(
            w.regionController()->dirty()); // l'état de correction survit au changement d'espace
        QVERIFY(w.session().undoNames().size() == depth + 1);
        // Le guide dit la vérité : les objets ne suivent plus la segmentation.
        QCOMPARE(w.guideNext(), NextAction::Regenerate);
        QVERIFY(w.guide().steps[1].state == StepState::Stale);
        w.setWorkspace(Workspace::Optimize);
        QVERIFY(w.runCommand("analysis.run"));
        w.waitForIdle();
        QVERIFY(w.session().undo()); // l'historique est intact et réversible
    }

    void zoomCommandsGuidesAndShortcutsAreReachable() {
        ShellWindow w;
        w.resize(1100, 800);
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 4;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        auto* canvas = w.canvas();
        canvas->fitAll();
        const double fitted = canvas->zoom();
        QVERIFY(w.runCommand("view.zoom-in"));
        QVERIFY(canvas->zoom() > fitted * 1.2);
        QVERIFY(w.runCommand("view.zoom-out"));
        QCOMPARE(canvas->zoom(), fitted);
        QVERIFY(canvas->zoomHistoryDepth() >= 2);
        QVERIFY(w.runCommand("view.zoom-in"));
        QVERIFY(w.runCommand("view.zoom-previous"));
        QCOMPARE(canvas->zoom(), fitted); // retour exact au zoom d'avant
        // Taille réelle : 1 mm du motif = 1 mm à l'écran.
        QVERIFY(w.runCommand("view.zoom-actual"));
        const double expected = (canvas->logicalDpiX() / 25.4) / w.previewFrame().px_per_mm;
        QVERIFY(qAbs(canvas->zoom() - expected) < 1e-9 * qMax(1.0, expected));
        // Raccourcis déclarés dans la palette.
        const auto entries = w.commandEntries();
        const auto shortcut = [&](const char* id) {
            for (const auto& e : entries) {
                if (e.id == id) {
                    return e.shortcut;
                }
            }
            return std::string("absent");
        };
        QCOMPARE(shortcut("view.zoom-in"), std::string("Ctrl+="));
        QCOMPARE(shortcut("view.zoom-actual"), std::string("Ctrl+0"));
        QVERIFY(w.findChild<QMenu*>("zoomMenu") != nullptr);
        // Guides : ajout, accrochage candidat, effacement.
        QCOMPARE(w.guideCount(), 0);
        QVERIFY(!w.runCommand("guide.clear")); // rien à effacer : indisponible
        w.addGuide(true, 12.5);
        w.addGuide(false, -4.0);
        w.addGuide(true, 12.5); // doublon ignoré
        QCOMPARE(w.guideCount(), 2);
        QVERIFY(canvas->hasGuides());
        QVERIFY(w.runCommand("guide.clear"));
        QCOMPARE(w.guideCount(), 0);
        QVERIFY(!canvas->hasGuides());
    }

    void regionSelectionIsMultipleSharedAndReachableByKeyboard() {
        ShellWindow w;
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 5;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.region.edit"));
        auto* controller = w.regionController();
        auto* panel = w.regionPanel();
        const auto all = allRegionIds(w.session().project());
        QVERIFY(all.size() >= 4);

        // Plusieurs régions dans le MODÈLE commun, la principale en dernier.
        controller->selectRegions({all[0], all[1], all[2]});
        QCOMPARE(controller->selection().size(), std::size_t{3});
        QCOMPARE(controller->selected(), all[2]);
        QCOMPARE(w.session().selection().targets().size(), std::size_t{3});
        for (const auto& t : w.session().selection().targets()) {
            QVERIFY(t.kind == TargetKind::Region);
        }
        QCOMPARE(panel->selectedCount(), 3);
        auto* list = panel->findChild<QListWidget*>("regionList");
        QCOMPARE(list->selectedItems().size(), 3);

        // Une sélection posée AILLEURS (modèle commun) est reprise par l'outil.
        w.session().selection().replace({{TargetKind::Region, all[3]}});
        QCOMPARE(controller->selection(), std::vector<std::uint64_t>{all[3]});
        w.session().selection().clear();
        QVERIFY(controller->selection().empty());

        // Boutons de choix rapide et commandes (palette / clavier).
        controller->selectRegion(all[0]);
        QVERIFY(w.runCommand("region.invert"));
        QCOMPARE(controller->selection().size(), all.size() - 1);
        QVERIFY(w.runCommand("selection.all")); // Ctrl+A avec l'outil Régions : les régions
        QCOMPARE(controller->selection().size(), all.size());
        QVERIFY(w.runCommand("selection.clear"));
        QVERIFY(controller->selection().empty());
        QVERIFY(!w.runCommand("region.merge")); // rien de choisi : refusée, sans effet

        // Fusion de l'ensemble : une étape d'annulation, régénération proposée.
        const auto depth = w.session().undoNames().size();
        controller->selectRegions({all[1], all[2]});
        QVERIFY(w.runCommand("region.merge"));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(allRegionIds(w.session().project()).size(), all.size() - 1);
        QVERIFY(w.session().undo());
        QCOMPARE(allRegionIds(w.session().project()).size(), all.size());

        // Suppr retire les RÉGIONS choisies (pas les objets) ; Échap désélectionne.
        const auto objectsBefore = w.session().project().embroidery_objects.size();
        controller->selectRegions({all[2]});
        QVERIFY(w.runCommand("edit.delete"));
        QCOMPARE(w.session().project().embroidery_objects.size(), objectsBefore);
        QCOMPARE(allRegionIds(w.session().project()).size(), all.size() - 1);
        QVERIFY(w.session().undo());
        controller->selectRegions({all[0]});
        QTest::keyClick(&w, Qt::Key_Escape);
        QVERIFY(controller->selection().empty());

        // Lien région -> objets -> région.
        controller->selectRegions({all[0]});
        QVERIFY(w.runCommand("region.objects"));
        QVERIFY(!w.session().selection().empty());
        QVERIFY(w.session().selection().targets().front().kind == TargetKind::EmbroideryObject);
        QVERIFY(controller->selection().empty()); // la sélection est unique
        QVERIFY(w.runCommand("region.from-objects"));
        QVERIFY(!controller->selection().empty());
        QVERIFY(w.session().selection().targets().front().kind == TargetKind::Region);
    }

    void regionClicksUseCtrlAndShiftToExtendTheSelection() {
        ShellWindow w;
        w.resize(1200, 800);
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 5;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.region.edit"));
        auto* controller = w.regionController();
        const auto& f = w.previewFrame();
        const auto& project = w.session().project();
        const double mmPerPx = project.mm_per_px.value;
        const double wMm = project.segmentation->width * mmPerPx;
        const double hMm = project.segmentation->height * mmPerPx;
        // Un pixel de scène par région (centre de son premier pixel trouvé).
        std::map<std::uint64_t, QPointF> spot;
        for (int y = 0; y < project.segmentation->height; y += 3) {
            for (int x = 0; x < project.segmentation->width; x += 3) {
                if (const auto id = regionAtPixel(project, x, y); id && !spot.count(*id)) {
                    spot[*id] = QPointF(f.mmToPxX((x + 0.5) * mmPerPx - wMm / 2.0),
                                        f.mmToPxY((y + 0.5) * mmPerPx - hMm / 2.0));
                }
            }
        }
        QVERIFY(spot.size() >= 3);
        auto it = spot.begin();
        const auto a = *it++;
        const auto b = *it++;
        const auto c = *it++;
        QVERIFY(controller->onCanvasClick(a.second));
        QCOMPARE(controller->selection(), std::vector<std::uint64_t>{a.first});
        QVERIFY(controller->onCanvasClick(b.second, Qt::ControlModifier)); // ajoute
        QCOMPARE(controller->selection().size(), std::size_t{2});
        QVERIFY(controller->onCanvasClick(c.second, Qt::ShiftModifier)); // ajoute
        QCOMPARE(controller->selection().size(), std::size_t{3});
        QCOMPARE(controller->selected(), c.first);
        QVERIFY(controller->onCanvasClick(b.second, Qt::ControlModifier)); // bascule : retire
        QCOMPARE(controller->selection(), (std::vector<std::uint64_t>{a.first, c.first}));
        QVERIFY(controller->onCanvasClick(a.second)); // sans modificateur : remplace
        QCOMPARE(controller->selection(), std::vector<std::uint64_t>{a.first});
    }

    void renderQualityIsAdjustableSavedAndReachable() {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "os_v2_render_quality";
        fs::remove_all(dir);
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            w.show();
            w.newProject();
            QCOMPARE(w.renderSettings(), RenderSettings{});
            // Menu Affichage > Qualité du rendu réaliste : un préréglage par entrée.
            auto* menu = w.findChild<QMenu*>("renderMenu");
            QVERIFY(menu != nullptr);
            auto* photo = w.findChild<QAction*>("renderPreset_photo");
            QVERIFY(photo != nullptr);
            photo->trigger();
            QCOMPARE(w.renderSettings(), *renderPreset("photo"));
            QVERIFY(photo->isChecked());
            // Palette et raccourci F4 : fenêtre de réglages détaillés.
            bool listed = false;
            for (const auto& e : w.commandEntries()) {
                if (e.id == "view.render") {
                    listed = true;
                    QCOMPARE(e.shortcut, std::string("F4"));
                }
            }
            QVERIFY(listed);
            QVERIFY(w.runCommand("view.render"));
            auto* dialog = w.findChild<QDialog*>("renderQualityDialog");
            QVERIFY(dialog != nullptr && dialog->isVisible());
            auto* resolution = dialog->findChild<QDoubleSpinBox*>("renderResolution");
            QVERIFY(resolution != nullptr);
            QCOMPARE(resolution->value(), 10.0);
            resolution->setValue(4.5); // un réglage détaillé devient « personnalisé »
            QCOMPARE(w.renderSettings().pxPerMm, 4.5);
            QVERIFY(matchingRenderPreset(w.renderSettings()).empty());
            QVERIFY(w.runCommand("render.preset.draft"));
            QCOMPARE(w.renderSettings(), *renderPreset("draft"));
            QCOMPARE(resolution->value(), 1.5); // la fenêtre suit
            QVERIFY(w.runCommand("render.preset.high"));
            w.waitForIdle();
        }
        {
            ShellWindow reopened;
            reopened.setStateDirectory(dir);
            QCOMPARE(reopened.renderSettings(), *renderPreset("high"));
            QVERIFY(reopened.findChild<QAction*>("renderPreset_high")->isChecked());
        }
        fs::remove_all(dir);
    }

    void eachWorkspaceShowsOnlyItsOwnPanelsAndKeepsSelectionAndHistory() {
        ShellWindow w;
        w.show();
        w.newProject();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "R").report);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto visible = [&](const char* name) {
            auto* d = w.findChild<QDockWidget*>(name);
            return d != nullptr && d->isVisible();
        };
        w.setWorkspace(Workspace::Zones);
        QVERIFY(visible("regionDock") && visible("documentDock"));
        QVERIFY(!visible("calqueDock") && !visible("optimizeDock") && !visible("threadDock"));
        w.setWorkspace(Workspace::Design);
        QVERIFY(visible("calqueDock") && visible("documentDock"));
        QVERIFY(!visible("regionDock") && !visible("optimizeDock") && !visible("diagnosticDock"));
        w.setWorkspace(Workspace::Optimize);
        QVERIFY(visible("optimizeDock") && visible("diagnosticDock"));
        QVERIFY(!visible("regionDock") && !visible("calqueDock"));
        // Aller-retour : rien ne se perd.
        QCOMPARE(w.session().selection().targets().size(), std::size_t{1});
        QVERIFY(w.session().canUndo());
        // Affichage › Panneaux rouvre un panneau d'un autre espace sans changer d'espace.
        auto* panels = w.findChild<QMenu*>("panelsMenu");
        QVERIFY(panels != nullptr);
        QVERIFY(panels->actions().size() >= 6);
        w.findChild<QDockWidget*>("regionDock")->toggleViewAction()->trigger();
        QVERIFY(visible("regionDock"));
        QCOMPARE(w.workspace(), Workspace::Optimize);
    }

    void zonesAndShapesAreSeparateWorkspacesWithTheirOwnTools() {
        ShellWindow w;
        const auto& r = w.tools();
        const auto has = [&](Workspace ws, const char* id) {
            for (const auto* t : r.forWorkspace(ws)) {
                if (t->id == id) {
                    return true;
                }
            }
            return false;
        };
        // Zones : segmentation. Formes : vecteur. Aucun outil de région dans les Formes.
        for (const char* id : {"region.edit", "region.brush", "detail-brush", "bucket"}) {
            QVERIFY2(has(Workspace::Zones, id), id);
            QVERIFY2(!has(Workspace::Design, id), id);
        }
        for (const char* id : {"node.edit", "draw.bezier", "draw.freeform", "draw.rectangle"}) {
            QVERIFY2(has(Workspace::Design, id), id);
            QVERIFY2(!has(Workspace::Zones, id), id);
        }
        QVERIFY(w.runCommand("workspace.zones"));
        QCOMPARE(w.workspace(), Workspace::Zones);
        QVERIFY(w.runCommand("tool.region.edit"));
        QCOMPARE(w.workspace(), Workspace::Zones);
    }

    void everyCommandShortcutIsUniqueAndWorkspacesUseCtrlOneToFive() {
        ShellWindow w;
        std::map<std::string, std::string> seen;
        for (const auto& e : w.commandEntries()) {
            if (e.shortcut.empty() || e.id.rfind("tool.", 0) == 0) {
                continue; // les outils ont leurs touches seules, vérifiées par le registre
            }
            const auto [it, fresh] = seen.emplace(e.shortcut, e.id);
            QVERIFY2(fresh, (e.shortcut + " : " + it->second + " et " + e.id).c_str());
        }
        QVERIFY(seen.count("Ctrl+5") == 1 && seen["Ctrl+5"] == "workspace.produce");
        QVERIFY(seen.count("F5") == 1 && seen.count("Ctrl+Shift+U") == 1);
    }

    void chatPanelTalksToTheProviderAndShowsToolsErrorsAndUsage() {
        ShellWindow w;
        w.show();
        w.newProject();
        FakeChatBackend backend;
        w.setChatBackend(&backend);
        auto* chat = w.chatPanel();
        QVERIFY(chat != nullptr);
        QVERIFY(chat->statusText().contains("Faux agent"));
        QVERIFY(!chat->submit("   ")); // vide : refusé
        QVERIFY(chat->submit("Bonjour"));
        QCOMPARE(backend.received, QStringList{"Bonjour"});
        QVERIFY(!chat->busy()); // le faux backend a terminé
        QVERIFY(chat->messageText(0).contains("Bonjour"));
        QVERIFY(chat->messageText(1).contains("Reçu : Bonjour"));
        QVERIFY(chat->messageText(2).contains("project_inspect"));
        QVERIFY(chat->usageText().contains("120"));
        // Annulation d'une demande en cours.
        backend.hold = true;
        QVERIFY(chat->submit("Longue demande"));
        QVERIFY(chat->busy());
        QVERIFY(!chat->submit("Deuxième")); // occupé : refusé
        const int before = chat->messageCount();
        backend.cancel();
        QVERIFY(!chat->busy());
        QVERIFY(chat->messageCount() > before);
        QVERIFY(chat->messageText(chat->messageCount() - 1).contains("annulée"));
        // Document remplacé : la conversation repart de zéro. (Pas de `newProject()` ici : le
        // document est « modifié » et la fenêtre demanderait « Enregistrer ? » dans une boîte
        // modale que personne ne fermerait.)
        auto fresh = openstitch::project_ops::create_empty_project({});
        QVERIFY(fresh.has_value());
        w.session().replaceProject(std::move(*fresh), true);
        QCOMPARE(chat->messageCount(), 0);
        w.setChatBackend(nullptr);
    }

    void everyCommandOfTheFinalisationIsListedInThePalette() {
        // Garde : une commande câblée dans `runCommand` mais absente de `commandEntries` est
        // inatteignable (le test du chat l'a révélé : l'insertion visait une ligne reformatée).
        ShellWindow w;
        w.show();
        w.newProject();
        const auto entries = w.commandEntries();
        for (const char* id :
             {"satin.rung.insert", "satin.rung.remove", "satin.diagnose", "analysis.pull",
              "analysis.calibration", "export.options", "export.check", "thread.palette",
              "text.edit", "text.glyph", "text.glyph.reset", "chat.open", "companion.settings",
              "tool.region.brush", "node.smooth", "node.corner", "node.hole.remove",
              "text.remove"}) {
            const bool listed = std::any_of(entries.begin(), entries.end(),
                                            [id](const auto& e) { return e.id == id; });
            QVERIFY2(listed, id);
        }
    }

    void chatOpenCommandShowsTheDockWithTheRealProvider() {
        ShellWindow w;
        w.show();
        QVERIFY(!w.runCommand("chat.open")); // aucun document ouvert
        const auto entries = w.commandEntries();
        const auto listed = [&entries](const char* id) {
            return std::any_of(entries.begin(), entries.end(),
                               [id](const auto& e) { return e.id == id; });
        };
        QVERIFY(listed("chat.open"));
        QVERIFY(listed("companion.settings"));
        w.newProject();
        QVERIFY(w.runCommand("chat.open"));
        auto* dock = w.findChild<QDockWidget*>("chatDock");
        QVERIFY(dock != nullptr);
        QVERIFY(dock->isVisible());
        QVERIFY(!w.chatPanel()->statusText().isEmpty());
        QVERIFY(w.chatPanel()->messageCount() == 0);
    }

    void chatWithoutAConfiguredProviderExplainsAndRefusesToSend() {
        ShellWindow w;
        w.show();
        w.newProject();
        FakeChatBackend backend;
        backend.ready = false;
        w.setChatBackend(&backend);
        auto* chat = w.chatPanel();
        QVERIFY(chat->statusText().contains("pas prêt"));
        QVERIFY(!chat->submit("Bonjour"));
        QVERIFY(backend.received.isEmpty());
        // Le signal `configureRequested` de CE panneau ouvre la boîte de réglages (modale) : on ne
        // l'émet pas ici, elle bloquerait le test. Un panneau isolé, sans fenêtre, le vérifie.
        {
            ChatPanel alone;
            bool asked = false;
            QObject::connect(&alone, &ChatPanel::configureRequested, [&asked] { asked = true; });
            emit alone.configureRequested();
            QVERIFY(asked);
        }
        w.setChatBackend(nullptr);
        QVERIFY(chat->statusText().contains("Aucun fournisseur"));
    }

    void successfulEditsConfirmWithoutChangingDocumentHistory() {
        ShellWindow w;
        w.newProject();
        auto* cue = w.findChild<FeedbackCue*>("feedbackCue");
        QVERIFY(cue != nullptr);
        const auto revision = w.session().revision();
        const auto result = w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle");
        QVERIFY(result.committed);
        QCOMPARE(w.session().revision(), revision + 1);
        QCOMPARE(cue->text(), QStringLiteral("✓"));
        QVERIFY(!cue->toolTip().isEmpty());
        cue->setReducedMotion(true);
        QVERIFY(!cue->animating());
        QVERIFY(w.session().canUndo());
        QVERIFY(w.session().undo());
        QVERIFY(!cue->animating());
    }

    void selectionFeedbackIsTransientAndNeverChangesTheProject() {
        ShellWindow w;
        w.newProject();
        const auto created = w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle");
        const auto id = *createdObjectId(created.report);
        w.waitForIdle();
        const auto revision = w.session().revision();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QCOMPARE(w.canvas()->selectionBoxCount(), 1);
        QVERIFY(w.canvas()->selectionFeedbackActive());
        auto* reduce = w.findChild<QAction*>("reduceMotion");
        QVERIFY(reduce != nullptr);
        reduce->trigger();
        QVERIFY(w.canvas()->reducedMotion());
        QVERIFY(!w.canvas()->selectionFeedbackActive());
        QCOMPARE(w.session().revision(), revision);
        w.session().selection().clear();
        QCOMPARE(w.canvas()->selectionBoxCount(), 0);
        w.canvas()->clearImage();
        QVERIFY(!w.canvas()->selectionFeedbackActive());
    }

    void reducedMotionPreferenceIsReusedOnRelaunch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = fs::path(dir.path().toStdU16String());
        {
            ShellWindow w;
            w.setStateDirectory(path);
            auto* action = w.findChild<QAction*>("reduceMotion");
            auto* cue = w.findChild<FeedbackCue*>("feedbackCue");
            QVERIFY(action != nullptr);
            QVERIFY(cue != nullptr);
            QVERIFY(!cue->reducedMotion());
            action->trigger();
            QVERIFY(action->isChecked());
            QVERIFY(cue->reducedMotion());
            cue->pulse();
            QVERIFY(!cue->animating());
        }
        ShellWindow reopened;
        reopened.setStateDirectory(path);
        QVERIFY(reopened.findChild<FeedbackCue*>()->reducedMotion());
        QVERIFY(reopened.findChild<QAction*>("reduceMotion")->isChecked());
    }

    void studioThemeUsesAccessibleFocusAndPreservesModeSwitching() {
        const auto dark = studioColors(StudioScheme::Dark);
        const auto light = studioColors(StudioScheme::Light);
        QVERIFY(dark.background != light.background);
        QVERIFY(dark.accent != light.accent);
        QVERIFY(studioStyleSheet(StudioScheme::Dark).contains(dark.accent));
        QVERIFY(studioStyleSheet(StudioScheme::Light).contains(light.accent));
        QVERIFY(studioStyleSheet(StudioScheme::Dark).contains("QToolButton:focus"));
        ShellWindow w;
        w.setThemeMode("dark");
        QCOMPARE(w.themeMode(), QString("dark"));
        QVERIFY(qApp->styleSheet().contains(dark.background));
        w.setThemeMode("light");
        QVERIFY(qApp->styleSheet().contains(light.background));
        w.setThemeMode("system");
        QCOMPARE(w.themeMode(), QString("system"));
    }

    void emptyShellHasNoObjectsAndNoPreview() {
        ShellWindow w;
        QVERIFY(w.session().project().embroidery_objects.empty());
        QVERIFY(!w.previewShown());
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void workspacesFilterTheToolRailButKeepOneSelectedTool() {
        ShellWindow w;
        auto* rail = w.findChild<QToolBar*>("toolRail");
        QVERIFY(rail);
        const auto design = rail->actions().size();
        w.setWorkspace(Workspace::Produce);
        QVERIFY(rail->actions().size() < design);
        QCOMPARE(w.activeToolId(), QString("select"));
        w.setWorkspace(Workspace::Design);
        QCOMPARE(rail->actions().size(), design);
    }

    void reselectingCurrentWorkspacePreservesActiveToolAndRailActions() {
        ShellWindow w;
        auto* rail = w.findChild<QToolBar*>(QStringLiteral("toolRail"));
        auto* status = w.findChild<QLabel*>(QStringLiteral("activeToolStatus"));
        QVERIFY(rail != nullptr);
        QVERIFY(status != nullptr);
        QVERIFY(w.runCommand(QStringLiteral("tool.region.edit")));
        QCOMPARE(w.activeToolId(), QStringLiteral("region.edit"));
        const auto actionsBefore = rail->actions();
        QVERIFY(!actionsBefore.isEmpty());
        const QString statusBefore = status->text();

        // Recliquer sur l'espace courant (Zones, où vit l'outil) n'est pas un changement de
        // contexte.
        w.setWorkspace(Workspace::Zones);
        QCOMPARE(w.activeToolId(), QStringLiteral("region.edit"));
        QCOMPARE(status->text(), statusBefore);
        QCOMPARE(rail->actions(), actionsBefore);
    }

    void switchingWorkspaceReturnsFocusToCanvasWhenRailButtonIsReplaced() {
        ShellWindow w;
        // The empty shell displays the welcome page, not the canvas. A hidden
        // widget cannot receive keyboard focus, so exercise the editor state.
        w.newProject();
        w.show();
        QCoreApplication::processEvents();
        auto* rail = w.findChild<QToolBar*>(QStringLiteral("toolRail"));
        auto* canvas = w.findChild<QWidget*>(QStringLiteral("previewCanvas"));
        QVERIFY(rail != nullptr);
        QVERIFY(canvas != nullptr);
        QVERIFY(canvas->isVisible());
        QVERIFY(!rail->actions().isEmpty());
        auto* button = rail->widgetForAction(rail->actions().first());
        QVERIFY(button != nullptr);
        button->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(button->hasFocus());

        w.setWorkspace(Workspace::Produce);
        QTRY_VERIFY(canvas->hasFocus());
        QCOMPARE(w.activeToolId(), QStringLiteral("select"));
    }

    void switchingWorkspaceResetsTheActiveToolStatusAlongWithSelection() {
        ShellWindow w;
        auto* status = w.findChild<QLabel*>(QStringLiteral("activeToolStatus"));
        QVERIFY(status != nullptr);
        QVERIFY(w.runCommand("tool.region.edit"));
        QCOMPARE(w.activeToolId(), QStringLiteral("region.edit"));
        QVERIFY(status->text().contains(QStringLiteral("Régions")));

        w.setWorkspace(Workspace::Produce);
        QCOMPARE(w.activeToolId(), QStringLiteral("select"));
        QCOMPARE(status->text(), QStringLiteral("Sélection"));
    }

    void allToolsMenuKeepsLowRailToolsAccessibleWithoutEditingTheDocument() {
        ShellWindow w;
        w.resize(1024, 520);
        w.show();
        auto* chooser = w.findChild<QToolButton*>(QStringLiteral("allToolsButton"));
        auto* menu = w.findChild<QMenu*>(QStringLiteral("allToolsMenu"));
        QVERIFY(chooser != nullptr);
        QVERIFY(menu != nullptr);
        QVERIFY(chooser->isVisibleTo(&w));
        QCOMPARE(chooser->menu(), menu);
        const auto globalToolCount = menu->actions().size();
        QVERIFY(globalToolCount >=
                static_cast<qsizetype>(w.tools().forWorkspace(Workspace::Design).size()));
        const auto revision = w.session().revision();

        const auto findTool = [menu](const QString& id) -> QAction* {
            for (auto* action : menu->actions()) {
                if (action->data().toString() == id) {
                    return action;
                }
            }
            return nullptr;
        };
        auto* regions = findTool(QStringLiteral("region.edit"));
        QVERIFY(regions != nullptr);
        QVERIFY(regions->isEnabled());
        QVERIFY(regions->text().contains(QStringLiteral("Régions")));
        regions->trigger();
        QCOMPARE(w.activeToolId(), QStringLiteral("region.edit"));
        QCOMPARE(w.session().revision(), revision);

        auto* unavailable = findTool(QStringLiteral("bucket"));
        QVERIFY(unavailable != nullptr);
        QVERIFY(!unavailable->isEnabled());
        QVERIFY(unavailable->toolTip().contains(QStringLiteral("pas encore migré")));

        w.setWorkspace(Workspace::Produce);
        QCOMPARE(menu->actions().size(), globalToolCount);
        auto* crossWorkspaceRegion = findTool(QStringLiteral("region.edit"));
        QVERIFY(crossWorkspaceRegion != nullptr);
        QVERIFY(crossWorkspaceRegion->isEnabled());
        QCOMPARE(w.activeToolId(), QStringLiteral("select"));
        crossWorkspaceRegion->trigger();
        QCOMPARE(w.workspace(), Workspace::Zones);
        QCOMPARE(w.activeToolId(), QStringLiteral("region.edit"));
        QCOMPARE(w.session().revision(), revision);
    }

    void toolRailUsesDescriptiveNamesAndKeepsAllToolsInBothModes() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        ShellWindow w;
        w.setStateDirectory(fs::path(home.path().toStdU16String()) / "isolated-ui-state");
        auto* rail = w.findChild<QToolBar*>("toolRail");
        auto* toggle = w.findChild<QAction*>("action_view_compact_tools");
        auto* status = w.findChild<QLabel*>("activeToolStatus");
        QVERIFY(rail && toggle && status);
        // A fresh workspace starts canvas-first; expanded labels remain available on demand.
        QVERIFY(w.compactToolRail());
        QVERIFY(toggle->isChecked());
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonIconOnly);
        const auto count = rail->actions().size();
        QVERIFY(count > 5);
        for (auto* action : rail->actions()) {
            QVERIFY(!action->icon().isNull());
            QVERIFY(action->text().length() > 1);
            QVERIFY(!action->toolTip().isEmpty());
        }
        QCOMPARE(status->text(), QStringLiteral("Sélection"));
        toggle->setChecked(true);
        QVERIFY(w.compactToolRail());
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(rail->actions().size(), count);
        w.setWorkspace(Workspace::Digitize);
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(w.activeToolId(), QStringLiteral("select"));
        toggle->setChecked(false);
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    }

    void newWorkspaceDefaultsToCanvasFirstCompactRail() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const auto dir = fs::path(home.path().toStdU16String()) / "fresh-ui-state";
        ShellWindow w;
        w.setStateDirectory(dir);
        auto* rail = w.findChild<QToolBar*>("toolRail");
        auto* toggle = w.findChild<QAction*>("action_view_compact_tools");
        QVERIFY(rail != nullptr);
        QVERIFY(toggle != nullptr);
        QVERIFY(w.compactToolRail());
        QVERIFY(toggle->isChecked());
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(rail->width(), 52);
        w.setWorkspace(Workspace::Produce);
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonIconOnly);
        w.setCompactToolRail(false);
        QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    }

    void compactRailPreferenceSurvivesRestartWithoutChangingShortcuts() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const auto dir = fs::path(home.path().toStdU16String()) / "ui-state";
        QVERIFY(!fs::exists(dir));
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            w.setCompactToolRail(true);
            auto* rail = w.findChild<QToolBar*>("toolRail");
            QVERIFY(rail);
            QVERIFY(!rail->actions().isEmpty());
            QCOMPARE(rail->actions().front()->shortcut(), QKeySequence("V"));
        }
        QVERIFY(fs::exists(dir / "ui.ini"));
        {
            ShellWindow reopened;
            reopened.setStateDirectory(dir);
            QVERIFY(reopened.compactToolRail());
            auto* rail = reopened.findChild<QToolBar*>("toolRail");
            QCOMPARE(rail->toolButtonStyle(), Qt::ToolButtonIconOnly);
        }
    }

    void unmigratedToolsAreVisibleButDisabledWithAReason() {
        ShellWindow w;
        auto* rail = w.findChild<QToolBar*>("toolRail");
        int enabled = 0;
        int disabled = 0;
        for (auto* a : rail->actions()) {
            if (a->isEnabled()) {
                ++enabled;
            } else {
                ++disabled;
                QVERIFY(a->toolTip().contains("pas encore migré"));
            }
        }
        QCOMPARE(enabled,
                 12); // Formes : sans les outils de Zones (Régions, Pinceau de frontière, Détail)
        QVERIFY(disabled > 0);
    }

    void modifiedDocumentCanCancelNewProjectAndClose() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle").committed);
        const auto revision = w.session().revision();
        QVERIFY(w.session().dirty());
        QVERIFY(w.isWindowModified());

        int dialogs = 0;
        const auto cancelPrompt = [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                ++dialogs;
                dialog->done(QMessageBox::Cancel);
            }
        };
        QTimer::singleShot(0, &w, cancelPrompt);
        w.newProject();
        QCOMPARE(w.session().revision(), revision);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));

        QTimer::singleShot(0, &w, cancelPrompt);
        QVERIFY(!w.close());
        QVERIFY(w.isVisible());
        QCOMPARE(dialogs, 2);

        QTimer::singleShot(0, &w, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                dialog->button(QMessageBox::Discard)->click();
            }
        });
        QVERIFY(w.close());
    }

    void discardReplacesTheDocumentAndKeepsUndoConsistent() {
        ShellWindow w;
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle").committed);
        QTimer::singleShot(0, &w, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                dialog->button(QMessageBox::Discard)->click();
            }
        });
        w.newProject();
        QVERIFY(w.session().project().embroidery_objects.empty());
        QVERIFY(!w.session().canUndo());
        QVERIFY(w.session().dirty());
    }

    void laterImageOpenWinsOverAnEarlierRequest() {
        ShellWindow w;
        w.openImage(tentabrode(), 300.0);
        w.openImage(tentabrode(), 120.0);
        w.waitForIdle();
        QVERIFY(!w.session().project().embroidery_objects.empty());
        const double height = w.session().project().canvas.height.value / 1000.0;
        QVERIFY2(height < 150.0, "Le résultat d'un ancien chargement a écrasé le dernier.");
    }

    void imageToDstEndToEnd() {
        ShellWindow w;
        w.openImage(tentabrode(), 120.0);
        w.waitForIdle();
        QVERIFY2(!w.session().project().embroidery_objects.empty(), qPrintable(w.lastNotice()));
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        QCOMPARE(static_cast<size_t>(tree->topLevelItemCount()),
                 w.session().project().embroidery_objects.size());
        QVERIFY(w.previewShown());
        QVERIFY(w.session().dirty());

        const auto dst = fs::temp_directory_path() / "openstitch_v2_shell.dst";
        fs::remove(dst);
        w.exportDst(dst);
        w.waitForIdle();
        QVERIFY2(fs::exists(dst), qPrintable(w.lastNotice()));
        QVERIFY(fs::file_size(dst) > 512);
        QVERIFY(w.lastNotice().contains("DST"));
        fs::remove(dst);
    }

    void treeSelectionAndModelStayInSync() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        QVERIFY(tree->topLevelItemCount() >= 2);
        tree->topLevelItem(0)->setSelected(true);
        tree->topLevelItem(1)->setSelected(true);
        QCOMPARE(w.session().selection().targets().size(), size_t(2));
        // Sens inverse : le modèle pilote l'arbre (canevas, compagnon IA, annulation…).
        const auto first = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, first}});
        QCOMPARE(tree->selectedItems().size(), 1);
        QCOMPARE(tree->selectedItems().front()->data(0, Qt::UserRole + 1).toULongLong(),
                 static_cast<qulonglong>(first));
    }

    void renamingFromTheTreeIsOneUndoStep() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        const std::string before = w.session().project().embroidery_objects.front().name;
        tree->topLevelItem(0)->setText(0, "Ciel \"V2\"");
        QCOMPARE(QString::fromStdString(w.session().project().embroidery_objects.front().name),
                 QString("Ciel \"V2\""));
        QVERIFY(w.session().canUndo());
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.front().name, before);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString::fromStdString(before));
    }

    void hidingFromTheTreeGoesThroughTheBatchPathAndUndoes() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* tree = w.findChild<QTreeWidget*>("objectTree");
        tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QVERIFY(!w.session().project().embroidery_objects.front().visible);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Masquer"));
        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().embroidery_objects.front().visible);
    }

    void projectSaveThenReopenRoundTrips() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto count = w.session().project().embroidery_objects.size();
        const auto name = w.session().project().embroidery_objects.front().name;
        const auto osp = fs::temp_directory_path() / "openstitch_v2_roundtrip.osp";
        fs::remove(osp);
        QVERIFY(w.saveProject(osp));
        QVERIFY(!w.session().dirty()); // enregistré : l'historique est « propre »

        ShellWindow reopened;
        reopened.openProject(osp);
        reopened.waitForIdle();
        QCOMPARE(reopened.session().project().embroidery_objects.size(), count);
        QCOMPARE(reopened.session().project().embroidery_objects.front().name, name);
        QVERIFY(!reopened.session().dirty());
        QVERIFY(reopened.previewShown());
        fs::remove(osp);
    }

    void failedImportShowsAnErrorNoticeAndKeepsTheDocument() {
        ShellWindow w;
        w.openImage(fs::path("missing/nothing.png"), 0.0);
        w.waitForIdle();
        QVERIFY(w.session().project().embroidery_objects.empty());
        QVERIFY(w.lastNotice().contains("impossible"));
    }

    void cancellingNeverLeavesAHalfInstalledDocument() {
        ShellWindow w;
        w.openImage(tentabrode(), 300.0);
        w.tasks().cancelAll();
        w.waitForIdle();
        // Soit annulé avant la fin (document vierge), soit terminé avant l'annulation (document
        // complet) : aucun état intermédiaire n'est observable.
        const bool pristine = w.session().project().embroidery_objects.empty();
        const bool complete = !pristine;
        QVERIFY(pristine || complete);
        if (pristine) {
            QVERIFY(!w.session().dirty());
        }
    }

    void clickingTheCanvasSelectsTheObjectUnderTheCursor() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas && canvas->hasImage());
        // Un point appartenant à un objet (milieu de son dernier brin).
        std::optional<QPointF> point;
        std::uint64_t id = 0;
        for (const auto& o : w.session().project().embroidery_objects) {
            if ((point = w.objectCentrePx(o.id.value))) {
                id = o.id.value;
                break;
            }
        }
        QVERIFY(point.has_value());
        const QPoint viewPos = canvas->mapFromScene(*point);
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
        QCOMPARE(canvas->selectionBoxCount(), 1);
        (void)id;

        // Ctrl+clic au même endroit : bascule (désélectionne).
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, viewPos);
        QVERIFY(w.session().selection().empty());
        QCOMPARE(canvas->selectionBoxCount(), 0);

        // Clic dans le vide (coin de l'image) : sélection vide.
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QVERIFY(!w.session().selection().empty());
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                          canvas->mapFromScene(QPointF(1, 1)));
        QVERIFY(w.session().selection().empty());
    }

    void aDragOnEmptyFabricIsARubberBandNotAClick() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        // Coin du tissu (hors de tout fil) : un glisser y trace un cadre, il ne déplace rien.
        const QPoint viewPos = canvas->mapFromScene(QPointF(2, 2));
        const auto depth = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, viewPos);
        QTest::mouseMove(canvas->viewport(), viewPos + QPoint(30, 30));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            viewPos + QPoint(30, 30));
        QVERIFY(w.session().selection().empty()); // un glisser n'est pas un clic
        QCOMPARE(w.session().undoNames().size(), depth);
    }

    void rubberBandSelectsWholeObjectsOnly() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas->hasImage());
        const QRectF image = canvas->sceneRect().adjusted(40, 40, -40, -40);
        // Cadre autour de toute l'image : tous les objets visibles sont dedans.
        const QPoint from = canvas->mapFromScene(image.topLeft() + QPointF(-5, -5));
        const QPoint to = canvas->mapFromScene(image.bottomRight() + QPointF(5, 5));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(20, 20));
        QTest::mouseMove(canvas->viewport(), to);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        // Un objet sans aucun point cousu n'a pas d'emprise : il n'est pas sélectionnable au
        // canevas.
        size_t stitched = 0;
        for (const auto& o : w.session().project().embroidery_objects) {
            stitched += w.objectCentrePx(o.id.value).has_value() ? 1 : 0;
        }
        QVERIFY(stitched > 0);
        QCOMPARE(w.session().selection().targets().size(), stitched);

        // Petit cadre au milieu : aucun objet entier (les remplissages dépassent) -> sélection
        // vide.
        const QPoint c = canvas->mapFromScene(QPointF(2, 2)); // tissu vide
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(canvas->viewport(), c + QPoint(10, 10));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, c + QPoint(10, 10));
        QVERIFY(w.session().selection().empty());
    }

    void panToolDragsTheViewInsteadOfARubberBand() {
        ShellWindow w;
        QVERIFY(w.runCommand("tool.pan"));
        QCOMPARE(w.activeToolId(), QString("pan"));
        QCOMPARE(w.canvas()->dragMode(), QGraphicsView::ScrollHandDrag);
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Pan);
        QVERIFY(w.runCommand("tool.select"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Select);
    }

    void commandsCoverToolsWorkspacesAndEditing() {
        ShellWindow w;
        const auto entries = w.commandEntries();
        const auto has = [&](const char* id) {
            for (const auto& e : entries) {
                if (e.id == id) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(has("file.open") && has("edit.undo") && has("workspace.produce"));
        // Les 25 outils du registre sont tous atteignables par la palette.
        int tools = 0;
        for (const auto& e : entries) {
            tools += e.id.rfind("tool.", 0) == 0 ? 1 : 0;
        }
        QCOMPARE(tools, 25);
        // Un outil non migré est listé mais refusé, sans effet de bord.
        QVERIFY(!w.runCommand("tool.bucket"));
        QCOMPARE(w.activeToolId(), QString("select"));
        QVERIFY(!w.runCommand("nope"));
        // Sans document, enregistrer / exporter / annuler sont désactivés.
        QVERIFY(!w.runCommand("file.save"));
        QVERIFY(!w.runCommand("edit.undo"));
        QVERIFY(w.runCommand("workspace.optimize"));
        QCOMPARE(w.workspace(), Workspace::Optimize);
    }

    void selectAllAndClearThroughCommands() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        QVERIFY(w.runCommand("selection.all"));
        QCOMPARE(w.session().selection().targets().size(),
                 w.session().project().embroidery_objects.size());
        QVERIFY(w.runCommand("selection.clear"));
        QVERIFY(w.session().selection().empty());
        QVERIFY(!w.runCommand("selection.clear")); // plus rien à désélectionner : désactivée
    }

    void paletteFiltersAndRunsTheChosenCommand() {
        ShellWindow w;
        CommandPalette palette;
        palette.setEntries(w.commandEntries());
        palette.setQuery("espace numerisation");
        QCOMPARE(palette.visibleIds(), QStringList{"workspace.digitize"});
        QString chosen;
        QObject::connect(&palette, &CommandPalette::chosen,
                         [&](const QString& id) { chosen = id; });
        QVERIFY(palette.activateCurrent());
        QCOMPARE(chosen, QString("workspace.digitize"));
        QVERIFY(w.runCommand(chosen));
        QCOMPARE(w.workspace(), Workspace::Digitize);

        // Une commande désactivée ne se valide pas.
        palette.setQuery("peinture");
        QCOMPARE(palette.visibleIds().size(), 1);
        QVERIFY(!palette.activateCurrent());
    }

    void commandPaletteExplainsEmptyOrUnavailableResults() {
        CommandPalette palette;
        palette.setEntries({{"disabled", "Outil indisponible", "Outil", "", false},
                            {"enabled", "Outil disponible", "Outil", "", true}});
        auto* hint = palette.findChild<QLabel*>("paletteHint");
        auto* list = palette.findChild<QListWidget*>("paletteList");
        QVERIFY(hint != nullptr);
        QVERIFY(list != nullptr);

        palette.setQuery("outil indisponible");
        QVERIFY(hint->isVisibleTo(&palette) || !hint->isHidden());
        QVERIFY(hint->text().contains("indisponibles"));
        QCOMPARE(list->currentRow(), -1);
        QVERIFY(!palette.activateCurrent());

        palette.setQuery("aucun-resultat-possible");
        QCOMPARE(list->count(), 0);
        QVERIFY(hint->text().contains("Aucune commande"));
        QVERIFY(!palette.activateCurrent());

        palette.setQuery("outil");
        QCOMPARE(list->count(), 2);
        QVERIFY(list->currentItem() != nullptr);
        QCOMPARE(list->currentItem()->data(Qt::UserRole + 1).toString(), QString("enabled"));
        QVERIFY(hint->isHidden());
    }

    void drawingToolsCreateObjectsFromMouseGestures() {
        ShellWindow w;
        w.show();
        QVERIFY(w.runCommand("file.new"));
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas->hasImage());
        QVERIFY(canvas->hasHoop()); // le cadre de broderie est dessiné
        QVERIFY(w.session().project().embroidery_objects.empty());

        // Rectangle : glisser dans le cadre.
        QVERIFY(w.runCommand("tool.draw.rectangle"));
        QVERIFY(canvas->mode() == PreviewCanvas::Mode::DrawBox);
        const QRectF scene = canvas->sceneRect().adjusted(40, 40, -40, -40);
        const QPoint a = canvas->mapFromScene(scene.center() - QPointF(60, 40));
        const QPoint b = canvas->mapFromScene(scene.center() + QPointF(60, 40));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), b);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Rectangle"));
        QCOMPARE(w.session().selection().targets().size(),
                 size_t(1)); // l'objet créé est sélectionné
        w.waitForIdle();

        // Ellipse avec Maj : cercle (largeur = hauteur).
        QVERIFY(w.runCommand("tool.draw.ellipse"));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::ShiftModifier, a);
        QTest::mouseMove(canvas->viewport(), b, 1);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::ShiftModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Ellipse"));

        // Un seul Annuler par forme.
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        w.waitForIdle();
        QVERIFY(w.previewShown());
    }

    void polygonToolClicksThenEnterAndEscapeCancels() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(w.runCommand("tool.draw.polygon"));
        QVERIFY(canvas->mode() == PreviewCanvas::Mode::DrawPolygon);
        const QPointF c = canvas->sceneRect().center();
        const auto click = [&](QPointF p) {
            QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                              canvas->mapFromScene(p));
        };
        click(c + QPointF(-50, 40));
        click(c + QPointF(50, 40));
        click(c + QPointF(0, -50));
        QCOMPARE(canvas->polygonInProgress().size(), 3);
        QTest::keyClick(canvas, Qt::Key_Backspace); // retire le dernier sommet
        QCOMPARE(canvas->polygonInProgress().size(), 2);
        QTest::keyClick(canvas, Qt::Key_Escape); // annule tout
        QCOMPARE(canvas->polygonInProgress().size(), 0);
        QVERIFY(w.session().project().embroidery_objects.empty());

        click(c + QPointF(-50, 40));
        click(c + QPointF(50, 40));
        click(c + QPointF(0, -50));
        QTest::keyClick(canvas, Qt::Key_Return);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Polygone"));
        QCOMPARE(canvas->polygonInProgress().size(), 0);

        // Changer d'outil abandonne un polygone en cours.
        click(c + QPointF(-30, 30));
        click(c + QPointF(30, 30));
        QCOMPARE(canvas->polygonInProgress().size(), 2);
        QVERIFY(w.runCommand("tool.select"));
        QCOMPARE(canvas->polygonInProgress().size(), 0);
    }

    void aRefusedShapeShowsAnErrorAndLeavesTheDocumentUntouched() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(w.runCommand("tool.draw.polygon"));
        const QPointF c = canvas->sceneRect().center();
        const auto click = [&](QPointF p) {
            QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                              canvas->mapFromScene(p));
        };
        // « Papillon » : les côtés se croisent, le moteur refuse la forme.
        click(c + QPointF(-40, -40));
        click(c + QPointF(40, 40));
        click(c + QPointF(40, -40));
        click(c + QPointF(-40, 40));
        const auto revision = w.session().revision();
        QTest::keyClick(canvas, Qt::Key_Return);
        QVERIFY(w.session().project().embroidery_objects.empty());
        QCOMPARE(w.session().revision(), revision);
        QVERIFY(w.lastNotice().contains("refusé"));
    }

    void saveIsAvailableOnANewProjectButExportNeedsStitches() {
        ShellWindow w;
        QVERIFY(!w.runCommand("file.save")); // aucun projet ouvert
        QVERIFY(w.runCommand("file.new"));
        const auto entries = w.commandEntries();
        const auto enabled = [&](const char* id) {
            for (const auto& e : entries) {
                if (e.id == id) {
                    return e.enabled;
                }
            }
            return false;
        };
        QVERIFY(enabled("file.save"));
        QVERIFY(!enabled("file.export")); // rien à exporter
        QVERIFY(enabled("tool.draw.rectangle"));
        QVERIFY(!enabled("tool.bucket")); // pas encore migré
    }

    void draggingAnObjectMovesTheSelectionInOneUndoStep() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle");
        const auto id = *createdObjectId(r.report);
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto before = *w.previewFrame().hits->boundsOf(id);
        const auto depth = w.session().undoNames().size();

        // Appui sur l'objet NON sélectionné puis glisser : il est sélectionné puis déplacé.
        const auto point = w.objectCentrePx(id);
        QVERIFY(point.has_value());
        const QPoint from = canvas->mapFromScene(*point);
        const QPoint to = from + QPoint(60, -30);
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(20, -10));
        QTest::mouseMove(canvas->viewport(), to);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Déplacer"));
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));
        w.waitForIdle();
        const auto after = *w.previewFrame().hits->boundsOf(id);
        const double expectedDx = (to - from).x() / canvas->zoom() / w.previewFrame().px_per_mm;
        const double expectedDy = (to - from).y() / canvas->zoom() / w.previewFrame().px_per_mm;
        QVERIFY(qAbs((after.x0 - before.x0) - expectedDx) < 0.8);
        QVERIFY(qAbs((after.y0 - before.y0) - expectedDy) < 0.8);

        QVERIFY(w.session().undo());
        w.waitForIdle();
        QVERIFY(qAbs(w.previewFrame().hits->boundsOf(id)->x0 - before.x0) < 0.1);
    }

    void aPlainClickOnAnObjectDoesNotMoveIt() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle");
        const auto id = *createdObjectId(r.report);
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto depth = w.session().undoNames().size();
        const QPoint at = canvas->mapFromScene(*w.objectCentrePx(id));
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(w.session().undoNames().size(), depth); // aucune étape d'annulation
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
    }

    void draggingACornerHandleScalesAboutTheOppositeCorner() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle");
        const auto id = *createdObjectId(r.report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        auto* canvas = w.canvas();
        const auto& frame = w.previewFrame();
        const auto before = *frame.hits->boundsOf(id);
        // Coin bas-droit de l'emprise affichée (cadre de sélection = emprise + 3 px de marge).
        const QPointF corner(frame.mmToPxX(before.x1) + 3, frame.mmToPxY(before.y1) + 3);
        const QPoint from = canvas->mapFromScene(corner);
        const QPoint to = from + QPoint(40, 40);
        const auto depth = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(15, 15));
        QTest::mouseMove(canvas->viewport(), to);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Mettre à l'échelle"));
        w.waitForIdle();
        const auto after = *w.previewFrame().hits->boundsOf(id);
        QVERIFY((after.x1 - after.x0) > 1.2 * (before.x1 - before.x0)); // plus grand
        QVERIFY(qAbs(after.x0 - before.x0) < 1.0); // coin opposé (haut-gauche) fixe
        QVERIFY(qAbs(after.y0 - before.y0) < 1.0);
        // Rapport d'aspect conservé (échelle uniforme).
        const double ratioBefore = (before.x1 - before.x0) / (before.y1 - before.y0);
        const double ratioAfter = (after.x1 - after.x0) / (after.y1 - after.y0);
        QVERIFY(qAbs(ratioBefore - ratioAfter) < 0.1);
    }

    void aDragOnEmptyFabricStillMakesARubberBandWhenObjectsExist() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle");
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto depth = w.session().undoNames().size();
        const QPoint a = canvas->mapFromScene(QPointF(5, 5)); // coin du tissu, hors objet
        const QPoint b = a + QPoint(25, 25);
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), b);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().undoNames().size(), depth); // rien n'a été déplacé
    }

    void editingCommandsActOnTheSelectionOnly() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
        w.waitForIdle();
        // Sans sélection : toutes ces commandes sont désactivées.
        w.session().selection().clear();
        for (const char* id :
             {"edit.delete", "edit.duplicate", "transform.rotate.cw", "order.front"}) {
            QVERIFY2(!w.runCommand(id), id);
        }
        // Dupliquer : les copies deviennent la sélection.
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(w.runCommand("edit.duplicate"));
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(3));
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
        QVERIFY(w.session().selection().primary()->id != a);
        // Supprimer la copie sélectionnée : une annulation la ramène.
        QVERIFY(w.runCommand("edit.delete"));
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        QVERIFY(w.session().selection().empty()); // la cible a disparu : sélection élaguée
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(3));
        // Ordre : « premier plan » met b en dernier.
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QVERIFY(w.runCommand("order.front"));
        QCOMPARE(w.session().project().embroidery_objects.back().id.value, b);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Premier plan"));
    }

    void rotateAndMirrorUseTheSelectionCentre() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({30, 10}, 30, 10), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto before = *w.previewFrame().hits->boundsOf(id);
        QVERIFY(w.runCommand("transform.rotate.ccw"));
        w.waitForIdle();
        const auto rotated = *w.previewFrame().hits->boundsOf(id);
        // Pivot = centre de l'objet : le centre ne bouge pas, largeur et hauteur s'échangent.
        QVERIFY(qAbs((rotated.x0 + rotated.x1) / 2 - (before.x0 + before.x1) / 2) < 0.8);
        QVERIFY(qAbs((rotated.y0 + rotated.y1) / 2 - (before.y0 + before.y1) / 2) < 0.8);
        QVERIFY(qAbs((rotated.x1 - rotated.x0) - (before.y1 - before.y0)) < 1.5);
        QVERIFY(w.runCommand("transform.mirror.h"));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Miroir horizontal"));
    }

    void arrowKeysNudgeTheSelectionByOneOrTenMillimetres() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto before = *w.previewFrame().hits->boundsOf(id);
        QTest::keyClick(w.canvas(), Qt::Key_Right);
        w.waitForIdle();
        const auto right = *w.previewFrame().hits->boundsOf(id);
        QVERIFY(qAbs((right.x0 - before.x0) - 1.0) < 0.3);
        QTest::keyClick(w.canvas(), Qt::Key_Up, Qt::ShiftModifier); // 10 mm vers le haut
        w.waitForIdle();
        const auto up = *w.previewFrame().hits->boundsOf(id);
        QVERIFY(qAbs((up.y0 - right.y0) + 10.0) < 0.5);              // haut = y écran décroissant
        QCOMPARE(w.session().undoNames().size(), size_t(1 + 1 + 1)); // création + 2 déplacements
    }

    void diagnosticsRunInTheBackgroundAndGoStaleOnEdit() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* panel = w.diagnostics();
        QCOMPARE(panel->findingCount(), 0);
        QVERIFY(w.runCommand("analysis.run"));
        QCOMPARE(w.workspace(), Workspace::Optimize); // l'analyse bascule vers son espace
        w.waitForIdle();
        QVERIFY(!panel->isStale());
        QVERIFY(w.lastNotice().contains("Analyse"));
        // Toute modification rend l'analyse affichée périmée.
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().applyBatch(batchSetVisible({id}, false), "Masquer");
        QVERIFY(panel->isStale());
        // Rien à analyser sans objet.
        ShellWindow empty;
        empty.runCommand("file.new");
        QVERIFY(!empty.runCommand("analysis.run")); // commande désactivée
    }

    void clickingAFindingSelectsItsObjectsAndSkipsVanishedOnes() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto& objects = w.session().project().embroidery_objects;
        QVERIFY(objects.size() >= 2);
        DiagnosticsResult fake;
        openstitch::project_ops::Finding f;
        f.severity = "warning";
        f.code = "TEST";
        f.message = "Constat de test";
        f.objects = {objects[0].id.value, objects[1].id.value,
                     987654321ULL}; // le dernier n'existe pas
        f.at_mm = std::array<double, 2>{0.0, 0.0};
        fake.findings = {f};
        fake.warnings = 1;
        fake.revision = w.session().revision();
        w.diagnostics()->setResult(fake);
        QCOMPARE(w.diagnostics()->findingCount(), 1);
        w.diagnostics()->activateRow(0);
        QCOMPARE(w.session().selection().targets().size(), size_t(2));
        w.diagnostics()->activateRow(5); // ligne inexistante : sans effet
        QCOMPARE(w.session().selection().targets().size(), size_t(2));
    }

    void theInspectorShowsEssentialStitchSettingsOfTheCommonType() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
        w.waitForIdle();
        auto* panel = w.inspector();
        w.session().selection().clear();
        QVERIFY(panel->paramKeys().isEmpty()); // rien de sélectionné : pas de réglages

        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(panel->paramKeys().contains("row_spacing_mm"));
        QVERIFY(panel->paramKeys().contains("angle_deg"));
        const auto depth = w.session().undoNames().size();
        QVERIFY(panel->applyParam("row_spacing_mm", 0.6));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(qAbs(*panel->paramValue("row_spacing_mm") - 0.6) <
                0.01); // l'éditeur suit le document

        // Deux objets, valeurs différentes : l'éditeur affiche « mixte » ; un réglage applique aux
        // deux.
        w.session().selection().replace(
            {{TargetKind::EmbroideryObject, a}, {TargetKind::EmbroideryObject, b}});
        QVERIFY(panel->paramMixed("row_spacing_mm"));
        QVERIFY(panel->applyParam("row_spacing_mm", 0.45));
        QVERIFY(!panel->paramMixed("row_spacing_mm"));
        QVERIFY(qAbs(*panel->paramValue("row_spacing_mm") - 0.45) < 0.01);

        // Valeur hors plage ou réglage d'un autre type : refusé, rien d'empilé.
        const auto before = w.session().undoNames().size();
        QVERIFY(!panel->applyParam("row_spacing_mm", 9.0));
        QVERIFY(!panel->applyParam("repeats", 2));
        QCOMPARE(w.session().undoNames().size(), before);

        // Types différents : plus de réglages communs.
        QVERIFY(panel->applyStitchType("running")); // les deux passent en contour
        QVERIFY(panel->paramKeys().contains("repeats"));
        w.session().applyBatch(batchSetStitchType({a}, "tatami"), "Type");
        QVERIFY(panel->paramKeys().isEmpty()); // sélection de types mixtes
    }

    void rapidInspectorNumericChangesMakeOneUndoStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        const auto added = w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle");
        const auto id = *createdObjectId(added.report);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        auto* spin = w.inspector()->findChild<QDoubleSpinBox*>("param_row_spacing_mm");
        QVERIFY(spin != nullptr);
        const auto beforeUndo = w.session().undoNames().size();
        const auto beforeRevision = w.session().revision();
        spin->setValue(0.65);
        spin->setValue(0.70);
        spin->setValue(0.75);
        QCOMPARE(w.session().revision(), beforeRevision);
        QTRY_COMPARE_WITH_TIMEOUT(w.session().revision(), beforeRevision + 1, 1500);
        QCOMPARE(w.session().undoNames().size(), beforeUndo + 1);
        const auto value =
            readParam(w.session().project().embroidery_objects.front(), "row_spacing_mm");
        QVERIFY(value.has_value());
        QVERIFY(qAbs(*value - 0.75) < 0.01);
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().undoNames().size(), beforeUndo);
    }

    void draggingAParameterLabelChangesItsValueAsOneUndoStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        const auto id = *createdObjectId(
            w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "Rectangle").report);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        auto* spin = w.inspector()->findChild<QDoubleSpinBox*>("param_row_spacing_mm");
        QVERIFY(spin != nullptr);
        QLabel* label = nullptr;
        for (auto* l : w.inspector()->findChildren<QLabel*>()) {
            if (l->buddy() == spin) {
                label = l;
            }
        }
        QVERIFY(label != nullptr);
        const double before = spin->value();
        const auto undoBefore = w.session().undoNames().size();
        QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QMouseEvent move(QEvent::MouseMove, QPointF(5 + 30, 5), QPointF(5 + 30, 5), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(label, &move); // 30 px = 5 pas
        QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, QPoint(35, 5));
        QVERIFY(qAbs(spin->value() - (before + 5 * spin->singleStep())) < 1e-6);
        QTRY_COMPARE_WITH_TIMEOUT(w.session().undoNames().size(), undoBefore + 1, 1500);
        const auto stored =
            readParam(w.session().project().embroidery_objects.front(), "row_spacing_mm");
        QVERIFY(stored.has_value());
        QVERIFY(qAbs(*stored - spin->value()) < 0.01);
    }

    void pendingInspectorNumericEditIsDiscardedWhenSelectionChanges() {
        ShellWindow w;
        w.show();
        w.newProject();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "A").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "B").report);
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        auto* spin = w.inspector()->findChild<QDoubleSpinBox*>("param_row_spacing_mm");
        QVERIFY(spin != nullptr);
        const auto beforeRevision = w.session().revision();
        spin->setValue(0.85);
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QTest::qWait(330);
        QCOMPARE(w.session().revision(), beforeRevision);
    }

    void everyActionDialogYieldsValuesTheCatalogAccepts() {
        ShellWindow w;
        for (const auto& def : actionCatalog()) {
            ActionDialog dialog(def, defaultValues(def));
            const auto values = dialog.values();
            ActionContext ctx;
            ctx.ids = {1, 2}; // contexte fictif : seul le contrôle des champs nous intéresse
            std::string error;
            if (def.run) { // actions adossées à une commande : seule la validation est contrôlée
                auto checked = values;
                QVERIFY2(validateAction(def, checked, ctx, error), def.id.c_str());
                continue;
            }
            const auto batch = buildAction(def, values, ctx, error);
            QVERIFY2(!batch.empty() || def.id == "file.import_svg" || def.id == "file.import_dxf",
                     def.id.c_str());
            if (def.id == "file.import_svg") {
                QVERIFY(error.find("obligatoire") != std::string::npos ||
                        error.find("introuvable") != std::string::npos);
            }
        }
    }

    void actionsAreInTheToolsMenuAndThePalette() {
        ShellWindow w;
        auto* menu = w.menuBar()->findChild<QMenu*>();
        QVERIFY(menu != nullptr);
        for (const auto& def : actionCatalog()) {
            QVERIFY2(w.findChild<QAction*>(QString::fromStdString("action_" + def.id)) != nullptr,
                     def.id.c_str());
        }
        const auto entries = w.commandEntries();
        int actions = 0;
        for (const auto& e : entries) {
            actions += e.id.rfind("action.", 0) == 0 ? 1 : 0;
        }
        QCOMPARE(static_cast<size_t>(actions), actionCatalog().size());
        // « Satin automatique » exige une sélection : désactivée sans objet.
        QVERIFY(!w.runCommand("action.satin.auto"));
    }

    void runningAnActionAppliesItAndRefusesBadValuesWithAMessage() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runActionWith("canvas.set", {{"width_mm", "160"}, {"height_mm", "90"}}));
        QCOMPARE(w.session().project().canvas.width.value, 160'000);
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Cadre de broderie…"));
        const auto revision = w.session().revision();
        QVERIFY(!w.runActionWith("canvas.set", {{"width_mm", "99999"}}));
        QVERIFY(w.lastNotice().contains("Largeur"));
        QCOMPARE(w.session().revision(), revision); // rien n'a changé
        QVERIFY(!w.runActionWith("no.such.action", {}));
        w.waitForIdle();
        QVERIFY(w.previewShown()); // l'aperçu suit le nouveau cadre
    }

    void textActionCreatesSelectedObjects() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runActionWith(
            "text.add", {{"text", "OK"}, {"font_file", OPENSTITCH_FONTS_DIR "/VeraBd.ttf"}}));
        QVERIFY(!w.session().project().embroidery_objects.empty());
    }

    void theRulerMeasuresADragInMillimetres() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.measure"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Measure);
        auto* canvas = w.canvas();
        const auto& f = w.previewFrame();
        // 30 mm à l'horizontale : de (-15, 0) à (15, 0) du modèle.
        const QPoint a = canvas->mapFromScene(QPointF(f.mmToPxX(-15), f.mmToPxY(0)));
        const QPoint b = canvas->mapFromScene(QPointF(f.mmToPxX(15), f.mmToPxY(0)));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), (a + b) / 2);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QVERIFY(canvas->hasMeasure());
        // Les pixels entiers arrondissent selon la taille du canevas : tolérance de 0,5 mm.
        const auto match =
            QRegularExpression(QStringLiteral("(\\d+)[.,](\\d+) mm")).match(w.lastNotice());
        QVERIFY2(match.hasMatch(), qPrintable(w.lastNotice()));
        const double measured = (match.captured(1) + "." + match.captured(2)).toDouble();
        QVERIFY2(std::abs(measured - 30.0) < 0.5, qPrintable(w.lastNotice()));
        QVERIFY(w.session().undoNames().empty()); // la règle ne modifie jamais le document
        QVERIFY(w.runCommand("tool.select"));
        QVERIFY(!canvas->hasMeasure()); // changer d'outil efface la mesure
    }

    void gridAndHoopToggles() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(!w.canvas()->hasGrid());
        w.setGridSpacing(10.0);
        QVERIFY(w.canvas()->hasGrid());
        w.setGridSpacing(0.0);
        QVERIFY(!w.canvas()->hasGrid());
        w.setGridSpacing(0.05); // trop dense : rien n'est tracé
        QVERIFY(!w.canvas()->hasGrid());
        QVERIFY(w.canvas()->hoopVisible());
        w.canvas()->setHoopVisible(false);
        QVERIFY(!w.canvas()->hoopVisible());
    }

    void contextMenuListsOnlyActiveSelectionCommands() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* none = w.buildContextMenu();
        for (auto* a : none->actions()) {
            QVERIFY(a->data().toString() != "edit.delete"); // rien de sélectionné
        }
        delete none;
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        auto* menu = w.buildContextMenu();
        QStringList ids;
        for (auto* a : menu->actions()) {
            ids << a->data().toString();
        }
        QVERIFY(ids.contains("edit.delete"));
        QVERIFY(ids.contains("transform.rotate.cw"));
        QVERIFY(ids.contains("order.front"));
        for (const auto& id : ids) { // le menu ne montre que ce que le tableau de contexte déclare
            if (id.isEmpty()) {
                continue; // séparateur
            }
            QVERIFY(commandBelongsTo(CommandSurface::ContextMenu, id.toStdString()));
        }
        QVERIFY(!ids.contains("edit.undo")); // pas de redites du menu Édition
        // Une commande du menu agit comme le menu principal.
        for (auto* a : menu->actions()) {
            if (a->data().toString() == "edit.duplicate") {
                a->trigger();
            }
        }
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        delete menu;
    }

    void spoolRailReordersByMoveAndKeyboardAndSelectsOnClick() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* rail = w.findChild<SpoolRailWidget*>("spoolRail");
        QVERIFY(rail != nullptr);
        QCOMPARE(rail->count(), 0);
        for (const double x : {-30.0, 0.0, 30.0}) {
            w.session().applyBatch(batchAddRect({x, 0}, 10, 10), "R");
        }
        w.waitForIdle();
        rail->refreshNow();
        QCOMPARE(rail->count(), 3);
        QVERIFY(rail->summaryText().startsWith("3 bobines"));
        const auto first = rail->spoolId(0);
        QVERIFY(rail->moveSpoolTo(0, 2)); // équivalent d'un glisser-déposer
        rail->refreshNow();
        QCOMPARE(rail->spoolId(2), first);
        QVERIFY(!rail->moveSpoolTo(2, 2));
        rail->setCurrentRow(2);
        QTest::keyClick(rail, Qt::Key_Left, Qt::AltModifier); // alternative clavier
        rail->refreshNow();
        QCOMPARE(rail->spoolId(1), first);
        // Un clic sélectionne l'objet dans le document.
        const auto id = rail->spoolId(0);
        Q_EMIT rail->itemClicked(rail->item(0));
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));
        w.session().undo();
        w.session().undo();
        w.waitForIdle();
        rail->refreshNow();
        QCOMPARE(rail->spoolId(0), first);
    }

    void floatingBarFollowsTheSelectionAndRunsCommands() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* bar = w.findChild<FloatingActionBar*>("floatingActionBar");
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isHidden()); // rien de sélectionné
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(bar->buttonCount() >= 3);
        QVERIFY(!bar->isHidden());
        auto* dup = bar->findChild<QToolButton*>("floatingAction_edit.duplicate");
        QVERIFY(dup != nullptr);
        dup->click();
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        w.session().selection().clear();
        QVERIFY(bar->isHidden());
    }

    void rightClickOnAnObjectSelectsItBeforeTheMenuOpens() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 30, 30), "R").report);
        w.waitForIdle();
        QVERIFY(w.session().selection().empty());
        w.showContextMenu(QPoint(10, 10), *w.objectCentrePx(id));
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));
        for (auto* menu : w.findChildren<QMenu*>("canvasContextMenu")) {
            menu->close();
        }
    }

    void simulationScrubsThroughTheSewingOrderOnlyInProduction() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        w.session().applyBatch(batchAddRect({0, 0}, 40, 30), "R");
        w.waitForIdle();
        auto* bar = w.findChild<QToolBar*>("simulationBar");
        QVERIFY(bar != nullptr);
        QVERIFY(!bar->isVisible());
        QVERIFY(w.runCommand("workspace.produce"));
        QVERIFY(bar->isVisible());
        const int total = w.simulationTotal();
        QVERIFY(total > 50);
        QVERIFY(!w.simulating());
        w.setSimulationPosition(total / 2);
        QVERIFY(w.simulating());
        QCOMPARE(w.simulationPosition(), total / 2);
        w.waitForIdle();
        QCOMPARE(w.simulationTotal(), total); // le total vient de la séquence complète
        QVERIFY(w.previewShown());
        w.setSimulationPosition(total + 10); // au-delà : tout
        QVERIFY(!w.simulating());
        w.setSimulationPosition(0);
        QVERIFY(w.simulating());
        QVERIFY(w.runCommand("workspace.design")); // quitter Production termine la simulation
        QVERIFY(!w.simulating());
        QVERIFY(w.session().undoNames().size() == 1); // la simulation ne modifie jamais le document
    }

    void themeModeIsValidatedAndRestorable() {
        ShellWindow w;
        w.setThemeMode("dark");
        QCOMPARE(w.themeMode(), QString("dark"));
        w.setThemeMode("light");
        QCOMPARE(w.themeMode(), QString("light"));
        w.setThemeMode("bogus");
        QCOMPARE(w.themeMode(), QString("system"));
        w.setThemeMode("system");
    }

    void exportImageWritesAPng() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R");
        w.waitForIdle();
        const auto png = fs::temp_directory_path() / "openstitch_v2_shell_image.png";
        fs::remove(png);
        w.exportImage(png, 3.0);
        w.waitForIdle();
        QVERIFY2(fs::exists(png), qPrintable(w.lastNotice()));
        QVERIFY(fs::file_size(png) > 500);
        QVERIFY(w.lastNotice().contains("Image"));
        fs::remove(png);
    }

    void presetsAreSavedAppliedAndListedInTheMenuAndPalette() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
        w.waitForIdle();
        QVERIFY(!w.runCommand("preset.save")); // rien de sélectionné : désactivée
        w.session().applyBatch(batchSetParam({a}, "tatami", "row_spacing_mm", 0.8), "Écart");
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(w.savePresetNamed("Trame lâche"));
        QVERIFY(!w.savePresetNamed("   ")); // nom vide refusé, message affiché
        QVERIFY(w.lastNotice().contains("nom"));
        const auto entries = w.commandEntries();
        bool apply = false;
        for (const auto& e : entries) {
            apply = apply || (e.id == "preset.apply.0" && e.enabled);
        }
        QVERIFY(apply);
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QVERIFY(w.runCommand("preset.apply.0"));
        QVERIFY(qAbs(*readParam(w.session().project().embroidery_objects[1], "row_spacing_mm") -
                     0.8) < 0.01);
        QVERIFY(w.runCommand("preset.remove.0"));
        QVERIFY(w.session().project().stitch_presets.empty());
        QVERIFY(!w.runCommand("preset.apply.0")); // plus de préréglage
    }

    void calquePanelEditsLayersThroughUndoableCommands() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* panel = w.calquePanel();
        QVERIFY(panel != nullptr);
        QCOMPARE(panel->rowCount(), 0);
        QVERIFY(panel->addNamed("Fond"));
        QCOMPARE(panel->rowCount(), 2);               // « Calque 1 » matérialisé + « Fond »
        QCOMPARE(panel->rowText(0), QString("Fond")); // le dessus d'abord
        QVERIFY(!panel->addNamed("  "));              // refus, message
        panel->setCurrentRow(0);
        QVERIFY(panel->renameCurrent("Dessus"));
        QCOMPARE(panel->rowText(0), QString("Dessus"));
        QVERIFY(panel->toggleLockCurrent());
        QVERIFY(panel->rowText(0).contains("🔒"));
        QVERIFY(panel->moveCurrent(-1)); // descend
        QVERIFY(panel->removeCurrent());
        QCOMPARE(panel->rowCount(), 1);
        panel->setCurrentRow(0);
        QVERIFY(!panel->removeCurrent()); // le dernier calque reste
        QVERIFY(w.session().undo());
        QCOMPARE(panel->rowCount(), 2);
    }

    void nodeToolDragsInsertsAndUndoesThroughTheCanvas() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 4);
        auto* canvas = w.canvas();
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::Nodes);

        // Le coin le plus à droite et en haut de l'écran.
        int corner = 0;
        for (int i = 1; i < w.nodeHandleCount(); ++i) {
            const auto a = w.nodeHandlePx(i);
            const auto b = w.nodeHandlePx(corner);
            if (a.x() - a.y() > b.x() - b.y()) {
                corner = i;
            }
        }
        const auto before = nodeView(w.session().project(), id);
        QVERIFY(before.has_value());
        const auto depth = w.session().undoNames().size();
        const QPoint from = canvas->mapFromScene(w.nodeHandlePx(corner));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(20, 10));
        QTest::mouseMove(canvas->viewport(), from + QPoint(40, 20));
        QVERIFY(canvas->hasNodeOutline());               // aperçu pendant le glisser
        QCOMPARE(w.session().undoNames().size(), depth); // rien n'est écrit avant le relâchement
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            from + QPoint(40, 20));
        w.waitForIdle();
        QVERIFY(!canvas->hasNodeOutline());
        QCOMPARE(w.session().undoNames().size(), depth + 1); // un geste = un pas d'annulation
        const auto after = nodeView(w.session().project(), id);
        QVERIFY(after.has_value());
        QVERIFY(after->nodes.size() == 4);
        double moved = 0.0;
        for (std::size_t i = 0; i < 4; ++i) {
            moved += std::abs(after->nodes[i].pos.x - before->nodes[i].pos.x) +
                     std::abs(after->nodes[i].pos.y - before->nodes[i].pos.y);
        }
        QVERIFY2(moved > 0.5, "le contour doit avoir bougé");

        // Double-clic au milieu d'un côté : un nœud de plus, sans changer la forme.
        const auto mid = nearestSegment(*after, {0.0, 5.0}, 50.0);
        QVERIFY(mid.has_value());
        const auto& a = after->nodes[mid->start.node];
        const auto& b = after->nodes[(mid->start.node + 1) % 4];
        const QPointF midMm{(a.pos.x + b.pos.x) / 2.0, (a.pos.y + b.pos.y) / 2.0};
        const QPoint midPx = canvas->mapFromScene(
            QPointF(w.previewFrame().mmToPxX(midMm.x()), w.previewFrame().mmToPxY(-midMm.y())));
        QTest::mouseDClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, midPx);
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 5);

        w.session().undo();
        w.session().undo();
        w.waitForIdle();
        const auto restored = nodeView(w.session().project(), id);
        QVERIFY(restored.has_value());
        for (std::size_t i = 0; i < 4; ++i) {
            QVERIFY(std::abs(restored->nodes[i].pos.x - before->nodes[i].pos.x) < 0.01);
        }
    }

    void companionPanelCopiesTheClaudeCodeConnectionCommand() {
        ShellWindow w;
        w.show();
        auto* button = w.companionPanel()->findChild<QPushButton*>("companionCopy");
        QVERIFY(button != nullptr);
        QGuiApplication::clipboard()->clear();
        button->click();
        const QString text = QGuiApplication::clipboard()->text();
        QVERIFY2(text.startsWith("claude mcp add openstitch --"), qPrintable(text));
        QVERIFY(text.contains("--live"));
        QVERIFY(text.contains("openstitch-mcp"));
        QCOMPARE(text, CompanionPanel::connectionCommand());
    }

    void agentChangesTheOpenProjectThroughTheLiveBridgeAfterApproval() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        const auto before = w.session().project().embroidery_objects.front().rgb;

        w.setLiveDirectory(std::filesystem::path(dir.path().toStdU16String()));
        int asked = 0;
        bool answer = true;
        w.setApprovalOverride([&](const openstitch::mcp::ApprovalRequest&) {
            ++asked;
            return answer;
        });
        QVERIFY(!w.liveBridgeEnabled()); // jamais activé sans l'utilisateur
        QVERIFY(w.runCommand("companion.toggle"));
        QVERIFY(w.liveBridgeEnabled());
        QVERIFY(w.companionPanel()->active());

        QLocalSocket socket;
        socket.connectToServer(w.liveEndpoint());
        QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QLocalSocket::ConnectedState, 5000);

        const auto send = [&](const std::string& token, const nlohmann::json& args) {
            nlohmann::json request = {{"v", 1},
                                      {"id", 1},
                                      {"token", token},
                                      {"op", "call"},
                                      {"tool", "project_apply"},
                                      {"args", args}};
            QByteArray line = QByteArray::fromStdString(request.dump());
            line.append(char(10));
            socket.write(line);
            socket.flush();
        };
        const nlohmann::json batch = {
            {"operations", {{{"op", "set_color"}, {"object", id}, {"rgb", "#FF0000"}}}},
            {"dry_run", false}};

        // Mauvais jeton : refusé, rien ne change.
        send("faux-jeton", batch);
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(), 15000);
        const auto bad = nlohmann::json::parse(socket.readLine().toStdString());
        QVERIFY(!bad["ok"].get<bool>());
        QCOMPARE(asked, 0);
        QCOMPARE(w.session().project().embroidery_objects.front().rgb, before);

        // Refus de l'utilisateur : le document reste identique.
        answer = false;
        send(w.liveToken(), batch);
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(), 15000);
        const auto refused = nlohmann::json::parse(socket.readLine().toStdString());
        QVERIFY(refused["result"]["isError"].get<bool>());
        QCOMPARE(w.session().project().embroidery_objects.front().rgb, before);

        // Accord : appliqué, en UNE étape de l'historique de l'utilisateur.
        answer = true;
        const auto depth = w.session().undoNames().size();
        send(w.liveToken(), batch);
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(), 15000);
        const auto ok = nlohmann::json::parse(socket.readLine().toStdString());
        QVERIFY(!ok["result"]["isError"].get<bool>());
        const std::array<std::uint8_t, 3> red{255, 0, 0};
        QCOMPARE(w.session().project().embroidery_objects.front().rgb, red);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        w.waitForIdle();
        QVERIFY(w.companionPanel()->eventCount() >= 2); // le journal montre refus et application

        // L'utilisateur annule la modification de l'agent, comme n'importe laquelle des siennes.
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.front().rgb, before);

        // Désactivation : le point d'accès disparaît et un nouvel appel est impossible.
        QVERIFY(w.runCommand("companion.toggle"));
        QVERIFY(!w.liveBridgeEnabled());
        QVERIFY(w.liveEndpoint().isEmpty());
    }

    void fadeAxisToolMovesAndRotatesTheTransitionThroughTheCanvas() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(w.runCommand("tool.fade.axis"));
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 0); // pas de fondu : aucune poignée, rien d'inventé

        QVERIFY(createFade(w.session(), id, FadeSettings{{200, 30, 30}, true}).ok);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 4); // début, fin, déplacement, rotation
        auto* canvas = w.canvas();
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::Nodes);
        QVERIFY(canvas->hasNodeGuides()); // l'axe est tracé

        const auto before = fadeAxisView(w.session().project(), id);
        QVERIFY(before.has_value());
        const auto depth = w.session().undoNames().size();
        const QPoint from = canvas->mapFromScene(w.nodeHandlePx(2)); // poignée de déplacement
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(10, 5));
        QTest::mouseMove(canvas->viewport(), from + QPoint(25, 10));
        QVERIFY(canvas->hasNodeOutline());               // aperçu de l'axe pendant le glisser
        QCOMPARE(w.session().undoNames().size(), depth); // rien d'écrit avant le relâchement
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            from + QPoint(25, 10));
        w.waitForIdle();
        QVERIFY(!canvas->hasNodeOutline());
        QCOMPARE(w.session().undoNames().size(), depth + 1); // un geste = un pas d'annulation
        const auto after = fadeAxisView(w.session().project(), id);
        QVERIFY(after.has_value());
        QVERIFY(std::abs(after->centre.x - before->centre.x) +
                    std::abs(after->centre.y - before->centre.y) >
                0.5);

        // Annuler remet l'axe exactement où il était.
        QVERIFY(w.session().undo());
        w.waitForIdle();
        const auto undone = fadeAxisView(w.session().project(), id);
        QVERIFY(undone.has_value());
        QVERIFY(std::abs(undone->centre.x - before->centre.x) < 0.01);

        // Échap abandonne un glisser sans rien écrire.
        const auto revision = w.session().revision();
        const QPoint start = canvas->mapFromScene(w.nodeHandlePx(1));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas->viewport(), start + QPoint(-20, 0));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->hasNodeOutline());
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            start + QPoint(-20, 0));
        QCOMPARE(w.session().revision(), revision);
    }

    void orderOptimisationShowsMeasuresThenAcceptsAsOneUndoStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        const double xs[4] = {0.0, 60.0, 30.0, 90.0};
        const char* colours[4] = {"#FF0000", "#0000FF", "#FF0000", "#0000FF"};
        for (int i = 0; i < 4; ++i) {
            QVERIFY(w.session().applyBatch(batchAddRect({xs[i], 0}, 10, 10), "Carré").committed);
        }
        for (int i = 0; i < 4; ++i) {
            const auto id =
                w.session().project().embroidery_objects[static_cast<std::size_t>(i)].id.value;
            const std::string batch = "{\"schema\":\"openstitch.batch/1\",\"operations\":[{\"op\":"
                                      "\"set_color\",\"object\":" +
                                      std::to_string(id) + ",\"rgb\":\"" + colours[i] + "\"}]}";
            QVERIFY(w.session().applyBatch(batch, "Couleur").committed);
        }
        w.waitForIdle();
        auto* panel = w.optimizePanel();
        QVERIFY(panel != nullptr);
        QVERIFY(!panel->hasProposal());
        QVERIFY(!panel->canAccept());

        std::vector<std::uint64_t> order;
        for (const auto& o : w.session().project().embroidery_objects) {
            order.push_back(o.id.value);
        }
        const auto revision = w.session().revision();
        w.runOptimization(PathStrategy::ByColor, false);
        w.waitForIdle();
        QVERIFY(panel->hasProposal());
        QCOMPARE(panel->metricCount(), 5);
        QVERIFY2(panel->metricText(0).contains("→"), qPrintable(panel->metricText(0)));
        QVERIFY(panel->canAccept());
        QCOMPARE(w.session().revision(), revision); // une proposition ne modifie rien

        // Un document modifié rend la proposition périmée : elle ne peut plus être acceptée.
        QVERIFY(w.session().applyBatch(batchAddRect({200, 0}, 5, 5), "Autre").committed);
        w.waitForIdle();
        QVERIFY(!panel->canAccept());
        QVERIFY(!w.acceptOptimization());

        // Nouvelle proposition, acceptée : un seul pas d'annulation.
        w.runOptimization(PathStrategy::ByColor, false);
        w.waitForIdle();
        QVERIFY(panel->canAccept());
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.acceptOptimization());
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(!panel->hasProposal());
        QVERIFY(w.session().undo());

        // Refuser ne change rien.
        w.runOptimization(PathStrategy::ByColor, false);
        w.waitForIdle();
        const auto after = w.session().undoNames().size();
        w.refuseOptimization();
        QVERIFY(!panel->hasProposal());
        QCOMPARE(w.session().undoNames().size(), after);
    }

    void fabricRiskOverlayShowsAnHonestEstimateAndFollowsTheDocument() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 30), "Rectangle").committed);
        w.waitForIdle();
        QVERIFY(!w.fabricRiskShown());

        QVERIFY(w.runCommand("analysis.fabric"));
        w.waitForIdle();
        QVERIFY(w.fabricRiskActive());
        QVERIFY(w.fabricRiskShown());
        QVERIFY(w.canvas()->hasFabricArrows());
        QVERIFY(w.fabricRisk().has_value());
        QVERIFY(!w.fabricRisk()->calibrated); // jamais présenté comme une mesure
        QVERIFY2(w.lastNotice().contains("NON calibrée"), qPrintable(w.lastNotice()));
        const auto revision = w.session().revision();
        QCOMPARE(w.fabricRisk()->revision, revision);

        // Une modification recalcule la carte sans action de l'utilisateur.
        QVERIFY(w.session().applyBatch(batchAddRect({60, 0}, 20, 20), "Autre").committed);
        w.waitForIdle();
        QVERIFY(w.fabricRiskShown());
        QCOMPARE(w.fabricRisk()->revision, w.session().revision());

        QVERIFY(w.runCommand("analysis.fabric")); // second appel : masque
        QVERIFY(!w.fabricRiskActive());
        QVERIFY(!w.fabricRiskShown());
        QVERIFY(!w.canvas()->hasFabricArrows());
    }

    void importDialogShowsTheSegmentationThenAppliesTheChosenColours() {
        ShellWindow w;
        w.show();
        w.importImage(tentabrode());
        auto* dialog = w.importDialog();
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->sourceShown()); // l'image d'origine est visible
        const auto settled = [&] {
            w.waitForIdle();
            return dialog->previewShown();
        };
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 30000);
        QVERIFY(dialog->paletteCount() >= 2);
        const int many = dialog->paletteCount();

        // Moins de couleurs : l'aperçu se recalcule et montre moins de régions.
        dialog->setColors(3);
        QVERIFY(!dialog->previewShown()); // l'ancien aperçu n'est plus présenté comme actuel
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 30000);
        QVERIFY(dialog->paletteCount() <= many);
        QVERIFY2(dialog->summary().contains("3"), qPrintable(dialog->summary()));
        QCOMPARE(dialog->settings().max_colors, 3);

        // Valider numérise avec CES réglages : au plus trois fils différents.
        dialog->accept();
        w.waitForIdle();
        const auto& objects = w.session().project().embroidery_objects;
        QVERIFY(!objects.empty());
        std::set<std::array<std::uint8_t, 3>> threads;
        for (const auto& o : objects) {
            threads.insert(o.rgb);
        }
        QVERIFY2(threads.size() <= 3, qPrintable(QString::number(threads.size())));
    }

    void draggingAnObjectSnapsOntoANeighbourEdgeAndShowsAGuide() {
        const auto drag = [](bool snap, double wantedMm) {
            ShellWindow w;
            w.show();
            w.newProject();
            const auto a = w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "A");
            const auto b = w.session().applyBatch(batchAddRect({50, 0}, 20, 20), "B");
            if (!(a.committed && b.committed)) {
                qFatal("setup");
            }
            w.waitForIdle();
            w.setSnapEnabled(snap);
            const auto idB = w.session().project().embroidery_objects[1].id.value;
            const auto idA = w.session().project().embroidery_objects[0].id.value;
            w.session().selection().replace({{TargetKind::EmbroideryObject, idB}});
            w.waitForIdle();
            const auto before = *w.previewFrame().hits->boundsOf(idB);
            const auto neighbour = *w.previewFrame().hits->boundsOf(idA);
            auto* canvas = w.canvas();
            const double viewPerMm = w.previewFrame().px_per_mm * canvas->transform().m11();
            const QPointF centrePx(w.previewFrame().mmToPxX((before.x0 + before.x1) / 2.0),
                                   w.previewFrame().mmToPxY((before.y0 + before.y1) / 2.0));
            const QPoint from = canvas->mapFromScene(centrePx);
            const QPoint to = from + QPoint(qRound(wantedMm * viewPerMm), 0);
            QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
            QTest::mouseMove(canvas->viewport(), from + (to - from) / 2);
            QTest::mouseMove(canvas->viewport(), to);
            const bool guide = canvas->hasSnapGuides();
            QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
            w.waitForIdle();
            const auto after = *w.previewFrame().hits->boundsOf(idB);
            struct Out {
                double moved;
                double gap;
                bool guide;
            };
            return Out{after.x0 - before.x0, after.x0 - neighbour.x1, guide};
        };

        // B est à 30 mm du bord droit de A : un glisser de -29,55 mm (0,45 mm trop court)…
        const auto snapped = drag(true, -29.55);
        QVERIFY2(std::abs(snapped.gap) < 0.05, qPrintable(QString::number(snapped.gap)));
        QVERIFY(snapped.guide); // …s'accroche au bord de A et trace un repère
        const auto free = drag(false, -29.55);
        QVERIFY2(std::abs(free.gap) > 0.2, qPrintable(QString::number(free.gap)));
        QVERIFY(!free.guide);
    }

    void curvesCanBeCreatedAndBentByDraggingASegment() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        std::vector<PointMm> octagon;
        for (int k = 0; k < 8; ++k) {
            const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
            octagon.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
        }
        QVERIFY(w.session().applyBatch(batchAddPolygon(octagon), "Octogone").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        w.waitForIdle();

        // Conversion en courbes par la commande : un pas d'annulation, des nœuds à tangentes.
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runCommand("vector.curves"));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 8);

        const auto before = nodeView(w.session().project(), id);
        QVERIFY(before.has_value());
        auto* canvas = w.canvas();
        // Milieu du bord supérieur : loin de tout nœud, c'est le segment qui est saisi.
        const auto& frame = w.previewFrame();
        const QPointF topPx(frame.mmToPxX(0.0), frame.mmToPxY(-9.24));
        const QPoint from = canvas->mapFromScene(topPx);
        for (int i = 0; i < w.nodeHandleCount(); ++i) {
            QVERIFY((canvas->mapFromScene(w.nodeHandlePx(i)) - from).manhattanLength() > 12);
        }
        const auto steps = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(0, -15));
        QTest::mouseMove(canvas->viewport(), from + QPoint(0, -35));
        QVERIFY(canvas->hasNodeOutline());               // aperçu de la courbe pendant le glisser
        QCOMPARE(w.session().undoNames().size(), steps); // rien d'écrit avant le relâchement
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            from + QPoint(0, -35));
        w.waitForIdle();
        QVERIFY(!canvas->hasNodeOutline());
        QCOMPARE(w.session().undoNames().size(), steps + 1);
        const auto after = nodeView(w.session().project(), id);
        QVERIFY(after.has_value());
        QCOMPARE(after->nodes.size(), before->nodes.size()); // les nœuds n'ont pas bougé
        for (std::size_t i = 0; i < before->nodes.size(); ++i) {
            QVERIFY(std::abs(after->nodes[i].pos.x - before->nodes[i].pos.x) < 0.01);
            QVERIFY(std::abs(after->nodes[i].pos.y - before->nodes[i].pos.y) < 0.01);
        }
        double topBefore = -1e9;
        double topAfter = -1e9;
        for (const auto& p : before->outlines.front()) {
            topBefore = std::max(topBefore, p.y);
        }
        for (const auto& p : after->outlines.front()) {
            topAfter = std::max(topAfter, p.y);
        }
        QVERIFY2(topAfter > topBefore + 0.5, "le bord doit être tiré vers le haut");
        QVERIFY(w.session().undo());
    }

    void penToolDrawsACurvedShapeInOneUndoStepAndEscapeAbandonsIt() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.draw.bezier"));
        auto* canvas = w.canvas();
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::Pen);
        const auto before = w.session().undoNames().size();

        const QPoint centre = canvas->viewport()->rect().center();
        const QPoint a = centre + QPoint(-120, 90);
        const QPoint b = centre + QPoint(120, 90);
        const QPoint c = centre + QPoint(0, -110);
        // Premier nœud : clic-glisser (nœud lisse), les deux autres : simples clics.
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), a + QPoint(15, -25));
        QTest::mouseMove(canvas->viewport(), a + QPoint(30, -50));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            a + QPoint(30, -50));
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, c);
        QCOMPARE(canvas->penNodeCount(), 3);
        QCOMPARE(w.session().undoNames().size(), before); // rien n'est écrit avant la fin
        QTest::keyClick(canvas, Qt::Key_Return);
        w.waitForIdle();
        QCOMPARE(canvas->penNodeCount(), 0);
        QCOMPARE(w.session().undoNames().size(), before + 1); // une forme = un pas d'annulation
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        const auto id = w.session().project().embroidery_objects.front().id.value;
        const auto view = nodeView(w.session().project(), id);
        QVERIFY(view.has_value());
        QCOMPARE(view->nodes.size(), size_t(3));
        QVERIFY(view->nodes.front().curved); // le nœud glissé porte une poignée
        QVERIFY(!view->nodes.back().curved); // les clics simples restent des angles
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));

        // Échap abandonne une forme en cours sans rien créer.
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(canvas->penNodeCount(), 2);
        QTest::keyClick(canvas, Qt::Key_Escape);
        QCOMPARE(canvas->penNodeCount(), 0);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));

        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().embroidery_objects.empty());
    }

    void deleteKeyRemovesTheChosenNodesNotTheObject() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        std::vector<PointMm> octagon;
        for (int k = 0; k < 8; ++k) {
            const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
            octagon.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
        }
        QVERIFY(w.session().applyBatch(batchAddPolygon(octagon), "Octogone").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 8);

        auto* canvas = w.canvas();
        const QPoint at = canvas->mapFromScene(w.nodeHandlePx(3));
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(w.selectedNodeCount(), 1);
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runCommand("edit.delete"));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1)); // l'objet reste
        QCOMPARE(w.nodeHandleCount(), 7);
        QVERIFY(w.session().undo());
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 8);
    }

    void nodeKindCommandsNeedChosenNodesAndAreUndoable() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        std::vector<PointMm> octagon;
        for (int k = 0; k < 8; ++k) {
            const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
            octagon.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
        }
        QVERIFY(w.session().applyBatch(batchAddPolygon(octagon), "Octogone").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        // Aucun nœud choisi : les trois commandes sont indisponibles.
        QVERIFY(!w.runCommand("node.smooth"));
        QVERIFY(!w.runCommand("node.corner"));
        QVERIFY(!w.runCommand("node.hole.remove"));

        auto* canvas = w.canvas();
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                          canvas->mapFromScene(w.nodeHandlePx(3)));
        QCOMPARE(w.selectedNodeCount(), 1);
        QVERIFY(!w.runCommand("node.hole.remove")); // ce nœud n'est pas dans un trou
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runCommand("node.smooth"));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.lastNotice().contains("lisse"));
        const auto reselect =
            [&] { // la sélection survit-elle au rafraîchissement ? sinon on la refait
                if (w.selectedNodeCount() == 0) {
                    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                                      canvas->mapFromScene(w.nodeHandlePx(3)));
                }
                QCOMPARE(w.selectedNodeCount(), 1);
            };
        reselect();
        QVERIFY(!w.runCommand("node.smooth")); // déjà lisse : refusé, notice claire
        QVERIFY(w.lastNotice().contains("déjà"));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        reselect();
        QVERIFY(w.runCommand("node.corner"));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 2);
        QVERIFY(w.session().undo());
        QVERIFY(w.session().undo());
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth);
    }

    void pencilTurnsAHandDrawnLoopIntoASmoothShape() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.draw.freeform"));
        auto* canvas = w.canvas();
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::Stroke);
        const auto before = w.session().undoNames().size();

        const QPoint centre = canvas->viewport()->rect().center();
        const double radius = 110.0; // pixels de vue
        const auto at = [&](int deg) {
            const double a = deg * 3.14159265358979323846 / 180.0;
            const double wobble = 2.0 * std::sin(deg * 0.7); // tremblement de la main
            return centre + QPoint(qRound((radius + wobble) * std::cos(a)),
                                   qRound((radius + wobble) * std::sin(a)));
        };
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at(0));
        for (int deg = 4; deg < 360; deg += 4) {
            QTest::mouseMove(canvas->viewport(), at(deg));
        }
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at(358));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), before + 1); // un trait = un pas d'annulation
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        const auto id = w.session().project().embroidery_objects.front().id.value;
        const auto view = nodeView(w.session().project(), id);
        QVERIFY(view.has_value());
        QVERIFY2(view->nodes.size() < 40, qPrintable(QString::number(view->nodes.size())));
        QVERIFY(view->nodes.size() >= 3);
        QVERIFY(w.session().selection().contains({TargetKind::EmbroideryObject, id}));
        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().embroidery_objects.empty());
    }

    void savedProjectReopensToTheSameMachineFileByteForByte() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto base = std::filesystem::path(dir.path().toStdU16String());
        const auto exportBytes = [](ShellWindow& window, const std::filesystem::path& file) {
            openstitch::TaskContext task;
            const auto stats = export_project_dst(window.session().snapshot(), file, task);
            if (!stats) {
                return QByteArray();
            }
            QFile f(QString::fromStdU16String(file.u16string()));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };

        QByteArray original;
        {
            ShellWindow w;
            w.openImage(tentabrode(), 80.0);
            w.waitForIdle();
            QVERIFY(!w.session().project().embroidery_objects.empty());
            // Une retouche manuelle avant l'enregistrement : elle aussi doit survivre.
            const auto id = w.session().project().embroidery_objects.front().id.value;
            QVERIFY(w.session().applyBatch(batchTranslate({id}, 2.0, -1.0), "Déplacer").committed);
            original = exportBytes(w, base / "avant.dst");
            QVERIFY(original.size() > 512);
            QVERIFY(w.saveProject(base / "projet.osp"));
        }
        ShellWindow reopened;
        reopened.openProject(base / "projet.osp");
        reopened.waitForIdle();
        QVERIFY(!reopened.session().project().embroidery_objects.empty());
        QVERIFY(
            !reopened.session().dirty()); // un projet qu'on vient d'ouvrir n'est pas « modifié »
        const QByteArray again = exportBytes(reopened, base / "apres.dst");
        QCOMPARE(again.size(), original.size());
        QVERIFY2(again == original, "le fichier machine doit être identique octet pour octet");

        // Modifier puis annuler revient exactement au même fichier.
        const auto id = reopened.session().project().embroidery_objects.front().id.value;
        QVERIFY(
            reopened.session().applyBatch(batchTranslate({id}, 5.0, 5.0), "Déplacer").committed);
        QVERIFY(exportBytes(reopened, base / "modifie.dst") != original);
        QVERIFY(reopened.session().undo());
        QVERIFY(exportBytes(reopened, base / "annule.dst") == original);
    }

    void inspectorHidesAdvancedSettingsUntilAskedOrSearched() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        w.waitForIdle();
        auto* inspector = w.inspector();
        QVERIFY(inspector != nullptr);

        // Par défaut : les essentiels seulement.
        auto keys = inspector->visibleParamKeys();
        QVERIFY(keys.contains("row_spacing_mm"));
        QVERIFY(keys.contains("angle_deg"));
        QVERIFY(!keys.contains("stagger"));
        QVERIFY(!keys.contains("underlay_inset_mm"));
        QVERIFY(inspector->hiddenAdvancedCount() >= 2);

        // La case « Réglages avancés » les révèle tous.
        inspector->setAdvancedShown(true);
        keys = inspector->visibleParamKeys();
        QVERIFY(keys.contains("stagger"));
        QVERIFY(keys.contains("underlay_inset_mm"));
        inspector->setAdvancedShown(false);

        // La recherche (sans accent ni casse) ne montre que ce qui correspond, avancés compris.
        inspector->setSearch("SOUS-COUCHE");
        keys = inspector->visibleParamKeys();
        QVERIFY(keys.contains("underlay_edge"));
        QVERIFY(keys.contains("underlay_inset_mm")); // avancé, révélé par la recherche
        QVERIFY(!keys.contains("angle_deg"));
        inspector->setSearch("ecart");
        keys = inspector->visibleParamKeys();
        QVERIFY(keys.contains("row_spacing_mm")); // « Écart entre rangées »
        QVERIFY(!keys.contains("pull_compensation_mm"));
        inspector->setSearch("zzz-inconnu");
        QVERIFY(inspector->visibleParamKeys().isEmpty());
        inspector->setSearch(QString());
        QVERIFY(inspector->visibleParamKeys().contains("angle_deg"));

        // Un réglage avancé modifié est bien appliqué (une étape d'annulation) et relu.
        inspector->setAdvancedShown(true);
        const auto depth = w.session().undoNames().size();
        QVERIFY(inspector->applyParam("stagger", 5.0));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.session().undo());
    }

    void tangentHandlesAppearForChosenCurvedNodesAndBendTheCurve() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        w.newProject();
        std::vector<PointMm> octagon;
        for (int k = 0; k < 8; ++k) {
            const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
            octagon.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
        }
        QVERIFY(w.session().applyBatch(batchAddPolygon(octagon), "Octogone").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(w.runCommand("vector.curves"));
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        auto* canvas = w.canvas();
        QCOMPARE(w.tangentHandleCount(), 0); // aucune poignée tant qu'aucun nœud n'est choisi

        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                          canvas->mapFromScene(w.nodeHandlePx(2)));
        QCOMPARE(w.selectedNodeCount(), 1);
        QCOMPARE(w.tangentHandleCount(), 2); // les deux poignées du nœud lisse choisi

        const auto before = nodeView(w.session().project(), id);
        QVERIFY(before.has_value());
        const auto depth = w.session().undoNames().size();
        const QPoint from = canvas->mapFromScene(w.tangentHandlePx(0));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(10, 10));
        QTest::mouseMove(canvas->viewport(), from + QPoint(25, 20));
        QVERIFY(canvas->hasNodeOutline()); // aperçu de la courbe pendant le glisser
        QCOMPARE(w.session().undoNames().size(), depth);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            from + QPoint(25, 20));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 1); // un geste = un pas d'annulation
        const auto after = nodeView(w.session().project(), id);
        QVERIFY(after.has_value());
        QCOMPARE(after->nodes.size(), before->nodes.size());
        for (std::size_t i = 0; i < before->nodes.size(); ++i) { // les nœuds n'ont pas bougé
            QVERIFY(std::abs(after->nodes[i].pos.x - before->nodes[i].pos.x) < 0.01);
        }
        bool changed = false;
        for (std::size_t i = 0; i < before->tangents.size(); ++i) {
            changed = changed ||
                      std::abs(after->tangents[i].pos.x - before->tangents[i].pos.x) > 0.3 ||
                      std::abs(after->tangents[i].pos.y - before->tangents[i].pos.y) > 0.3;
        }
        QVERIFY2(changed, "au moins une poignée doit avoir bougé");
        QVERIFY(w.session().undo());
    }

    void fadeCurveCommandsChangeTheTransitionShapeInOneStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(!w.runCommand("fade.curve.steep")); // pas de fondu : commande indisponible
        QVERIFY(createFade(w.session(), id, FadeSettings{{200, 30, 30}, true}).ok);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runCommand("fade.curve.steep"));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(fadeAxisView(w.session().project(), id)->curve == FadeCurve::Steep);
        QVERIFY(w.lastNotice().contains("Resserrée"));
        QVERIFY(w.runCommand("fade.curve.easein"));
        QVERIFY(fadeAxisView(w.session().project(), id)->curve == FadeCurve::EaseIn);
        QVERIFY(w.session().undo());
        QVERIFY(fadeAxisView(w.session().project(), id)->curve == FadeCurve::Steep);
    }

    void myHoopsAreSavedReloadedAppliedAndRemoved() {
        const auto dir = std::filesystem::temp_directory_path() / "openstitch_v2_my_hoops";
        std::filesystem::remove_all(dir);
        const auto file = dir / "hoops.txt";
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(applyHoop(w.session(), "round-100", 4.0).ok);
        {
            ExportOptionsDialog dialog(w.session(), file);
            const int libraryOnly = dialog.hoopCount();
            QCOMPARE(dialog.userHoopCount(), 0);
            QVERIFY(!dialog.saveCurrentHoopAs("  ").isEmpty());  // nom vide : refus clair
            QVERIFY(!dialog.saveCurrentHoopAs("a;b").isEmpty()); // « ; » interdit
            QVERIFY(dialog.saveCurrentHoopAs("Rond atelier").isEmpty());
            QCOMPARE(dialog.userHoopCount(), 1);
            QCOMPARE(dialog.hoopCount(), libraryOnly + 1);
            QVERIFY(std::filesystem::exists(file));
            QVERIFY(!dialog.removeUserHoopNamed("inconnu").isEmpty());
        }
        // Un autre cadre est appliqué, puis « Mes cadres » le ramène en une étape d'annulation.
        QVERIFY(applyHoop(w.session(), "rect-130x180", 0.0).ok);
        QCOMPARE(w.session().project().canvas.width.value, 130'000);
        {
            ExportOptionsDialog dialog(w.session(), file); // relu depuis le fichier
            QCOMPARE(dialog.userHoopCount(), 1);
            dialog.chooseHoop("user:Rond atelier", 0.0);
            const auto depth = w.session().undoNames().size();
            QVERIFY(dialog.apply().isEmpty());
            QCOMPARE(w.session().undoNames().size(), depth + 1);
            QCOMPARE(w.session().project().canvas.width.value, 100'000);
            QCOMPARE(w.session().project().canvas.margin.value, 4'000);
            QVERIFY(w.session().project().canvas.shape == openstitch::document::HoopShape::Ellipse);
            QVERIFY(dialog.removeUserHoopNamed("rond ATELIER")
                        .isEmpty()); // sans tenir compte de la casse
            QCOMPARE(dialog.userHoopCount(), 0);
        }
        {
            ExportOptionsDialog dialog(w.session(), file);
            QCOMPARE(dialog.userHoopCount(), 0); // la suppression est bien écrite
        }
        {
            ExportOptionsDialog noStore(w.session()); // sans dossier de préférences : refus clair
            QVERIFY(!noStore.saveCurrentHoopAs("X").isEmpty());
        }
        std::filesystem::remove_all(dir);
    }

    void threadPaletteDialogAssignsChartThreadsInOneUndoStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(!w.runCommand("thread.palette")); // aucun fil
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        w.waitForIdle();
        QVERIFY(w.runCommand("thread.palette"));
        auto* dialog = w.findChild<ThreadPaletteDialog*>();
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->chartCount() >= 2);
        QVERIFY(dialog->threadRowCount() > 0);
        QVERIFY(dialog->nearestText().contains("Plus proches"));
        QVERIFY(dialog->blendText().contains("Mélange"));
        QVERIFY(!dialog->assignSelected().isEmpty()); // aucun fil choisi : refus en clair
        const auto depth = w.session().undoNames().size();
        dialog->selectThreadRow(1);
        QVERIFY(dialog->assignSelected().isEmpty());
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        const auto& obj = w.session().project().embroidery_objects.front();
        QVERIFY(obj.thread.has_value());
        QVERIFY(dialog->assignNearest().isEmpty());
        QCOMPARE(w.session().undoNames().size(), depth + 2);
        dialog->setSearch("zzzz-no-thread-has-this");
        QCOMPARE(dialog->threadRowCount(), 0);
        dialog->close();
    }

    void densityAndThemeArePersistedAndRestored() {
        const auto dir = std::filesystem::temp_directory_path() / "openstitch_v2_prefs_shell";
        std::filesystem::remove_all(dir);
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            QCOMPARE(w.density(), UiDensity::Normal);
            const QString normal = qApp->styleSheet();
            w.setDensity(UiDensity::Spacious);
            w.setThemeMode("dark");
            QVERIFY(qApp->styleSheet() != normal); // marges plus grandes : feuille différente
            QVERIFY(w.findChild<QAction*>("density_spacious")->isChecked());
            QVERIFY(w.findChild<QAction*>("theme_dark")->isChecked());
        }
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            QCOMPARE(w.density(), UiDensity::Spacious);
            QCOMPARE(w.themeMode(), QString("dark"));
            QVERIFY(w.findChild<QAction*>("density_spacious")->isChecked());
            w.setDensity(UiDensity::Compact);
            QVERIFY(qApp->styleSheet().contains("padding:"));
        }
        // Une feuille à l'échelle 1 reste celle d'origine ; hors bornes, l'échelle est bornée.
        QCOMPARE(studioStyleSheet(StudioScheme::Light, 1.0), studioStyleSheet(StudioScheme::Light));
        QVERIFY(studioStyleSheet(StudioScheme::Light, 1.3) !=
                studioStyleSheet(StudioScheme::Light));
        QCOMPARE(studioStyleSheet(StudioScheme::Light, 99.0),
                 studioStyleSheet(StudioScheme::Light, 2.0));
        std::filesystem::remove_all(dir);
    }

    void textEditCommandNeedsAWholeTextAndRewritesItInOneStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(!w.runCommand("text.edit")); // rien de sélectionné
        QVERIFY(w.runActionWith("text.add", {{"text", "Ab"},
                                             {"letter_spacing_mm", "1"},
                                             {"font_file", OPENSTITCH_FONTS_DIR "/VeraBd.ttf"}}));
        w.waitForIdle();
        QCOMPARE(w.session().project().text_objects.size(), size_t{1});
        QVERIFY(w.runCommand("text.edit")); // les lettres créées sont sélectionnées
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runActionWith("text.edit", {{"text", "Abc"}, {"letter_spacing_mm", "2"}}));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(QString::fromStdString(w.session().project().text_objects.front().text),
                 QString("Abc"));
        QCOMPARE(w.session().project().text_objects.front().letter_spacing.value, 2'000);
        QVERIFY(w.session().undo());
        QCOMPARE(QString::fromStdString(w.session().project().text_objects.front().text),
                 QString("Ab"));
    }

    void letterCommandsNeedOneLetterAndEditItInOneUndoStep() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(!w.runCommand("text.glyph")); // rien de sélectionné
        QVERIFY(w.runActionWith(
            "text.add", {{"text", "Ab"}, {"font_file", OPENSTITCH_FONTS_DIR "/VeraBd.ttf"}}));
        w.waitForIdle();
        // Après l'ajout, TOUTES les lettres sont sélectionnées : deux lettres, donc refus clair.
        QVERIFY(!w.runCommand("text.glyph"));
        std::uint64_t a = 0;
        for (const auto& e : w.session().project().embroidery_objects) {
            if (e.name == "Lettre A") {
                a = e.id.value;
            }
        }
        QVERIFY(a != 0);
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(w.runCommand("text.glyph"));        // ouvre le dialogue
        QVERIFY(!w.runCommand("text.glyph.reset")); // jamais retouchée : indisponible
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runActionWith("text.glyph", {{"dx_mm", "3"}, {"scale", "1.2"}}));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(w.session().project().text_objects.front().glyph_edits.size(), size_t{1});
        // Les lettres du texte sont de nouveau sélectionnées : choisir « A » pour rétablir.
        a = 0;
        for (const auto& e : w.session().project().embroidery_objects) {
            if (e.name == "Lettre A") {
                a = e.id.value;
            }
        }
        w.session().selection().replace({{TargetKind::EmbroideryObject, a}});
        QVERIFY(w.runCommand("text.glyph.reset"));
        w.waitForIdle();
        QVERIFY(w.session().project().text_objects.front().glyph_edits.empty());
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().text_objects.front().glyph_edits.size(), size_t{1});
    }

    void machineOptionsDialogChangesHoopMachineAndTrimsInUndoableSteps() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(!w.runCommand("export.check")); // rien à contrôler
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        w.waitForIdle();
        QVERIFY(w.runCommand("export.check"));
        QVERIFY(w.lastNotice().contains("points cousus"));
        QVERIFY(w.runCommand("export.options"));
        auto* dialog = w.findChild<ExportOptionsDialog*>();
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->hoopCount() >= 10);
        QVERIFY(dialog->machineCount() >= 4);
        const auto depth = w.session().undoNames().size();
        dialog->chooseMachine("generic-single-130x180");
        dialog->chooseHoop("round-100", 4.0);
        dialog->setTrimThresholdMm(6.0);
        dialog->setLockKind(2);
        dialog->setDesignStart(true, -15.0, -5.0);
        QVERIFY(dialog->apply().isEmpty());
        QCOMPARE(w.session().undoNames().size(), depth + 3); // machine, cadre, finitions
        QCOMPARE(QString::fromStdString(w.session().project().machine_id),
                 QString("generic-single-130x180"));
        QCOMPARE(QString::fromStdString(currentHoopId(w.session().project())),
                 QString("round-100"));
        QCOMPARE(exportOptionsOf(w.session().project()).lockKind, LockKind::Triangle);
        QVERIFY(exportOptionsOf(w.session().project()).designStart.has_value());
        QVERIFY(dialog->apply().isEmpty()); // rien ne change : aucune trace de plus
        QCOMPARE(w.session().undoNames().size(), depth + 3);
        QVERIFY(dialog->checkText().contains("cousus"));
        dialog->close();
    }

    void pullComparisonAndCalibrationAssistantAreReachable() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(!w.runCommand("analysis.pull")); // rien à comparer
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        w.waitForIdle();
        QVERIFY(w.runCommand("analysis.pull"));
        QVERIFY(w.lastNotice().contains("NON calibrée"));
        QVERIFY(w.runCommand("analysis.calibration"));
        auto* dialog = w.findChild<CalibrationDialog*>();
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->rowCount() >= 6);
        QVERIFY(dialog->comparisonText().contains("Tissu"));
        QVERIFY(dialog->fitText().contains("insuffisantes"));
        // Mesures = ce que prédit un autre tissu : l'ajustement devient possible, mais le modèle
        // n'est jamais déclaré calibré.
        PullCalibration other("linen");
        for (const auto& r : other.rows()) {
            QVERIFY(other.setMeasure(r.id, r.predictedMm));
        }
        QCOMPARE(dialog->loadMeasurementsCsv(QString::fromStdString(other.csv())),
                 static_cast<int>(other.rows().size()));
        QVERIFY(dialog->fitText().contains("non calibré"));
        QVERIFY(!dialog->setMeasure("no-such-metric", 5.0));
        dialog->close();
    }

    void satinRungCommandsEditAndDiagnoseTheSelectedColumn() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        w.waitForIdle();
        const auto rectId = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, rectId}});
        QVERIFY(!w.runCommand("satin.rung.insert")); // pas une colonne satin
        const auto made = createSatinColumn(
            w.session(), {{{50, 0}, {50, 4}}, {{51, 0}, {51, 4}}, {{60, 0}, {60, 4}}});
        QVERIFY(made.ok);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, made.objectId}});
        const auto depth = w.session().undoNames().size();
        QVERIFY(w.runCommand("satin.rung.insert"));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(satinRungCount(w.session().project(), made.objectId), 4);
        QVERIFY(w.runCommand("satin.rung.remove"));
        QCOMPARE(satinRungCount(w.session().project(), made.objectId), 3);
        QVERIFY(w.runCommand("satin.diagnose"));
        QVERIFY(w.lastNotice().contains("saine"));
        QVERIFY(w.session().undo());
        QCOMPARE(satinRungCount(w.session().project(), made.objectId), 4);
    }

    void boundaryBrushNeedsAChosenRegionThenRepaintsAlongTheStroke() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 5;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.region.brush"));
        QCOMPARE(w.activeToolId(), QStringLiteral("region.brush"));
        auto* canvas = w.canvas();
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::Stroke);
        auto* panel = w.regionPanel();
        auto* controller = w.regionController();
        QVERIFY(panel != nullptr && controller != nullptr);
        panel->setBrushRadius(30);
        QCOMPARE(panel->brushRadius(), 30);

        const auto drag = [&] {
            const QRect view = canvas->viewport()->rect();
            const QPoint a = view.center() - QPoint(view.width() / 3, view.height() / 3);
            const QPoint b = view.center() + QPoint(view.width() / 3, view.height() / 3);
            QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
            for (int i = 1; i <= 20; ++i) {
                QTest::mouseMove(canvas->viewport(), a + (b - a) * i / 20);
            }
            QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        };
        const auto depth = w.session().undoNames().size();
        drag(); // aucune région choisie : refus clair, rien dans l'historique
        QCOMPARE(w.session().undoNames().size(), depth);
        QVERIFY(w.lastNotice().contains("choisissez d'abord"));

        const auto regions = listRegions(w.session().project());
        QVERIFY(regions.size() >= 3);
        controller->selectRegion(regions.front().id);
        const auto labelsBefore = w.session().project().segmentation->labels;
        drag();
        QVERIFY2(w.session().undoNames().size() == depth + 1, // un trait = un pas d'annulation
                 qPrintable(w.lastNotice()));
        QVERIFY(w.session().project().segmentation->labels != labelsBefore);
        QVERIFY(panel->regenerateEnabled()); // la correction attend la régénération
        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().segmentation->labels == labelsBefore);
    }

    void regionToolRecolorsAndMergesWholeColorClasses() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 5;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.region.edit"));
        auto* panel = w.regionPanel();
        auto* controller = w.regionController();
        QVERIFY(panel != nullptr && controller != nullptr);
        const auto regions = listRegions(w.session().project());
        QVERIFY(regions.size() >= 2);
        controller->selectRegion(regions.front().id);
        auto* recolor = panel->findChild<QPushButton*>("regionRecolor");
        auto* mergeColors = panel->findChild<QPushButton*>("regionMergeColors");
        QVERIFY(recolor != nullptr && recolor->isEnabled());
        QVERIFY(mergeColors != nullptr && mergeColors->isEnabled());
        auto* fillHoles = panel->findChild<QPushButton*>("regionFillHoles");
        QVERIFY(fillHoles != nullptr && fillHoles->isEnabled());

        auto newColor = std::array<std::uint8_t, 3>{31, 71, 137};
        if (newColor == regions.front().rgb) {
            newColor = {137, 71, 31};
        }
        const auto depth = w.session().undoNames().size();
        QVERIFY(controller->recolorSelected(newColor));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(panel->regenerateEnabled());
        const auto* recolored =
            w.session().project().segmentation->find(openstitch::RegionId{regions.front().id});
        QVERIFY(recolored != nullptr);
        QVERIFY(recolored->rgb == newColor);
        QVERIFY(w.session().undo());
        const auto* restored =
            w.session().project().segmentation->find(openstitch::RegionId{regions.front().id});
        QVERIFY(restored != nullptr);
        QVERIFY(restored->rgb == regions.front().rgb);

        controller->selectRegion(regions.front().id);
        const auto otherColor = [&regions](const RegionInfo& r) {
            return r.rgb != regions.front().rgb;
        };
        const auto target = std::find_if(regions.begin(), regions.end(), otherColor);
        QVERIFY(target != regions.end());
        QVERIFY(controller->mergeSelectedColorInto(target->id));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        const auto merged = listRegions(w.session().project());
        QVERIFY(std::none_of(merged.begin(), merged.end(),
                             [&](const RegionInfo& r) { return r.rgb == regions.front().rgb; }));
    }

    void regionPanelRetainsScrollAndMergeTargetsAcrossRefreshes() {
        RegionPanel panel;
        panel.resize(430, 300);
        panel.show();
        std::vector<RegionInfo> regions;
        for (std::uint64_t id = 1; id <= 100; ++id) {
            regions.push_back({id, {static_cast<std::uint8_t>(id), 35, 95}, 110 - id});
        }
        panel.setRegions(regions, regions[19].id);
        auto* list = panel.findChild<QListWidget*>(QStringLiteral("regionList"));
        QVERIFY(list != nullptr);
        QCoreApplication::processEvents();
        auto* scrollbar = list->verticalScrollBar();
        QVERIFY(scrollbar->maximum() > 0);
        scrollbar->setValue(std::min(25, scrollbar->maximum()));
        const int originalPosition = scrollbar->value();
        QSignalSpy selectionEvents(&panel, &RegionPanel::regionChosen);
        QVERIFY(selectionEvents.isValid());

        panel.setRegions(regions, regions[19].id);
        QCOMPARE(scrollbar->value(), originalPosition);
        QCOMPARE(selectionEvents.size(), 0);
        QCOMPARE(panel.selectedRegion(), regions[19].id);

        panel.setNeighbours({regions[1], regions[2], regions[3]});
        auto* neighbours = panel.findChild<QComboBox*>(QStringLiteral("regionNeighbours"));
        QVERIFY(neighbours != nullptr);
        neighbours->setCurrentIndex(1);
        const auto chosenNeighbour = neighbours->currentData();
        panel.setNeighbours({regions[3], regions[2], regions[1]});
        QCOMPARE(neighbours->currentData(), chosenNeighbour);
        panel.setNeighbours({regions[1]});
        const auto remainingNeighbour = static_cast<qulonglong>(regions[1].id);
        QCOMPARE(neighbours->currentData().toULongLong(), remainingNeighbour);

        panel.setColorTargets({regions[4], regions[5], regions[6]});
        auto* colors = panel.findChild<QComboBox*>(QStringLiteral("regionColorTargets"));
        QVERIFY(colors != nullptr);
        colors->setCurrentIndex(2);
        const auto chosenColor = colors->currentData();
        panel.setColorTargets({regions[6], regions[4], regions[5]});
        QCOMPARE(colors->currentData(), chosenColor);
        panel.setColorTargets({regions[4]});
        const auto remainingColor = static_cast<qulonglong>(regions[4].id);
        QCOMPARE(colors->currentData().toULongLong(), remainingColor);
    }

    void regionToolDeletesMergesAndRegeneratesTheObjects() {
        ShellWindow w;
        w.resize(1400, 900);
        w.show();
        openstitch::project_ops::DigitizeSettings settings;
        settings.max_colors = 5;
        w.openImage(tentabrode(), 60.0, settings);
        w.waitForIdle();
        QVERIFY(!w.session().project().embroidery_objects.empty());
        auto* panel = w.regionPanel();
        QVERIFY(panel != nullptr);
        auto* canvas = w.canvas();

        QVERIFY(w.runCommand("tool.region.edit"));
        w.waitForIdle();
        QVERIFY(panel->regionCount() >= 3);
        QVERIFY(canvas->hasRegionOverlay()); // la carte des régions est montrée sur le motif

        // Un clic sur le motif choisit la région dessous.
        const auto& frame = w.previewFrame();
        const QPoint centre = canvas->mapFromScene(QPointF(frame.mmToPxX(0.0), frame.mmToPxY(0.0)));
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        if (panel->selectedRegion() == 0) {
            // Le centre tombe sur le fond : on choisit une région dans la liste à la place.
            w.regionController()->selectRegion(listRegions(w.session().project()).front().id);
        }
        QVERIFY(panel->selectedRegion() != 0);
        QVERIFY(!panel->regenerateEnabled()); // rien à régénérer tant que rien n'est corrigé

        // Supprimer une région : une étape d'annulation, les objets ne bougent pas encore.
        std::vector<std::uint64_t> idsBefore;
        for (const auto& o : w.session().project().embroidery_objects) {
            idsBefore.push_back(o.id.value);
        }
        const int regionsBefore = panel->regionCount();
        const auto depth = w.session().undoNames().size();
        auto* del = panel->findChild<QPushButton*>("regionDelete");
        QVERIFY(del != nullptr && del->isEnabled());
        del->click();
        QCOMPARE(panel->regionCount(), regionsBefore - 1);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        std::vector<std::uint64_t> idsAfterDelete;
        for (const auto& o : w.session().project().embroidery_objects) {
            idsAfterDelete.push_back(o.id.value);
        }
        QVERIFY(idsAfterDelete == idsBefore); // les objets ne suivent pas encore
        QVERIFY(panel->regenerateEnabled());  // et le panneau le dit

        // Fusionner deux voisines : une région de moins, encore une étape.
        w.regionController()->selectRegion(listRegions(w.session().project()).front().id);
        QVERIFY(panel->neighbourCount() > 0);
        auto* merge = panel->findChild<QPushButton*>("regionMerge");
        QVERIFY(merge != nullptr && merge->isEnabled());
        merge->click();
        QCOMPARE(panel->regionCount(), regionsBefore - 2);
        QCOMPARE(w.session().undoNames().size(), depth + 2);

        // Régénérer : les objets sont refaits depuis les régions corrigées, en UNE étape.
        auto* regen = panel->findChild<QPushButton*>("regionRegenerate");
        QVERIFY(regen != nullptr && regen->isEnabled());
        regen->click();
        w.waitForIdle();
        QVERIFY(!panel->regenerateEnabled()); // plus rien en attente
        QCOMPARE(w.session().undoNames().size(), depth + 3);
        std::vector<std::uint64_t> idsAfterRegen;
        for (const auto& o : w.session().project().embroidery_objects) {
            idsAfterRegen.push_back(o.id.value);
        }
        QVERIFY(idsAfterRegen != idsBefore); // de nouveaux objets
        QVERIFY(!idsAfterRegen.empty());

        // Annuler la régénération rend exactement les objets d'avant.
        QVERIFY(w.session().undo());
        w.waitForIdle();
        std::vector<std::uint64_t> idsUndone;
        for (const auto& o : w.session().project().embroidery_objects) {
            idsUndone.push_back(o.id.value);
        }
        QVERIFY(idsUndone == idsBefore);
    }

    void nodeToolEditsSatinRailsAndShowsTheRungs() {
        ShellWindow w;
        w.show();
        w.newProject();
        const auto made = createSatinColumn(
            w.session(), {{{-10, -3}, {-10, 3}}, {{0, -3}, {0, 3}}, {{10, -3}, {10, 3}}});
        QVERIFY(made.ok);
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, made.objectId}});
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 6); // trois nœuds par rail
        auto* canvas = w.canvas();
        QVERIFY(canvas->hasNodeGuides()); // les barreaux sont dessinés

        const auto before = nodeView(w.session().project(), made.objectId);
        QVERIFY(before.has_value());
        const auto depth = w.session().undoNames().size();
        const QPoint from = canvas->mapFromScene(w.nodeHandlePx(1)); // milieu du rail A
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(0, 15));
        QTest::mouseMove(canvas->viewport(), from + QPoint(0, 30));
        QVERIFY(canvas->hasNodeOutline());
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            from + QPoint(0, 30));
        w.waitForIdle();
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        const auto after = nodeView(w.session().project(), made.objectId);
        QVERIFY(after.has_value());
        QVERIFY(std::abs(after->nodes[1].pos.y - before->nodes[1].pos.y) > 0.5);
        QVERIFY(std::abs(after->nodes[4].pos.y - before->nodes[4].pos.y) < 0.01); // l'autre rail
        w.session().undo();
        w.waitForIdle();
        QCOMPARE(w.nodeHandleCount(), 6);
    }

    void escapeCancelsANodeDragWithoutChangingTheDocument() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        w.waitForIdle();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(w.runCommand("tool.node.edit"));
        w.waitForIdle();
        auto* canvas = w.canvas();
        const auto revision = w.session().revision();
        const QPoint from = canvas->mapFromScene(w.nodeHandlePx(0));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(canvas->viewport(), from + QPoint(30, 30));
        QVERIFY(canvas->hasNodeOutline());
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->hasNodeOutline());
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            from + QPoint(30, 30));
        QCOMPARE(w.session().revision(), revision);
    }

    void densityMapOverlaysThePreviewAndFollowsTheDocument() {
        ShellWindow w;
        w.show();
        w.newProject();
        QVERIFY(w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        w.waitForIdle();
        QVERIFY(w.previewShown());
        QVERIFY(!w.densityShown());

        QVERIFY(w.runCommand("analysis.density"));
        w.waitForIdle();
        QVERIFY(w.densityActive());
        QVERIFY(w.densityShown());
        QVERIFY(w.canvas()->hasDensityOverlay());
        const auto revision = w.session().revision();
        QCOMPARE(w.session().revision(), revision); // analyse = lecture seule

        // Une modification recalcule la carte sans l'intervention de l'utilisateur.
        QVERIFY(w.session().applyBatch(batchAddRect({40, 0}, 10, 10), "Autre").committed);
        w.waitForIdle();
        QVERIFY(w.densityShown());

        QVERIFY(w.runCommand("analysis.density")); // second appel : masque
        QVERIFY(!w.densityActive());
        QVERIFY(!w.densityShown());
    }

    void threadPanelListsThreadsOrderAndColorways() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        (void)w.session().applyBatch(batchAddRect({-30, 0}, 10, 10), "A");
        (void)w.session().applyBatch(batchAddRect({0, 0}, 10, 10), "B");
        (void)w.session().applyBatch(batchAddRect({30, 0}, 10, 10), "C");
        w.waitForIdle();
        auto* panel = w.threadPanel();
        QVERIFY(panel != nullptr);
        QCOMPARE(panel->orderCount(), 3);
        QVERIFY(panel->threadCount() >= 1);
        // Fils : sélectionner puis recolorer.
        panel->setCurrentThread(0);
        QVERIFY(panel->selectCurrentThread());
        QVERIFY(!w.session().selection().empty());
        QVERIFY(panel->recolorCurrentThread(QColor(255, 0, 0)));
        QVERIFY(panel->threadText(0).contains("#ff0000"));
        QVERIFY(w.session().undo());
        QVERIFY(!panel->threadText(0).contains("#ff0000"));
        // Ordre de couture : déplacer le premier plus tard, refuser de dépasser la fin.
        const QString firstBefore = panel->orderText(0);
        panel->setCurrentOrder(0);
        QVERIFY(panel->moveCurrentOrder(+1));
        QVERIFY(panel->orderText(1).contains(firstBefore.mid(firstBefore.indexOf(". ") + 2)));
        panel->setCurrentOrder(2);
        QVERIFY(!panel->moveCurrentOrder(+1));
        // Coloris : ajouter, activer l'original, supprimer l'inactif.
        QVERIFY(!panel->addColorwayNamed("  "));
        QVERIFY(panel->addColorwayNamed("Hiver"));
        QCOMPARE(panel->colorwayCount(), 2);
        QVERIFY(panel->colorwayText(1).contains("actif"));
        panel->setCurrentColorway(0);
        QVERIFY(panel->activateCurrentColorway());
        QVERIFY(panel->colorwayText(0).contains("actif"));
        panel->setCurrentColorway(1);
        QVERIFY(panel->renameCurrentColorway("Ete"));
        QVERIFY(panel->removeCurrentColorway());
        QCOMPARE(panel->colorwayCount(), 1);
    }

    void theWelcomePageExplainsTheJourneyAndTheGuideFollowsTheDocument() {
        ShellWindow w;
        w.show();
        // Sans projet : accueil, pas de bandeau d'étapes.
        QVERIFY(w.welcomeVisible());
        QCOMPARE(w.guideNext(), NextAction::OpenImage);
        QVERIFY(w.findChild<QPushButton*>("welcomeOpen") != nullptr);
        QVERIFY(w.findChild<QPushButton*>("welcomeNew") != nullptr);
        QVERIFY(!w.findChild<QWidget*>("guideStrip")->isVisible());
        // Le bouton « Nouveau projet vierge » ouvre un projet vide : le bandeau apparaît.
        w.findChild<QPushButton*>("welcomeNew")->click();
        w.waitForIdle();
        QVERIFY(!w.welcomeVisible());
        QVERIFY(w.findChild<QWidget*>("guideStrip")->isVisible());
        QCOMPARE(w.guideCurrentStep(), 0);
        QVERIFY(w.guideHeadline().contains("vide"));
        // Des objets : la phase « Contrôler » est à traiter, le bouton principal lance le contrôle.
        (void)w.session().applyBatch(batchAddRect({-10, 0}, 20, 10), "A");
        w.waitForIdle();
        QCOMPARE(w.guideCurrentStep(), 2);
        QCOMPARE(w.guideNext(), NextAction::Analyse);
        QVERIFY(w.runGuideNext());
        w.waitForIdle();
        // Analyse sans erreur : ce n'est PAS une garantie machine, le contrôle machine est proposé
        // ; une fois passé, l'export ; une modification rouvre le contrôle.
        QCOMPARE(w.guideCurrentStep(), 3);
        QCOMPARE(w.guideNext(), NextAction::CheckMachine);
        QVERIFY(w.runGuideNext());
        w.waitForIdle();
        QCOMPARE(w.guideNext(), NextAction::Export);
        (void)w.session().applyBatch(batchAddEllipse({15, 0}, 12, 12), "B");
        w.waitForIdle();
        QCOMPARE(w.guideNext(), NextAction::Analyse);
        QVERIFY(w.guide().steps[2].state == StepState::Stale);
    }

    void hidingACalqueHidesItsObjectsFromThePreview() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        auto* panel = w.calquePanel();
        QVERIFY(panel->addNamed("Haut"));
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 20, 20), "R").report);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        panel->setCurrentRow(0); // « Haut » est au-dessus
        QVERIFY(panel->moveSelectionHere());
        w.waitForIdle();
        QVERIFY(w.previewFrame().hits->boundsOf(id).has_value());
        QVERIFY(panel->setRowVisible(0, false)); // masquer le calque
        w.waitForIdle();
        QVERIFY(!w.previewFrame().hits->boundsOf(id).has_value()); // ni dessiné ni cousu
        QVERIFY(panel->setRowVisible(0, true));
        w.waitForIdle();
        QVERIFY(w.previewFrame().hits->boundsOf(id).has_value());
    }

    void aFadeIsCreatedFromTheToolsMenuActionAndUndone() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 30, 20), "R").report);
        w.waitForIdle();
        QVERIFY(!w.runCommand("action.fade.create")); // rien de sélectionné
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QVERIFY(!w.runActionWith("fade.create", {{"foreground", "orange"}})); // format refusé
        QVERIFY(w.lastNotice().contains("#RRGGBB"));
        const auto before = w.session().project().embroidery_objects.size();
        QVERIFY(
            w.runActionWith("fade.create", {{"foreground", "#FF8800"}, {"complementary", "1"}}));
        QVERIFY(w.session().project().embroidery_objects.size() > before);
        QVERIFY(w.lastNotice().contains("fait"));
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
        w.waitForIdle();
        QVERIFY(w.previewShown());
    }

    void theEyedropperCopiesAThreadColourOntoTheSelection() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto a =
            *createdObjectId(w.session().applyBatch(batchAddRect({-20, 0}, 20, 20), "A").report);
        const auto b =
            *createdObjectId(w.session().applyBatch(batchAddRect({20, 0}, 20, 20), "B").report);
        w.session().applyBatch(batchSetColor({a}, {200, 30, 40}), "Couleur");
        w.waitForIdle();
        w.session().selection().replace({{TargetKind::EmbroideryObject, b}});
        QVERIFY(w.runCommand("tool.eyedropper"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Pick);
        const auto depth = w.session().undoNames().size();
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(*w.objectCentrePx(a)));
        QCOMPARE(w.session().project().embroidery_objects[1].rgb,
                 (std::array<std::uint8_t, 3>{200, 30, 40}));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.lastNotice().contains("#C81E28"));
        // Un clic sur le tissu ne change rien.
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(QPointF(2, 2)));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QVERIFY(w.lastNotice().contains("aucun fil"));
        // La sélection reste celle de l'utilisateur.
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
    }

    void theKnifeCutsTheSelectedShapeAlongTheDraggedLine() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 40, 20), "R").report);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.knife"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::Cut);
        const auto& f = w.previewFrame();
        const QPoint a = w.canvas()->mapFromScene(QPointF(f.mmToPxX(0), f.mmToPxY(-18)));
        const QPoint b = w.canvas()->mapFromScene(QPointF(f.mmToPxX(0), f.mmToPxY(18)));
        // Sans sélection : message, rien ne change.
        QTest::mousePress(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(w.canvas()->viewport(), (a + b) / 2);
        QTest::mouseRelease(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
        QVERIFY(w.lastNotice().contains("sélectionnez"));
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        QTest::mousePress(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(w.canvas()->viewport(), (a + b) / 2);
        QTest::mouseRelease(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
        QCOMPARE(QString::fromStdString(w.session().undoName()), QString("Découper"));
        QVERIFY(!w.canvas()->hasMeasure()); // la ligne de coupe disparaît après le geste
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), size_t(1));
    }

    void thePointToolMovesDeletesAndDiscardsStitchEdits() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        const auto id =
            *createdObjectId(w.session().applyBatch(batchAddRect({0, 0}, 30, 16), "R").report);
        w.waitForIdle();
        QVERIFY(w.runCommand("workspace.digitize"));
        QVERIFY(w.runCommand("tool.stitch.points"));
        // Sans sélection : pas de poignées.
        w.waitForIdle();
        QCOMPARE(w.canvas()->pointMarkerCount(), 0);
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        w.waitForIdle();
        QVERIFY(w.canvas()->pointMarkerCount() > 10);
        QCOMPARE(w.selectedPoint(), -1);
        // Cliquer sur la première poignée la sélectionne.
        const auto view = stitchPoints(w.session().project(), id);
        QVERIFY(view.has_value());
        const auto first = std::find_if(view->points.begin(), view->points.end(),
                                        [](const EditablePoint& p) { return p.movable; });
        QVERIFY(first != view->points.end());
        const auto& f = w.previewFrame();
        const QPoint at =
            w.canvas()->mapFromScene(QPointF(f.mmToPxX(first->pos.x), f.mmToPxY(-first->pos.y)));
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(w.selectedPoint(), 0);
        // Cliquer ailleurs la déplace : une étape d'annulation.
        const auto depth = w.session().undoNames().size();
        // Destination loin de toute poignée (sinon le clic sélectionnerait une autre poignée).
        QPoint dest;
        PointMm destMm{};
        for (const double cx : {-60.0, 60.0, -40.0, 40.0}) {
            const QPoint cand = w.canvas()->mapFromScene(QPointF(f.mmToPxX(cx), f.mmToPxY(0)));
            bool far = w.canvas()->viewport()->rect().contains(cand);
            for (const auto& p : view->points) {
                const QPoint q =
                    w.canvas()->mapFromScene(QPointF(f.mmToPxX(p.pos.x), f.mmToPxY(-p.pos.y)));
                far = far && (q - cand).manhattanLength() > 30;
            }
            if (far) {
                dest = cand;
                destMm = {cx, 0};
                break;
            }
        }
        QVERIFY2(!dest.isNull(), "no free spot on the canvas");
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, dest);
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(stitchPoints(w.session().project(), id)->state, PointEditState::ManuallyEdited);
        w.waitForIdle();
        // Suppression du point sélectionné.
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(QPointF(f.mmToPxX(destMm.x), f.mmToPxY(0))));
        QCOMPARE(w.selectedPoint() >= 0, true);
        QVERIFY(w.deleteSelectedPoint());
        QCOMPARE(w.session().undoNames().size(), depth + 2);
        // Effacer les retouches : retour à l'état propre.
        QVERIFY(w.discardPointEdits());
        QCOMPARE(stitchPoints(w.session().project(), id)->state, PointEditState::Clean);
        QVERIFY(!w.discardPointEdits()); // plus rien à effacer
        // Quitter l'outil retire les poignées.
        QVERIFY(w.runCommand("tool.select"));
        w.waitForIdle();
        QCOMPARE(w.canvas()->pointMarkerCount(), 0);
    }

    void theSatinColumnToolBuildsRailsFromPairsOfClicks() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new");
        w.waitForIdle();
        QVERIFY(w.runCommand("workspace.digitize"));
        QVERIFY(w.runCommand("tool.satin.column"));
        QVERIFY(w.canvas()->mode() == PreviewCanvas::Mode::DrawPolygon);
        const auto& f = w.previewFrame();
        auto at = [&](double x, double y) {
            return w.canvas()->mapFromScene(QPointF(f.mmToPxX(x), f.mmToPxY(y)));
        };
        const auto before = w.session().project().embroidery_objects.size();
        // Quatre clics = deux barreaux ; Entrée termine.
        for (const QPoint p : {at(-10, -2), at(-10, 2), at(10, -2), at(10, 2)}) {
            QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, p);
        }
        QTest::keyClick(w.canvas()->viewport(), Qt::Key_Return);
        QCOMPARE(w.session().project().embroidery_objects.size(), before + 1);
        QCOMPARE(w.session().selection().targets().size(), size_t(1));
        const auto id = w.session().selection().targets().front().id;
        QCOMPARE(railNodeCount(w.session().project(), id, RailSide::A), 2);
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
    }

    void clickToStitchEmbroidersAColourAreaOfTheSourceImage() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        QVERIFY(!w.sourceVisible());
        QVERIFY(w.runCommand("workspace.digitize")); // l'outil vit dans la Numérisation
        QVERIFY(w.runCommand("tool.click-to-stitch"));
        QVERIFY(w.sourceVisible()); // on ne clique pas une image invisible
        w.waitForIdle();
        QVERIFY(w.canvas()->hasSource());
        auto processed = processedImage(w.session().project());
        QVERIFY(processed.has_value());
        // Un pixel opaque sombre au milieu du motif.
        const double mmpp = w.session().project().mm_per_px.value;
        std::optional<std::pair<double, double>> mm;
        // Le clic passe par un pixel d'écran entier : on vise le milieu d'un bloc 15 x 15 sombre et
        // opaque, pas le bord d'une zone.
        const auto solid = [&](int cx, int cy) {
            for (int dy = -7; dy <= 7; ++dy) {
                for (int dx = -7; dx <= 7; ++dx) {
                    const int x = cx + dx;
                    const int y = cy + dy;
                    if (x < 0 || y < 0 || x >= processed->width || y >= processed->height) {
                        return false;
                    }
                    const auto* px =
                        &processed->rgba[(static_cast<size_t>(y) * processed->width + x) * 4];
                    if (!(px[3] > 250 && px[0] < 60 && px[1] < 80 && px[2] < 100)) {
                        return false;
                    }
                }
            }
            return true;
        };
        for (int y = processed->height / 2; y < processed->height && !mm; y += 3) {
            for (int x = processed->width / 3; x < 2 * processed->width / 3 && !mm; x += 3) {
                if (solid(x, y)) {
                    mm = pixelCentreMm(*processed, mmpp, {x, y});
                }
            }
        }
        QVERIFY(mm.has_value());
        const auto& f = w.previewFrame();
        const QPoint at =
            w.canvas()->mapFromScene(QPointF(f.mmToPxX(mm->first), f.mmToPxY(mm->second)));
        const auto before = w.session().project().embroidery_objects.size();
        const auto depth = w.session().undoNames().size();
        w.setClickToStitch(20, ClickKind::Fill);
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY2(w.session().project().embroidery_objects.size() == before + 1,
                 qPrintable(w.lastNotice()));
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        QCOMPARE(w.session().selection().targets().size(),
                 size_t(1)); // la zone brodée est sélectionnée
        QVERIFY(w.session().undo());
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
        // Hors de l'image : message, rien d'empilé.
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->mapFromScene(QPointF(f.mmToPxX(5000), f.mmToPxY(5000))));
        QVERIFY(w.lastNotice().contains("Clic-pour-broder"));
        QCOMPARE(w.session().project().embroidery_objects.size(), before);
    }

    void clickToStitchNeedsASourceImage() {
        ShellWindow w;
        w.show();
        w.runCommand("file.new"); // projet vierge : pas d'image
        w.waitForIdle();
        w.runCommand("workspace.digitize");
        QVERIFY(w.runCommand("tool.click-to-stitch"));
        QTest::mouseClick(w.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          w.canvas()->viewport()->rect().center());
        QVERIFY(w.lastNotice().contains("image"));
        QVERIFY(w.session().project().embroidery_objects.empty());
    }

    void theDetailBrushPaintsAndErasesWithOneUndoStepPerStroke() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        QVERIFY(w.runCommand("tool.detail-brush"));
        QVERIFY(w.sourceVisible());
        w.waitForIdle();
        QVERIFY(w.session().project().detail_mask.empty());
        auto* canvas = w.canvas();
        const QPoint a = canvas->viewport()->rect().center();
        const QPoint b = a + QPoint(40, 0);
        const auto depth = w.session().undoNames().size();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas->viewport(), a + QPoint(20, 0));
        QTest::mouseMove(canvas->viewport(), b);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QVERIFY(w.session().project().detail_mask.any_detail());
        QCOMPARE(w.session().undoNames().size(), depth + 1);
        w.waitForIdle();
        QVERIFY(canvas->hasDetailOverlay()); // le masque est dessiné sur l'image
        // Ctrl + trait au même endroit : gomme.
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, a);
        QTest::mouseMove(canvas->viewport(), b, 1);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, b);
        QCOMPARE(w.session().undoNames().size(), depth + 2);
        QVERIFY(w.session().undo());
        QVERIFY(w.session().project().detail_mask.any_detail());
        w.setBrushRadiusMm(100.0); // borné à 50 mm
    }

    void theSourceImageCanBeShownAndHiddenFromTheViewMenu() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        w.setSourceVisible(true);
        QVERIFY(w.canvas()->hasSource());
        QVERIFY(w.canvas()->sourceVisible());
        w.setSourceVisible(false);
        QVERIFY(!w.canvas()->hasSource() || !w.canvas()->sourceVisible());
        QVERIFY(w.findChild<QAction*>("action_view_source") != nullptr);
    }

    void autosaveRecoveryRecentFilesAndHistoryMenuWork() {
        const auto dir = std::filesystem::temp_directory_path() / "openstitch_v2_shell_state";
        std::filesystem::remove_all(dir);
        const auto osp = dir / "demo.osp";
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            QVERIFY(!w.hasRecovery());
            w.newProject();
            (void)w.session().applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle");
            (void)w.session().applyBatch(batchAddEllipse({10, 10}, 8, 8), "Ellipse");
            QVERIFY(w.autosaveNow());
            QVERIFY(w.hasRecovery());
            // historique : 2 entrées, la plus récente d'abord ; choisir la 2e annule 2 étapes
            auto* history = w.findChild<QMenu*>("menu_history");
            QVERIFY(history != nullptr);
            emit history->aboutToShow();
            QCOMPARE(history->actions().size(), 2);
            QCOMPARE(history->actions()[0]->text(), QStringLiteral("Ellipse"));
            history->actions()[1]->trigger();
            QCOMPARE(w.session().project().embroidery_objects.size(), size_t(0));
            (void)w.session().redo();
            (void)w.session().redo();
            QVERIFY(w.autosaveNow());
        } // fermeture « sale » : la sauvegarde reste
        {
            ShellWindow w;
            w.setStateDirectory(dir);
            QVERIFY(w.hasRecovery());
            QVERIFY(w.recoverAutosave());
            QCOMPARE(w.session().project().embroidery_objects.size(), size_t(2));
            QVERIFY(w.session().dirty());
            QVERIFY(w.saveProject(osp)); // enregistrer : plus rien à récupérer, récents mis à jour
            QVERIFY(!w.hasRecovery());
            QCOMPARE(w.recentFiles().size(), size_t(1));
            auto* recent = w.findChild<QMenu*>("menu_recent");
            QVERIFY(recent != nullptr);
            QCOMPARE(recent->actions().size(), 1);
        }
        {
            ShellWindow w; // nouvelle session : la liste persiste
            w.setStateDirectory(dir);
            QCOMPARE(w.recentFiles().size(), size_t(1));
            QVERIFY(!w.hasRecovery());
        }
        std::filesystem::remove_all(dir);
    }

    void alignGroupAndNumericPlacementWorkFromTheEditMenu() {
        ShellWindow w;
        w.newProject();
        auto& session = w.session();
        (void)session.applyBatch(batchAddRect({-30, 0}, 10, 10), "A");
        (void)session.applyBatch(batchAddRect({0, 15}, 20, 6), "B");
        (void)session.applyBatch(batchAddRect({25, -8}, 8, 12), "C");
        w.selectAllObjects();
        const auto depth = session.undoNames().size();
        QVERIFY(w.runCommand(QStringLiteral("arrange.align.left")));
        QCOMPARE(session.undoNames().size(), depth + 1);
        QVERIFY(w.runCommand(QStringLiteral("arrange.group")));
        QCOMPARE(session.project().groups.size(), size_t(1));
        QVERIFY(w.runCommand(QStringLiteral("arrange.ungroup")));
        QCOMPARE(session.project().groups.size(), size_t(0));
        QVERIFY(w.placeSelection(0.0, 0.0, 30.0, 30.0, 0.0));
        QVERIFY(!w.placeSelection(5000.0, 0.0, 30.0, 30.0, 0.0));
        QVERIFY(w.findChild<QMenu*>() != nullptr);
    }

    void imagePreparationStepsComeFromTheImageMenuAndTheCommandPalette() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto depth = w.session().project().ops.size();
        PrepRequest bc;
        bc.kind = PrepKind::BrightnessContrast;
        bc.a = 15;
        bc.b = 10;
        QVERIFY(w.applyPrep(bc));
        QVERIFY(w.runCommand(QStringLiteral("image.prep.gray")));
        QVERIFY(w.runCommand(QStringLiteral("image.prep.flip.h")));
        QCOMPARE(w.session().project().ops.size(), depth + 3);
        PrepRequest nothing;
        nothing.kind = PrepKind::BrightnessContrast;
        QVERIFY(!w.applyPrep(nothing));
        QCOMPARE(w.session().project().ops.size(), depth + 3);
        QVERIFY(w.findChild<QAction*>("action_image.prep.crop") != nullptr);
        ShellWindow empty;
        empty.newProject();
        QVERIFY(!empty.runCommand(QStringLiteral("image.prep.gray"))); // désactivée sans image
    }

    void theProductionSheetIsAPdfAndTheHelpListsTheCommands() {
        ShellWindow w;
        w.newProject();
        (void)w.session().applyBatch(batchAddRect({-10, 0}, 20, 10), "A");
        (void)w.session().applyBatch(batchAddEllipse({15, 0}, 12, 12), "B");
        w.waitForIdle();
        const auto pdf = std::filesystem::temp_directory_path() / "openstitch_v2_sheet_test.pdf";
        std::filesystem::remove(pdf);
        w.exportProductionSheet(pdf);
        w.waitForIdle();
        QVERIFY(std::filesystem::exists(pdf));
        QVERIFY(std::filesystem::file_size(pdf) > 1000);
        QFile file(QString::fromStdU16String(pdf.u16string()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(4), QByteArray("%PDF"));
        file.close();
        std::filesystem::remove(pdf);
        const QString help = w.helpText();
        QVERIFY(help.contains(QStringLiteral("Annuler")));
        QVERIFY(help.contains(QStringLiteral("Ctrl+Z")));
        QVERIFY(help.contains(QStringLiteral("Aligner")));
        QVERIFY(w.findChild<QAction*>("action_help_shortcuts") != nullptr);
        QVERIFY(w.findChild<QAction*>("action_file_production_sheet") != nullptr);
    }

    void inspectorEditsAllSelectedObjectsInOneUndoStep() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto& objects = w.session().project().embroidery_objects;
        QVERIFY(objects.size() >= 2);
        w.session().selection().replace({{TargetKind::EmbroideryObject, objects[0].id.value},
                                         {TargetKind::EmbroideryObject, objects[1].id.value}});
        auto* panel = w.inspector();
        QVERIFY(panel->applyColor(QColor(12, 200, 34)));
        QCOMPARE(w.session().project().embroidery_objects[0].rgb,
                 (std::array<std::uint8_t, 3>{12, 200, 34}));
        QCOMPARE(w.session().project().embroidery_objects[1].rgb,
                 (std::array<std::uint8_t, 3>{12, 200, 34}));
        QCOMPARE(w.session().undoNames().size(), size_t(1));
        QVERIFY(panel->applyStitchType("running"));
        QVERIFY(panel->applyLocked(true));
        QVERIFY(w.session().project().embroidery_objects[1].locked);
        // Un type refusé par le moteur n'empile rien.
        const auto depth = w.session().undoNames().size();
        QVERIFY(!panel->applyStitchType("satin"));
        QCOMPARE(w.session().undoNames().size(), depth);
        w.waitForIdle();
        QVERIFY(w.previewShown());
    }

    void inspectorWithoutSelectionDoesNothing() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        w.session().selection().clear();
        QVERIFY(!w.inspector()->applyColor(QColor(1, 2, 3)));
        QVERIFY(!w.session().canUndo());
    }

    void burstsOfEditsRenderFewerPreviewsThanEdits() {
        ShellWindow w;
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        const auto countPreviews = [&] {
            int n = 0;
            for (const auto& t : w.tasks().history()) {
                n += t.label == "Aperçu" ? 1 : 0;
            }
            return n;
        };
        const int before = countPreviews();
        const auto id = w.session().project().embroidery_objects.front().id.value;
        for (int i = 0; i < 6; ++i) {
            const std::string batch =
                R"({"schema":"openstitch.batch/1","operations":[{"op":"set_color","object":)" +
                std::to_string(id) + R"(,"rgb":"#0000)" + (i % 2 ? "FF" : "EE") + R"("}]})";
            QVERIFY(w.session().applyBatch(batch, "Couleur").committed);
        }
        w.waitForIdle();
        QVERIFY(countPreviews() - before <= 2);
        QVERIFY(w.previewShown());
    }

    void canvasTrackpadZoomAndKeyboardNavigationRemainBounded() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.canvas();
        const double initial = canvas->zoom();
        const QPoint at = canvas->viewport()->rect().center();
        const QPointF local(at);
        const QPointF global(canvas->viewport()->mapToGlobal(at));
        const auto buttons = Qt::NoButton;
        const auto modifiers = Qt::NoModifier;
        const auto phase = Qt::NoScrollPhase;
        QWheelEvent fine(local, global, QPoint(0, 24), QPoint(), buttons, modifiers, phase, false);
        QApplication::sendEvent(canvas->viewport(), &fine);
        QVERIFY(canvas->zoom() > initial); // trackpad : pixelDelta sans angleDelta
        const double afterTrackpad = canvas->zoom();
        QTest::keyClick(canvas, Qt::Key_Plus);
        QVERIFY(canvas->zoom() > afterTrackpad);
        QTest::keyClick(canvas, Qt::Key_Minus);
        QVERIFY(canvas->zoom() < afterTrackpad * 1.26);
        QWheelEvent huge(QPointF(at), QPointF(canvas->viewport()->mapToGlobal(at)),
                         QPoint(0, 50000), QPoint(), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas->viewport(), &huge);
        QVERIFY(canvas->zoom() <= 32.0 + 1e-6);
        QTest::keyClick(canvas, Qt::Key_F);
        QVERIFY(qAbs(canvas->zoom() - initial) < 1e-6);
    }

    void shiftFFramesSelectedObjectWithoutChangingTheProject() {
        ShellWindow w;
        w.show();
        w.newProject();
        const auto r = w.session().applyBatch(batchAddRect({0, 0}, 8, 8), "Petit carré");
        QVERIFY(r.committed);
        w.waitForIdle();
        auto& objects = w.session().project().embroidery_objects;
        QVERIFY(!objects.empty());
        const auto id = objects.front().id.value;
        w.session().selection().replace({{TargetKind::EmbroideryObject, id}});
        const auto revision = w.session().revision();
        const double whole = w.canvas()->zoom();
        QTest::keyClick(w.canvas(), Qt::Key_F, Qt::ShiftModifier);
        QVERIFY(w.canvas()->zoom() > whole);
        QCOMPARE(w.session().revision(), revision); // zoom = affichage uniquement
        QTest::keyClick(w.canvas(), Qt::Key_F);
        QVERIFY(qAbs(w.canvas()->zoom() - whole) < 1e-6);
    }

    void spaceTemporarilyPansWithoutInterruptingPolygonDrawing() {
        ShellWindow w;
        w.show();
        w.newProject();
        w.waitForIdle();
        auto* canvas = w.canvas();
        QVERIFY(canvas->hasImage());
        canvas->setMode(PreviewCanvas::Mode::DrawPolygon);
        const QPoint start = canvas->viewport()->rect().center();
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(canvas->polygonInProgress().size(), qsizetype(1));
        const auto depth = w.session().undoNames().size();

        QTest::keyPress(canvas, Qt::Key_Space);
        QVERIFY(canvas->temporaryPanActive());
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas->viewport(), start + QPoint(30, 15));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                            start + QPoint(30, 15));
        QTest::keyRelease(canvas, Qt::Key_Space);
        QVERIFY(!canvas->temporaryPanActive());
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::DrawPolygon);
        QCOMPARE(canvas->polygonInProgress().size(), qsizetype(1));
        QCOMPARE(w.session().undoNames().size(), depth);

        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(canvas->polygonInProgress().size(), qsizetype(2));
    }

    void middleMousePansInAnyToolWithoutModifyingDocument() {
        ShellWindow w;
        w.show();
        w.newProject();
        w.waitForIdle();
        auto* canvas = w.canvas();
        canvas->setMode(PreviewCanvas::Mode::DrawBox);
        canvas->scale(4.0, 4.0); // rend la scène plus large que le viewport
        auto* horizontal = canvas->horizontalScrollBar();
        QVERIFY(horizontal->maximum() > horizontal->minimum());
        horizontal->setValue((horizontal->maximum() + horizontal->minimum()) / 2);
        const auto before = horizontal->value();
        const auto depth = w.session().undoNames().size();
        const QPoint start = canvas->viewport()->rect().center();
        QTest::mousePress(canvas->viewport(), Qt::MiddleButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas->viewport(), start - QPoint(40, 20));
        QTest::mouseRelease(canvas->viewport(), Qt::MiddleButton, Qt::NoModifier,
                            start - QPoint(40, 20));
        QVERIFY(horizontal->value() != before);
        QCOMPARE(canvas->mode(), PreviewCanvas::Mode::DrawBox);
        QCOMPARE(w.session().undoNames().size(), depth);
    }

    void canvasZoomAnchorsUnderTheCursorAndFitRestores() {
        ShellWindow w;
        w.show();
        w.openImage(tentabrode(), 80.0);
        w.waitForIdle();
        auto* canvas = w.findChild<PreviewCanvas*>("previewCanvas");
        QVERIFY(canvas && canvas->hasImage());
        const double fit = canvas->zoom();
        const QPoint at = canvas->viewport()->rect().center() + QPoint(40, 30);
        const QPointF sceneBefore = canvas->mapToScene(at);
        QWheelEvent wheel(QPointF(at), QPointF(canvas->viewport()->mapToGlobal(at)), QPoint(),
                          QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas->viewport(), &wheel);
        QVERIFY(canvas->zoom() > fit);
        const QPointF sceneAfter = canvas->mapToScene(at);
        QVERIFY(qAbs(sceneAfter.x() - sceneBefore.x()) < 1.5);
        QVERIFY(qAbs(sceneAfter.y() - sceneBefore.y()) < 1.5);
        QTest::keyClick(canvas, Qt::Key_F);
        QVERIFY(qAbs(canvas->zoom() - fit) < 1e-6);
    }
};

QTEST_MAIN(ShellWindowTest)
#include "test_shell_window.moc"
