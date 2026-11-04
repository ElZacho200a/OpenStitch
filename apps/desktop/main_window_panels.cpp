// SPDX-License-Identifier: Apache-2.0
// Panneaux document, propriétés et inspecteur.
// Membres de MainWindow extraits de main_window.cpp (HP-QA-001) : déplacement pur.
#include "main_window.hpp"
#include "simulation_nav_bar.hpp"
#include "stitch_type_catalog.hpp"
#include "thread_strip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "ai_preferences.hpp"
#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"
#include "app_language.hpp"
#include "app_settings.hpp"
#include "app_theme.hpp"
#include "autodigitize_presets.hpp"
#include "autosave.hpp"
#include "brightness_dialog.hpp"
#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "command_palette_dialog.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "general_preferences_dialog.hpp"
#include "generation_options_dialog.hpp"
#include "help_dialogs.hpp"
#include "help_manual.hpp"
#include "import_dialog.hpp"
#include "length_units.hpp"
#include "machine_export_dialog.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/sewing_order_commands.hpp"
#include "order_list_widget.hpp"
#include "responsive.hpp"
#include "vector_tooltip.hpp"

#include "calque_panel.hpp"
#include "claude_code_process_backend.hpp"
#include "companion_settings_dialog.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/autodigitize/vector_convert.hpp"
#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/hoop_presets.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/export_limits.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/svg_import.hpp"
#include "openstitch/geometry/cut.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/machine_checks.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/satin_guides.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "properties_panel.hpp"
#include "recent_files.hpp"
#include "ruler.hpp"
#include "satin_guide_item.hpp"
#include "selection_hit_test.hpp"
#include "settings_dialog.hpp"
#include "ui_icons.hpp"
#include "ui_memory.hpp"
#include "ui_swatch.hpp"
#include "workflow_panel.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsPathItem>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "main_window_internal.hpp"

namespace openstitch::desktop {

void MainWindow::buildWorkflowPanel() {
    workflowDock_ = new QDockWidget(tr("Workflow"), this);
    workflowDock_->setObjectName(QStringLiteral("workflowDock"));
    workflowDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    workflowPanel_ = new WorkflowPanel(workflowDock_);
    workflowDock_->setWidget(scrollableDockContent(workflowPanel_, workflowDock_));
    if (documentDock_ != nullptr) {
        splitDockWidget(documentDock_, workflowDock_, Qt::Vertical); // sous Document
    } else {
        addDockWidget(Qt::LeftDockWidgetArea, workflowDock_);
    }
    connect(&AppTheme::instance(), &AppTheme::changed, workflowPanel_, &WorkflowPanel::applyTheme);
    connect(workflowPanel_, &WorkflowPanel::stepClicked, this, &MainWindow::onWorkflowStepClicked);
}

void MainWindow::refreshWorkflow() {
    if (workflowPanel_ == nullptr) {
        return;
    }
    using S = WorkflowPanel::State;
    std::array<S, WorkflowPanel::kStepCount> st{};
    const bool img = project_.hasImage();
    const bool seg = project_.segmentation.has_value();
    const bool vec = !project_.vector_objects.empty();
    const bool emb = !project_.embroidery_objects.empty();
    const bool imported = project_.imported_design.has_value();
    const bool seq = sequence_.has_value();
    // Source ≠ raster : un SVG peut fournir directement des vecteurs, et un DST/PES
    // directement des points. Les étapes inapplicables sont indiquées « non requis »
    // plutôt que « à faire » : elles ne doivent pas bloquer le parcours utilisateur.
    const bool hasSource = img || seg || vec || emb || imported;
    const bool machineOnly = imported && !img && !seg && !vec;

    st[0] = hasSource ? S::Done : S::NotStarted;
    if (!hasSource) {
        st[1] = S::NotStarted;
        st[2] = S::NotStarted;
    } else if (machineOnly) {
        st[1] = S::Skipped;
        st[2] = S::Skipped;
    } else {
        st[1] = !img ? S::Skipped : ((seg || vec) ? S::Done : S::Available);
        st[2] = vec ? S::Done : (seg ? S::Available : S::NotStarted);
    }
    st[3] = (emb || imported) ? S::Done : ((vec || seg) ? S::Available : S::NotStarted);
    st[4] = seq ? S::Available : S::NotStarted;
    st[5] = seq ? S::Available : S::NotStarted;
    workflowPanel_->setStates(st);
}

void MainWindow::buildDocumentPanel() {
    documentDock_ = new QDockWidget(tr("Document"), this);
    documentDock_->setObjectName(QStringLiteral("documentDock"));
    documentDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    documentPanel_ = new DocumentPanel(documentDock_);
    documentDock_->setWidget(documentPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, documentDock_);
    if (orderDock_ != nullptr) {
        tabifyDockWidget(documentDock_, orderDock_); // Document et Ordre en onglets
        documentDock_->raise();
    }

    connect(documentPanel_, &DocumentPanel::embroiderySelected, this, [this](ObjectId id) {
        Selection sel{.region = std::nullopt, .embroidery = id, .objects = {}};
        if (const auto* e = project_.findEmbroidery(id);
            e != nullptr && project_.findObject(e->source_vector) != nullptr) {
            sel.objects = {e->source_vector}; // met en évidence la forme au canevas
        }
        setSelection(std::move(sel));
        displayImage(processed_);
        updateActions();
    });
    connect(
        documentPanel_, &DocumentPanel::regionsSelected, this,
        [this](const std::vector<std::uint64_t>& ids, std::uint64_t active) {
            if (ids.size() < 2) {
                return; // une seule ligne : traitée par regionSelected (comportement historique)
            }
            std::vector<RegionId> others;
            for (const std::uint64_t id : ids) {
                if (id != active) {
                    others.push_back(RegionId{static_cast<std::uint32_t>(id)});
                }
            }
            setSelection({.region = RegionId{static_cast<std::uint32_t>(active)},
                          .embroidery = std::nullopt,
                          .objects = {},
                          .extraRegions = others});
            announceRegionSelection();
            displayImage(processed_);
            updateActions();
        });
    // Clic sur un groupe de sections : la forme source (donc toutes les sections) est visée.
    connect(documentPanel_, &DocumentPanel::groupSelected, this,
            [this](ObjectId sourceVector, int sections) {
                if (project_.findObject(sourceVector) == nullptr) {
                    return;
                }
                setSelection({.region = std::nullopt,
                              .embroidery = std::nullopt,
                              .objects = {sourceVector}});
                displayImage(processed_);
                updateActions();
                statusBar()->showMessage(
                    tr("Forme sélectionnée : %1 section(s) de couture.").arg(sections));
            });
    // Cases de la liste et renommage : commandes annulables (jamais de mutation directe).
    connect(documentPanel_, &DocumentPanel::visibilityToggled, this,
            [this](ObjectId id, bool visible) {
                undoStack_.execute(
                    std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible), project_);
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::orderLockToggled, this,
            [this](ObjectId id, bool locked) {
                undoStack_.execute(std::make_unique<commands::SetEmbroideryLockCommand>(id, locked),
                                   project_);
                refreshOrderPanel();
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::renameRequested, this,
            [this](ObjectId id, const QString& name) {
                undoStack_.execute(
                    std::make_unique<commands::RenameEmbroideryCommand>(id, name.toStdString()),
                    project_);
                refreshImage();
                updateActions();
            });
    // Calques du document (HP-OBJ-024) : pile au-dessus des onglets de la liste Document.
    {
        CalquePanel* calques = documentPanel_->calquePanel();
        connect(calques, &CalquePanel::addCalqueRequested, this, &MainWindow::addCalque);
        connect(calques, &CalquePanel::removeCalqueRequested, this, &MainWindow::removeCalque);
        connect(calques, &CalquePanel::renameCalqueRequested, this,
                &MainWindow::renameCalqueInteractive);
        connect(calques, &CalquePanel::moveSelectionToCalqueRequested, this,
                &MainWindow::moveSelectionToCalque);
        connect(calques, &CalquePanel::calqueActivated, this, [this](ObjectId id) {
            if (project_.findCalque(id) != nullptr) {
                project_.active_calque = id; // état d'interface : ni annulable ni enregistré
                refreshDocumentPanel();
                statusBar()->showMessage(
                    tr("Calque actif : « %1 ».")
                        .arg(QString::fromStdString(project_.findCalque(id)->name)));
            }
        });
        connect(calques, &CalquePanel::moveCalqueRequested, this, [this](ObjectId id, int dir) {
            undoStack_.execute(std::make_unique<commands::MoveCalqueCommand>(id, dir), project_);
            refreshImage();
            updateActions();
        });
        connect(calques, &CalquePanel::calqueVisibilityToggled, this,
                [this](ObjectId id, bool visible) {
                    undoStack_.execute(
                        std::make_unique<commands::SetCalqueVisibleCommand>(id, visible), project_);
                    refreshImage();
                    updateActions();
                });
        connect(calques, &CalquePanel::calqueLockToggled, this, [this](ObjectId id, bool locked) {
            undoStack_.execute(std::make_unique<commands::SetCalqueLockedCommand>(id, locked),
                               project_);
            pruneSelection();
            refreshImage();
            updateActions();
        });
    }
    // Couches de couture (HP-OBJ-023) : bouton « + Couche » et menu de ligne.
    connect(documentPanel_, &DocumentPanel::addLayerRequested, this,
            [this](ObjectId id, bool interactive) {
                if (interactive) {
                    addLayerInteractive(id);
                } else {
                    addLayerQuick(id);
                }
            });
    connect(documentPanel_, &DocumentPanel::duplicateLayerRequested, this,
            [this](ObjectId id) { duplicateLayer(id); });
    connect(documentPanel_, &DocumentPanel::regionSelected, this, [this](RegionId id) {
        setSelection({.region = id, .embroidery = std::nullopt, .objects = {}});
        displayImage(processed_);
        updateActions();
    });
    // Forme vectorielle non cousue (ligne indépendante de la liste).
    connect(documentPanel_, &DocumentPanel::vectorSelected, this, [this](ObjectId id) {
        if (project_.findObject(id) == nullptr) {
            return;
        }
        setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {id}});
        displayImage(processed_);
        updateActions();
    });
    // Ctrl/Maj + clic : plusieurs lignes = plusieurs formes dans la sélection (l'inspecteur
    // affiche « N objets »). Une broderie sans forme source valide ne peut pas rejoindre un lot.
    connect(documentPanel_, &DocumentPanel::objectsSelected, this,
            [this](const std::vector<ObjectId>& embroidery, const std::vector<ObjectId>& vectors) {
                std::vector<ObjectId> objects;
                for (const ObjectId id : embroidery) {
                    const auto* e = project_.findEmbroidery(id);
                    if (e != nullptr && project_.findObject(e->source_vector) != nullptr) {
                        objects.push_back(e->source_vector);
                    }
                }
                for (const ObjectId id : vectors) {
                    if (project_.findObject(id) != nullptr) {
                        objects.push_back(id);
                    }
                }
                if (objects.size() < 2 && !embroidery.empty()) {
                    setSelection({.region = std::nullopt,
                                  .embroidery = embroidery.back(),
                                  .objects = objects});
                } else {
                    setSelection({.region = std::nullopt,
                                  .embroidery = std::nullopt,
                                  .objects = std::move(objects)});
                }
                displayImage(processed_);
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::selectionCleared, this, [this] {
        setSelection({});
        displayImage(processed_);
        updateActions();
    });
    // Menu de ligne : mêmes commandes annulables que les cases et les boutons de l'Ordre.
    connect(documentPanel_, &DocumentPanel::visibilityChangeRequested, this,
            [this](const std::vector<ObjectId>& ids, bool visible) {
                auto composite = std::make_unique<commands::CompositeCommand>(
                    (visible ? tr("Afficher %1 objets") : tr("Masquer %1 objets"))
                        .arg(ids.size())
                        .toStdString());
                for (const ObjectId id : ids) {
                    composite->add(
                        std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible));
                }
                undoStack_.execute(std::move(composite), project_);
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::orderLockChangeRequested, this,
            [this](const std::vector<ObjectId>& ids, bool locked) {
                auto composite = std::make_unique<commands::CompositeCommand>(
                    (locked ? tr("Figer l'ordre de %1 objets") : tr("Libérer l'ordre de %1 objets"))
                        .arg(ids.size())
                        .toStdString());
                for (const ObjectId id : ids) {
                    composite->add(
                        std::make_unique<commands::SetEmbroideryLockCommand>(id, locked));
                }
                undoStack_.execute(std::move(composite), project_);
                refreshOrderPanel();
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::deleteRequested, this,
            [this](const std::vector<ObjectId>& embroidery, const std::vector<ObjectId>& vectors) {
                // Même règle que la suppression d'une broderie choisie : la broderie seule, sauf
                // proxy invisible (colonne satin manuelle) dont la suppression emporte la source.
                std::vector<ObjectId> removeVectors = vectors;
                std::vector<ObjectId> removeEmbroideries;
                for (const ObjectId id : embroidery) {
                    const auto* e = project_.findEmbroidery(id);
                    if (e == nullptr) {
                        continue;
                    }
                    const auto* source = project_.findObject(e->source_vector);
                    if (source != nullptr && !source->visible) {
                        removeVectors.push_back(source->id);
                    } else {
                        removeEmbroideries.push_back(id);
                    }
                }
                const std::size_t total = removeVectors.size() + removeEmbroideries.size();
                if (total == 0) {
                    return;
                }
                auto composite = std::make_unique<commands::CompositeCommand>(
                    tr("Supprimer %1 objets").arg(total).toStdString());
                for (const ObjectId id : removeEmbroideries) {
                    composite->add(removeEmbroideryCommand(id)); // un dégradé part en entier
                }
                for (std::size_t i = 0; i < removeVectors.size(); ++i) {
                    if (std::find(removeVectors.begin(),
                                  removeVectors.begin() + static_cast<std::ptrdiff_t>(i),
                                  removeVectors[i]) ==
                        removeVectors.begin() + static_cast<std::ptrdiff_t>(i)) {
                        composite->add(std::make_unique<commands::RemoveVectorObjectCommand>(
                            removeVectors[i]));
                    }
                }
                undoStack_.execute(std::move(composite), project_);
                setSelection({});
                refreshImage();
                updateActions();
                statusBar()->showMessage(tr("%1 objet(s) supprimé(s).").arg(total));
            });
    // Glisser-déposer : nouvel ordre complet via la commande des boutons Monter/Descendre.
    connect(documentPanel_, &DocumentPanel::orderChangeRequested, this,
            [this](const std::vector<ObjectId>& order) {
                if (order.size() != project_.embroidery_objects.size()) {
                    return; // liste périmée : le document fait foi
                }
                undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order),
                                   project_);
                refreshImage();
                refreshOrderPanel();
                updateActions();
            });
}

bool MainWindow::debugMenuEnabled() {
    return qEnvironmentVariable("OPENSTITCH_DEBUG") == QLatin1String("1") ||
           QSettings().value(QStringLiteral("debug/menu"), false).toBool();
}

void MainWindow::setDockAutoVisible(QDockWidget* dock, bool visible, bool force) {
    if (dock == nullptr) {
        return;
    }
    // Un rafraîchissement n'affiche un dock que lors de la transition vide -> non vide
    // (premier contenu) : ensuite, la visibilité appartient à l'utilisateur (croix du dock,
    // menu Affichage > Panneaux). `force` : demande explicite de l'utilisateur (Analyse).
    const bool hadContent = dockHadContent_.value(dock, false);
    dockHadContent_[dock] = visible;
    const bool show = visible && (force || !hadContent);
    if (!hidePanelsMode_) {
        if (show) {
            dock->show();
        } else if (!visible) {
            dock->hide();
        }
        return;
    }
    // Mode canevas seul : on ne touche pas à l'affichage, mais on tient la liste de
    // restauration à jour pour que la sortie du mode reflète l'état voulu.
    const auto it = std::find(panelsToRestore_.begin(), panelsToRestore_.end(), dock);
    if (show && it == panelsToRestore_.end()) {
        panelsToRestore_.push_back(dock);
    } else if (!visible && it != panelsToRestore_.end()) {
        panelsToRestore_.erase(it);
    }
}

void MainWindow::refreshDocumentPanel() {
    if (documentPanel_ == nullptr) {
        return;
    }
    documentPanel_->refresh(project_, editStates_);
    setDockAutoVisible(documentDock_, project_.hasImage() || !project_.embroidery_objects.empty());
    syncDocumentSelection();
}

void MainWindow::syncDocumentSelection() {
    if (documentPanel_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    if (emb != nullptr) {
        documentPanel_->syncSelection(DocumentPanel::Kind::Embroidery, emb->id.value);
    } else if (selectedRegion_) {
        std::vector<std::uint64_t> ids;
        for (const RegionId r : selectedRegionIds()) {
            ids.push_back(r.value);
        }
        documentPanel_->syncRegions(ids, selectedRegion_->value);
    } else if (hasMultiSelection()) {
        std::vector<std::uint64_t> ids;
        for (const ObjectId id : selectedObjectIds()) {
            ids.push_back(id.value);
        }
        documentPanel_->syncObjects(ids);
    } else if (selectedObject_) {
        // Forme sans broderie : ligne indépendante de la liste (vide si l'objet est masqué
        // ailleurs).
        documentPanel_->syncSelection(DocumentPanel::Kind::Vector, selectedObject_->value);
    } else {
        documentPanel_->syncSelection(DocumentPanel::Kind::None, 0);
    }
}

void MainWindow::buildPropertiesPanel() {
    propertiesDock_ = new QDockWidget(tr("Propriétés"), this);
    propertiesDock_->setObjectName(QStringLiteral("propertiesDock"));
    propertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesPanel_ = new PropertiesPanel(propertiesDock_);
    // Zone défilante : un inspecteur de 20 champs ne doit pas imposer sa hauteur à la
    // fenêtre (hauteur minimale mesurée : 934 px avec un satin sélectionné).
    auto* propertiesScroll = new QScrollArea(propertiesDock_);
    propertiesScroll->setWidgetResizable(true);
    propertiesScroll->setFrameShape(QFrame::NoFrame);
    propertiesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propertiesScroll->setWidget(propertiesPanel_);
    propertiesDock_->setWidget(propertiesScroll);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    // Édition d'un paramètre -> commande annulable -> régénération.
    // Boutons de l'inspecteur des régions : déclenchent l'action de menu du même nom (même état,
    // même grisage, même annulation).
    connect(propertiesPanel_, &PropertiesPanel::regionActionRequested, this,
            [this](const QString& actionName) {
                if (auto* act = findChild<QAction*>(actionName);
                    act != nullptr && act->isEnabled()) {
                    act->trigger();
                }
            });
    connect(propertiesPanel_, &PropertiesPanel::paramsEdited, this,
            [this](ObjectId id, document::StitchParams params, QString field) {
                // Auto-satin : l'inspecteur n'édite que les réglages scalaires ; les
                // guides (aussi posés depuis le canevas), l'entrée/sortie et les champs
                // sans widget viennent du document ACTUEL, jamais d'une copie périmée.
                if (auto* edited = std::get_if<document::AutoSatinParams>(&params)) {
                    if (const auto* current = project_.findEmbroidery(id)) {
                        if (const auto* cur =
                                std::get_if<document::AutoSatinParams>(&current->params)) {
                            edited->guides = cur->guides;
                            edited->entry_point = cur->entry_point;
                            edited->exit_point = cur->exit_point;
                            edited->push_start = cur->push_start;
                            edited->push_end = cur->push_end;
                            edited->lock_length = cur->lock_length;
                            edited->lock_passes = cur->lock_passes;
                        }
                    }
                }
                // HP-UX-029 : une modification qui rend des retouches manuelles obsolètes est
                // signalée AVANT d'être appliquée (bandeau non modal, annulable).
                if (const auto* current = project_.findEmbroidery(id);
                    current != nullptr && !(current->params == params)) {
                    noteOverridesInvalidated(id, current->params);
                }
                // Le champ nomme le pas d'historique et sert de clé de fusion : une
                // rafale (molette, flèches du spinbox) reste un seul pas d'annulation.
                // La commande est appliquée TOUT DE SUITE ; seule la régénération des points
                // est différée (HP-PERF-009).
                undoStack_.execute(std::make_unique<commands::SetStitchParamsCommand>(
                                       id, std::move(params), field.toStdString()),
                                   project_);
                // Le formulaire correspond désormais au document (guides repris du
                // document pour l'auto-satin) : pas de reconstruction ni de perte de focus.
                if (const auto* now = project_.findEmbroidery(id)) {
                    propertiesPanel_->adoptParams(id, now->params);
                    pendingCreationMemory_ = now->params; // dernier réglage, mémorisé au débounce
                }
                // scheduleParamsRefresh() appelle updateActions() (pas updateInspector() seul) :
                // un changement de paramètres peut faire passer un objet retouché à Dirty
                // (Lot 8.2) -- undo/redo, mode d'édition des points et indicateurs doivent
                // suivre. Ne reconstruit PAS le formulaire de paramètres (qui perdrait le
                // focus en cours de frappe) : le garde-fou kind==inspectedKind_/id==
                // inspectedId_ dans updateInspector() s'applique toujours. La régénération
                // des points (et donc l'état Clean/ManuallyEdited/Dirty) suit 150 ms après la
                // dernière édition, hors du fil d'interface.
                scheduleParamsRefresh();
                refreshOverrideWarning();
            });
    connect(propertiesPanel_, &PropertiesPanel::discardOverridesRequested, this,
            &MainWindow::discardOverrides);
    connect(propertiesPanel_, &PropertiesPanel::convertToDirectionalRequested, this,
            &MainWindow::convertToDirectional);
    connect(propertiesPanel_, &PropertiesPanel::convertToContourFillRequested, this,
            &MainWindow::convertToContourFill);
    connect(propertiesPanel_, &PropertiesPanel::convertToFillKindRequested, this,
            &MainWindow::convertToFillKind);
    connect(propertiesPanel_, &PropertiesPanel::createBorderSatinRequested, this,
            &MainWindow::createBorderSatin);
    connect(propertiesPanel_, &PropertiesPanel::borderSatinEdited, this,
            &MainWindow::editBorderSatin);
    connect(propertiesPanel_, &PropertiesPanel::joinModeEdited, this, &MainWindow::setJoinMode);
    connect(propertiesPanel_, &PropertiesPanel::connectorModeEdited, this,
            &MainWindow::setConnectorMode);
    connect(propertiesPanel_, &PropertiesPanel::knockoutEdited, this, &MainWindow::setKnockout);
    connect(propertiesPanel_, &PropertiesPanel::densityGradientRequested, this,
            &MainWindow::setDensityGradient);
    connect(
        propertiesPanel_, &PropertiesPanel::editSatinGuidesRequested, this, [this](ObjectId id) {
            if (auto* emb = project_.findEmbroidery(id); emb != nullptr && emb->is_auto_satin()) {
                editSelection([id](Selection& sel) { sel.embroidery = id; });
                if (directionGuideModeAct_->isChecked()) {
                    directionGuideModeAct_->setChecked(false);
                }
                directionGuideModeAct_->setChecked(true);
                if (directionGuideTarget_) {
                    setTool(Tool::DrawDirectionGuide);
                }
            }
        });
    connect(propertiesPanel_, &PropertiesPanel::satinGuideChangeRequested, this,
            &MainWindow::changeAutoSatinGuide);
    connect(propertiesPanel_, &PropertiesPanel::satinGuideSelected, this,
            &MainWindow::highlightAutoSatinGuide);
    connectInspectorEditing();
    connect(propertiesPanel_, &PropertiesPanel::satinGuideRemoveRequested, this,
            &MainWindow::removeAutoSatinGuide);
    connect(propertiesPanel_, &PropertiesPanel::compareSatinMethodsRequested, this,
            &MainWindow::compareAutoSatinMethods);
    // Barre du sélecteur de couches : mêmes commandes annulables que la liste Document.
    connect(propertiesPanel_, &PropertiesPanel::layerMoveRequested, this,
            [this](ObjectId id, int direction) { moveLayer(id, direction); });
    connect(propertiesPanel_, &PropertiesPanel::layerVisibilityRequested, this,
            [this](ObjectId id, bool visible) {
                undoStack_.execute(
                    std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible), project_);
                refreshImage();
                updateActions();
            });
    connect(propertiesPanel_, &PropertiesPanel::layerRenameRequested, this,
            &MainWindow::renameLayerInteractive);
    connect(propertiesPanel_, &PropertiesPanel::editDirectionGuidesRequested, this,
            [this](ObjectId id) {
                if (auto* emb = project_.findEmbroidery(id);
                    emb != nullptr && emb->is_directional()) {
                    editSelection([id](Selection& sel) { sel.embroidery = id; });
                    if (directionGuideModeAct_->isChecked()) {
                        directionGuideModeAct_->setChecked(false);
                    }
                    directionGuideModeAct_->setChecked(true);
                }
            });
}

void MainWindow::updateInspector() {
    if (propertiesPanel_ == nullptr) {
        return;
    }
    // Cible : objet de broderie (dock Ordre) > remplissage rattaché à l'objet
    // vectoriel sélectionné > objet vectoriel > région > rien.
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();

    QList<PropertiesPanel::FillLayer> layers;
    if (emb != nullptr) {
        for (const auto& layer : project_.embroidery_objects) {
            if (layer.source_vector == emb->source_vector) {
                layers.push_back(
                    {layer.id,
                     tr("%1 · %2").arg(layers.size() + 1).arg(QString::fromStdString(layer.name)),
                     QColor(layer.rgb[0], layer.rgb[1], layer.rgb[2]), layer.visible});
            }
        }
    }
    propertiesPanel_->setFillLayers(layers, emb ? std::optional<ObjectId>{emb->id} : std::nullopt);
    propertiesPanel_->setBlendActions(twoColorBlendAct_, blendZonesAct_, !layers.empty(),
                                      hasMultiSelection());
    updateGradientPanel();

    int kind = -1;
    std::uint64_t id = 0;
    if (emb != nullptr) {
        kind = 0;
        // Clé : l'objet ET sa couleur (la pastille de l'inspecteur doit suivre un changement de
        // couleur ou de fil, annulation comprise).
        id = (emb->id.value << 24) ^ (static_cast<std::uint64_t>(emb->rgb[0]) << 16) ^
             (static_cast<std::uint64_t>(emb->rgb[1]) << 8) ^
             static_cast<std::uint64_t>(emb->rgb[2]);
    } else if (hasMultiSelection()) {
        kind = 3; // « N objets » : type de points commun et réglages de groupe
        // Clé : la sélection ET le type de chaque couture (un changement de type regroupe ou
        // dégroupe le formulaire).
        std::uint64_t key = 1469598103934665603ULL;
        const auto mix = [&key](std::uint64_t v) { key = (key ^ v) * 1099511628211ULL; };
        for (const ObjectId vectorId : multiSelection_) {
            mix(vectorId.value);
            for (const auto& e : project_.embroidery_objects) {
                if (e.source_vector == vectorId) {
                    mix(e.id.value);
                    mix(e.params.index());
                }
            }
        }
        id = key;
    } else if (selectedObject_) {
        kind = 1;
        id = selectedObject_->value;
    } else if (selectedRegion_ && project_.segmentation) {
        kind = 2;
        // Clé : toutes les régions sélectionnées ET la couleur de l'active (un recolorage ou une
        // fusion doit reconstruire la pastille et les boutons, sans toucher aux autres
        // inspecteurs).
        std::uint64_t key = 1469598103934665603ULL;
        const auto mix = [&key](std::uint64_t v) { key = (key ^ v) * 1099511628211ULL; };
        for (const RegionId r : selectedRegionIds()) {
            mix(r.value);
        }
        if (const auto* active = project_.segmentation->find(*selectedRegion_)) {
            mix(active->rgb[0]);
            mix(active->rgb[1]);
            mix(active->rgb[2]);
        }
        id = key;
    }

    // Indicateur Clean/ManuallyEdited/Dirty (Lot 8.2) : mis à jour à CHAQUE
    // appel, y compris quand kind/id n'ont pas changé (une retouche/undo/redo
    // peut faire évoluer l'état sans changer la sélection) -- volontairement
    // AVANT le "ne reconstruit que si la sélection a changé" ci-dessous, pour
    // ne jamais dépendre de lui.
    propertiesPanel_->setEditState(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                   emb != nullptr ? editStateOf(emb->id)
                                                  : stitch_generation::ObjectEditState::Clean,
                                   emb != nullptr ? static_cast<int>(emb->overrides.size()) : 0);
    refreshOverrideWarning(); // HP-UX-029 : suit annulation, rétablissement et abandon
    propertiesPanel_->setJoinMode(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                  emb != nullptr ? emb->join : document::JoinMode::Inherit);
    propertiesPanel_->setConnectorMode(
        emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
        emb != nullptr ? emb->connector : document::ConnectorMode::Inherit);
    propertiesPanel_->setKnockout(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                  emb != nullptr && emb->knockout,
                                  emb != nullptr ? emb->knockout_margin.value / 1000.0 : 0.3);

    // Auto-satin : liste des guides et diagnostic mis à jour à chaque appel (les
    // guides se posent aussi depuis le canevas), sans reconstruire le formulaire.
    if (emb != nullptr && emb->is_auto_satin()) {
        propertiesPanel_->setAutoSatinState(
            emb->id, &std::get<document::AutoSatinParams>(emb->params), autoSatinSummary(*emb));
    }

    // Ne reconstruit que si la sélection a changé (n'interrompt pas une édition) OU si
    // les paramètres du document ne sont plus ceux que montre le formulaire (annulation,
    // changement de type de points qui garde le même id, rotation au canevas...) : sinon
    // le premier champ touché écraserait le document avec des valeurs périmées.
    bool formStale = kind == 0 && !propertiesPanel_->showsParams(emb->params);
    if (kind == 3) {
        // Groupe de même type : le formulaire montre le premier ; annulation => reconstruction.
        const EmbroideryGroup group = selectedEmbroideryGroup();
        const auto* rep = group.sameType ? project_.findEmbroidery(group.ids.front()) : nullptr;
        formStale = rep != nullptr && !propertiesPanel_->showsGroupParams(rep->params);
        // HP-UX-038 : une valeur d'un AUTRE objet du groupe (annulation, glisser…) change
        // l'affichage « — » : le formulaire est reconstruit quand les valeurs mixtes ont changé.
        if (!formStale && rep != nullptr) {
            std::vector<document::StitchParams> members;
            for (const ObjectId memberId : group.ids) {
                if (const auto* m = project_.findEmbroidery(memberId)) {
                    members.push_back(m->params);
                }
            }
            formStale = !propertiesPanel_->showsGroupMembers(members);
        }
    }
    if (kind == 1) {
        // Boîte X/Y/L/H : reconstruite si la forme a changé (annulation, glisser, flèches...).
        const auto* current = project_.findObject(*selectedObject_);
        const auto box = current != nullptr ? vectorBoxMm(*current) : std::nullopt;
        formStale = box && !propertiesPanel_->showsVectorBox(*selectedObject_, *box);
    }
    if (kind == inspectedKind_ && id == inspectedId_ && !formStale) {
        return;
    }
    inspectedKind_ = kind;
    inspectedId_ = id;

    if (kind == 0) {
        propertiesPanel_->showEmbroidery(*emb);
        if (emb->is_auto_satin()) {
            propertiesPanel_->setAutoSatinState(
                emb->id, &std::get<document::AutoSatinParams>(emb->params), autoSatinSummary(*emb));
        }
    } else if (kind == 3) {
        int withStitches = 0;
        for (const ObjectId vectorId : multiSelection_) {
            if (embroideryForVector(vectorId) != nullptr) {
                ++withStitches;
            }
        }
        const EmbroideryGroup group = selectedEmbroideryGroup();
        const auto* rep = group.sameType ? project_.findEmbroidery(group.ids.front()) : nullptr;
        std::vector<document::StitchParams> members;
        if (rep != nullptr) {
            for (const ObjectId memberId : group.ids) {
                if (const auto* m = project_.findEmbroidery(memberId)) {
                    members.push_back(m->params);
                }
            }
        }
        propertiesPanel_->showMultiSelection(static_cast<int>(multiSelection_.size()), withStitches,
                                             rep, static_cast<int>(group.ids.size()), members);
    } else if (kind == 1) {
        const auto* vec = project_.findObject(*selectedObject_);
        int nodes = 0;
        if (vec != nullptr) {
            for (const auto& set : vec->paths) {
                nodes += static_cast<int>(set.outer.nodes.size());
                for (const auto& h : set.holes)
                    nodes += static_cast<int>(h.nodes.size());
            }
        }
        const QString details =
            tr("Objet vectoriel — %1 morceau(x), %2 nœud(s).\n"
               "Créez un objet de broderie (menu Broderie) pour régler la couture.")
                .arg(vec != nullptr ? vec->paths.size() : 0)
                .arg(nodes);
        const auto box = vec != nullptr ? vectorBoxMm(*vec) : std::nullopt;
        if (vec != nullptr && box) {
            propertiesPanel_->showVectorObject(vec->id, QString::fromStdString(vec->name), details,
                                               *box);
        } else {
            propertiesPanel_->showInfo(vec != nullptr ? QString::fromStdString(vec->name)
                                                      : tr("Objet vectoriel"),
                                       details);
        }
    } else if (kind == 2) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const std::vector<RegionId> picked = selectedRegionIds();
            std::size_t pixels = 0;
            for (const RegionId r : picked) {
                if (const auto* rr = project_.segmentation->find(r)) {
                    pixels += rr->pixel_count;
                }
            }
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 = static_cast<double>(pixels) * mmPerPx * mmPerPx;
            PropertiesPanel::RegionSelectionInfo info;
            info.activeColor = QColor(region->rgb[0], region->rgb[1], region->rgb[2]);
            info.canMerge = picked.size() >= 2;
            info.canAbsorb = picked.size() == 1;
            if (picked.size() == 1) {
                const auto neighbours =
                    segmentation::neighbors_of(*project_.segmentation, region->id);
                info.title = tr("Région %1").arg(region->id.value);
                info.summary = tr("Aire : %1 mm²   ·   %2 pixels\nVoisines : %3")
                                   .arg(areaMm2, 0, 'f', 1)
                                   .arg(pixels)
                                   .arg(neighbours.size());
            } else {
                info.title = tr("%1 régions").arg(picked.size());
                info.summary = tr("Aire totale : %1 mm²   ·   %2 pixels\nRégion active : %3 "
                                  "(la dernière cliquée)")
                                   .arg(areaMm2, 0, 'f', 1)
                                   .arg(pixels)
                                   .arg(region->id.value);
            }
            propertiesPanel_->showRegions(info);
        }
    } else {
        propertiesPanel_->showInfo(
            tr("Aucune sélection"),
            tr("Sélectionnez une région, un objet vectoriel ou un objet de broderie."));
    }
}

Vec2um MainWindow::embroideryCentroid(const document::EmbroideryObject& object) const {
    // Satin : moyenne des nœuds des deux rails. Autres : centre du contour
    // extérieur du premier morceau de l'objet vectoriel source.
    std::int64_t sx = 0;
    std::int64_t sy = 0;
    std::int64_t n = 0;
    const auto accumulate = [&](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            sx += node.pos.x.value;
            sy += node.pos.y.value;
            ++n;
        }
    };
    if (const auto* satin = std::get_if<document::SatinParams>(&object.params)) {
        accumulate(satin->rail_a);
        accumulate(satin->rail_b);
    } else {
        for (const auto& vec : project_.vector_objects) {
            if (vec.id == object.source_vector && !vec.paths.empty()) {
                accumulate(vec.paths.front().outer);
                break;
            }
        }
    }
    if (n == 0) {
        return {};
    }
    return Vec2um{Micrometers{static_cast<std::int32_t>(sx / n)},
                  Micrometers{static_cast<std::int32_t>(sy / n)}};
}

} // namespace openstitch::desktop
