// SPDX-License-Identifier: Apache-2.0
// Rendu de l'image, de la scène et des points.
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

void MainWindow::refreshImage() {
    // HP-PERF-009 : ce rafraîchissement couvre toute édition différée en attente et rend caduc un
    // calcul hors fil encore en route (sa série ne correspondra plus).
    ++refreshSerial_;
    ++refreshCount_;
    paramsRefreshPending_ = false;
    if (paramsRefreshTimer_ != nullptr) {
        paramsRefreshTimer_->stop();
    }
    commitCreationMemory();
    applyCanvasToView(); // taille du cadre (peut avoir changé : réglage, undo, chargement)
    // Marqueur « modifié » dérivé de la pile d'annulation (et non forcé à true) :
    // un undo qui revient à l'état enregistré rend le document propre.
    setWindowModified(!undoStack_.isClean());
    // Une sélection qui ne correspond plus à une région, un objet vectoriel ou
    // de broderie vivant est élaguée (undo/redo, nouvelle segmentation,
    // suppression, rechargement) ; le dernier objet restant devient le principal.
    pruneSelection();
    Q_ASSERT(checkSelectionInvariants());
    refreshColorsPanel(); // le document a changé : liste des teintes à jour, aperçu caduc
    refreshDetailZonesOverlay();
    // Le raster est facultatif : SVG, dessin, texte et design machine doivent
    // eux aussi régénérer leurs points et rafraîchir tous les panneaux.
    if (!project_.hasImage()) {
        processed_ = {};
        processedSource_ = {};
        processedOps_.clear();
    } else {
        // Clé de contenu exacte : une édition vectorielle ne rejoue pas le
        // pipeline raster tant que l'image source et ses opérations sont identiques.
        const bool processedUpToDate =
            !processed_.empty() && processedOps_ == project_.ops &&
            processedSource_.width == project_.original.width &&
            processedSource_.height == project_.original.height &&
            processedSource_.source_had_alpha == project_.original.source_had_alpha &&
            processedSource_.rgba == project_.original.rgba;
        if (!processedUpToDate) {
            const auto result = image::apply_pipeline(project_.original, project_.ops);
            if (!result) {
                showFriendlyError(this, tr("Traitement de l'image impossible"),
                                  tr("Une des opérations appliquées à l'image a échoué."),
                                  result.error().message);
                return;
            }
            processed_ = *result;
            processedSource_ = project_.original;
            processedOps_ = project_.ops;
        }
    }

    // Régénération des points depuis le document (fonction pure). Un design
    // importé (AD-04, `project_.imported_design`) n'a pas d'objet à
    // régénérer : `generate_sequence` le restitue tel quel (inscription
    // S2a), donc le même appel ci-dessous couvre les deux cas. État
    // Clean/ManuallyEdited/Dirty par objet retouché (Lot 8.2) et vue brute de
    // l'objet en cours d'édition (Lot 8.2, source unique pour le placement
    // des poignées dans renderBase et la construction des commandes) : tenus
    // à jour ici. Ni displayImage() ni updateActions() ne les recalculent :
    // ils lisent editStates_/stitchEditView_.
    editStates_.clear();
    sequence_.reset();
    stitchEditView_.reset();
    if (!project_.embroidery_objects.empty() || project_.imported_design) {
        // refresh_context (pas effective_sequence + classify_all_edit_states +
        // edit_view séparément) : un seul appel interne à generate_sequence
        // produit à la fois la séquence effective -- retouches appliquées,
        // contenu identique à effective_sequence(project), seul point d'entree
        // autorise pour l'affichage/export (cf.
        // tests/check_no_raw_sequence_bypass.cmake) --, les états par objet et
        // la vue d'édition de l'objet ciblé, au lieu de jusqu'à trois
        // régénérations indépendantes après chaque glisser sur un motif dense.
        // stitchEditTarget_ ne peut être défini que sur un objet du document
        // (cf. onStitchEditModeToggled/resolveSelectedEmbroidery) : inutile de
        // rien calculer ici quand embroidery_objects est vide ou qu'une
        // séquence importée est affichée -- editStates_/stitchEditView_
        // resteraient de toute façon vides/absents dans ces cas.
        const auto adopt = [this](stitch_generation::RefreshContext&& ctx) {
            sequence_ = std::move(ctx.effective);
            editStates_ = std::move(ctx.edit_states);
            if (ctx.target_view &&
                ctx.target_view->state != stitch_generation::ObjectEditState::Dirty) {
                stitchEditView_ = std::move(*ctx.target_view);
            }
        };
        if (precomputedContext_) {
            // Calculé hors du fil d'interface par la même fonction, sur le même document
            // (HP-PERF-009) : sortie identique à la voie synchrone ci-dessous.
            adopt(std::move(*precomputedContext_));
            precomputedContext_.reset();
        } else if (auto ctx = stitch_generation::refresh_context(project_, stitchEditTarget_)) {
            adopt(std::move(*ctx));
        } else {
            statusBar()->showMessage(tr("Génération des points impossible : %1")
                                         .arg(QString::fromStdString(ctx.error().message)));
        }
    }
    if (simToolbar_ != nullptr) {
        updateSimulationRange();
    }
    markAnalysisStale(); // l'Analyse affichée ne décrit plus le motif : signalée, puis refaite
    if (orderDock_ != nullptr) {
        refreshOrderPanel();
    }
    if (filterDock_ != nullptr) {
        refreshFilterPanel();
    }
    refreshThreadPanel();
    refreshDocumentPanel();
    displayImage(processed_);
}

void MainWindow::displayImage(const image::Image& img) {
    renderBase(img);
    renderStitches();
}

void MainWindow::renderBase(const image::Image& img) {
    // Retire uniquement la couche base ; la couche points est gérée à part.
    for (QGraphicsItem* it : baseItems_) {
        scene_->removeItem(it);
        delete it;
    }
    baseItems_.clear();
    // Les aperçus/surbrillances temporaires appartenaient à la couche base : détruits ci-dessus.
    guideHighlight_ = nullptr;
    snapIndicator_ = nullptr;
    resizePreview_ = nullptr;
    rotationPreview_ = nullptr;
    commonBox_ = nullptr;
    reshapePreview_ = nullptr;
    brushRing_ = nullptr;
    detailRing_ = nullptr;
    nodeHandles_.clear();
    if (nodeSelObject_ &&
        (hasMultiSelection() || !selectedObject_ || *selectedObject_ != *nodeSelObject_ ||
         project_.findObject(*nodeSelObject_) == nullptr)) {
        clearNodeSelection(); // la sélection de nœuds suit la forme courante
    }
    hoverCacheValid_ = false; // les contours ont pu changer
    hideHoverHighlight();     // la sélection/les objets ont pu changer

    if (!img.empty() && (showImageAct_ == nullptr || showImageAct_->isChecked())) {
        // Empreinte exacte du contenu (FNV-1a par mots de 64 bits, ~3 ms pour 9 Mo) : la
        // conversion en pixmap n'est refaite que si l'image a vraiment changé.
        std::uint64_t key = 1469598103934665603ULL ^ (static_cast<std::uint64_t>(img.width) << 32) ^
                            static_cast<std::uint64_t>(img.height);
        const std::size_t bytes = img.rgba.size();
        const std::size_t words = bytes / 8;
        for (std::size_t i = 0; i < words; ++i) {
            std::uint64_t w = 0;
            std::memcpy(&w, img.rgba.data() + i * 8, 8);
            key = (key ^ w) * 1099511628211ULL;
        }
        for (std::size_t i = words * 8; i < bytes; ++i) {
            key = (key ^ img.rgba[i]) * 1099511628211ULL;
        }
        if (!basePixmapKeyValid_ || key != basePixmapKey_ || basePixmapCache_.isNull()) {
            const QImage qimg(img.rgba.data(), img.width, img.height, img.width * 4,
                              QImage::Format_RGBA8888);
            basePixmapCache_ = QPixmap::fromImage(qimg.copy());
            basePixmapKey_ = key;
            basePixmapKeyValid_ = true;
        }
        const QPixmap pixmap = basePixmapCache_;
        auto* item = scene_->addPixmap(pixmap);
        item->setData(0, QStringLiteral("backgroundImage"));
        item->setOpacity(imageOpacity_);
        const double mmPerPx = project_.mm_per_px.value;
        const double wMm = img.width * mmPerPx;
        const double hMm = img.height * mmPerPx;
        item->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        item->setPos(-wMm / 2.0, -hMm / 2.0);
        baseItems_.append(item);
    }

    // Objets vectoriels (remplissage translucide + contour).
    if (showVectorsAct_ != nullptr && showVectorsAct_->isChecked()) {
        // Préparer une correspondance sans rechercher linéairement chaque broderie pour chaque
        // forme. Les info-bulles ne régénèrent jamais les points du projet.
        std::unordered_map<std::uint64_t, const document::EmbroideryObject*> byVector;
        byVector.reserve(project_.embroidery_objects.size());
        for (const auto& embroidery : project_.embroidery_objects) {
            byVector.try_emplace(embroidery.source_vector.value, &embroidery);
        }
        for (const auto& object : project_.vector_objects) {
            // Filtres d'affichage : une forme sélectionnée reste visible (sinon la
            // sélection deviendrait invisible et ses poignées orphelines).
            const bool selected = isObjectSelected(object.id);
            if (!object.visible || !calqueShowsShape(object) ||
                (!selected && !vectorPassesFilter(object))) {
                continue;
            }
            const QColor color(object.rgb[0], object.rgb[1], object.rgb[2]);
            const QPainterPath outline = objectPainterPath(object);
            // Sélection à DOUBLE contraste : un halo clair sous un trait d'accent,
            // lisible quel que soit le fond du canevas.
            if (selected) {
                QPen halo(AppTheme::instance().tokens().canvasSelectionHalo);
                halo.setCosmetic(true);
                halo.setWidth(6);
                auto* haloItem = scene_->addPath(outline, halo, Qt::NoBrush);
                haloItem->setZValue(9);
                baseItems_.append(haloItem);
            }
            QPen pen(selected ? AppTheme::instance().tokens().canvasSelectionLine
                              : color.darker(150));
            pen.setCosmetic(true);
            pen.setWidth(selected ? 3 : 2);
            const QBrush brush(QColor(color.red(), color.green(), color.blue(), 90));
            QGraphicsPathItem* pathItem = nullptr;
            // Forme SÉLECTIONNÉE, mode Sélection : glisser n'importe où sur le
            // corps la déplace entière (tous nœuds, tous morceaux) au lieu
            // d'exiger de glisser chaque nœud un par un (défaut remonté en
            // usage réel — cf. VectorObjectBodyItem).
            if (selected && currentTool_ == Tool::Select) {
                const ObjectId objectId = object.id;
                auto* bodyItem = new VectorObjectBodyItem(outline, pen, brush, {});
                // Modificateurs lus sur le relâchement : Maj = verrou d'axe (déjà
                // appliqué par l'item), Alt = dupliquer en déplaçant (M4). L'accroche
                // (Ctrl la suspend) n'existe pas pour le glisser de corps.
                bodyItem->setReleasedWithModifiers([this, objectId](QPointF deltaSceneMm,
                                                                    Qt::KeyboardModifiers mods) {
                    const Vec2um delta = sceneMmToModel(deltaSceneMm);
                    const std::uint64_t generation = documentGeneration_;
                    // Alt + simple clic (mouvement <= seuil de glisser) : « Sélectionner
                    // dessous » côté canevas, pas de copie.
                    const bool duplicate = (mods & Qt::AltModifier) != 0 &&
                                           deltaSceneMm.manhattanLength() * view_->pixelsPerMm() >
                                               QApplication::startDragDistance();
                    // Diffère : refreshImage() détruirait cet item pendant son
                    // propre événement souris (même défaut que NodeHandleItem).
                    // Glisser un membre d'une multi-sélection déplace tout
                    // l'ensemble (même helper que les flèches : un seul pas).
                    QTimer::singleShot(0, this, [this, objectId, delta, duplicate, generation] {
                        if (generation != documentGeneration_) {
                            return; // document remplacé entre-temps : aucune commande périmée
                        }
                        if (duplicate) {
                            duplicateAndTranslate(hasMultiSelection() && isObjectSelected(objectId)
                                                      ? selectedObjectIds()
                                                      : std::vector<ObjectId>{objectId},
                                                  delta);
                            return;
                        }
                        translateObjects(hasMultiSelection() && isObjectSelected(objectId)
                                             ? selectedObjectIds()
                                             : std::vector<ObjectId>{objectId},
                                         delta);
                    });
                });
                // Remodelage direct d'un segment (HP-VEC-016) : un appui sur le CONTOUR d'une
                // forme seule sélectionnée tire le segment ; un appui à l'intérieur déplace la
                // forme comme avant.
                if (!hasMultiSelection()) {
                    VectorObjectBodyItem::ReshapeHooks hooks;
                    hooks.hover = [this, objectId](QPointF sceneMm) {
                        return canBeginSegmentReshape(objectId, sceneMm);
                    };
                    hooks.begin = [this, objectId](QPointF sceneMm) {
                        return beginSegmentReshape(objectId, sceneMm);
                    };
                    hooks.update = [this](QPointF sceneMm, Qt::KeyboardModifiers mods) {
                        updateSegmentReshape(sceneMm, mods);
                    };
                    hooks.commit = [this](QPointF sceneMm, Qt::KeyboardModifiers mods) {
                        commitSegmentReshape(sceneMm, mods);
                    };
                    hooks.cancel = [this] { cancelSegmentReshape(); };
                    bodyItem->setReshapeHooks(std::move(hooks));
                }
                scene_->addItem(bodyItem);
                pathItem = bodyItem;
            } else {
                pathItem = scene_->addPath(outline, pen, brush);
                // Curseur contextuel : une main pointeuse au survol d'une forme
                // cliquable, mais SEULEMENT en mode Sélection — avec un outil de
                // dessin actif, survoler une forme existante ne doit pas laisser
                // croire qu'un clic la sélectionnerait (il en dessinerait une
                // nouvelle) : la croix du mode dessin doit rester seule visible.
                if (currentTool_ == Tool::Select) {
                    pathItem->setCursor(Qt::PointingHandCursor);
                }
            }
            const auto match = byVector.find(object.id.value);
            pathItem->setToolTip(vectorTooltip(object, outline.boundingRect(),
                                               match == byVector.end() ? nullptr : match->second));
            pathItem->setData(0, QStringLiteral("vectorShape"));
            pathItem->setZValue(10);
            baseItems_.append(pathItem);
        }
        // Poignées de nœuds de l'objet sélectionné (+ poignées Bézier pour les
        // nœuds Lisse : glisser une poignée édite la courbe sans déplacer le
        // nœud — même principe que le remodelage des rails satin, § courbes
        // de Bézier).
        if (selectedObject_ && !hasMultiSelection()) {
            if (const auto* object = project_.findObject(*selectedObject_)) {
                for (std::size_t s = 0; s < object->paths.size(); ++s) {
                    const auto& set = object->paths[s];
                    const auto addHandles = [&](const geometry::Path& path, std::size_t pathIdx) {
                        for (std::size_t n = 0; n < path.nodes.size(); ++n) {
                            const auto& node = path.nodes[n];
                            const Vec2um pos = node.pos;
                            const QPointF sceneMm(to_millimeters(pos.x).value,
                                                  -to_millimeters(pos.y).value);
                            const ObjectId objectId = object->id;
                            const document::NodeRef ref{s, pathIdx, n};
                            // Relâchement avec modificateurs : Maj = verrou d'axe (appliqué
                            // par l'item), Ctrl = suspend l'accroche (lignes N2/N2b). Avec le
                            // réglage « Accrochage des nœuds au glisser » (désactivé par défaut),
                            // le nœud s'accroche aux SOMMETS des autres objets.
                            const auto generation = documentGeneration_;
                            const auto commitNodeMove = [this, objectId, ref, generation,
                                                         pos](QPointF releasedSceneMm,
                                                              Qt::KeyboardModifiers mods) {
                                hideNodeDragFeedback();
                                const bool inGroup =
                                    selectedNodes_.size() >= 2 && isNodeSelected(ref);
                                if (inGroup) {
                                    // Glisser un nœud sélectionné déplace tout le groupe.
                                    commitGroupMove(objectId,
                                                    sceneMmToModel(releasedSceneMm) - pos);
                                    if (sceneMmToModel(releasedSceneMm) == pos) {
                                        // simple clic : bascule/remplace la sélection
                                        selectNodes(objectId, {ref},
                                                    (mods & Qt::ControlModifier) != 0
                                                        ? SelectMode::Toggle
                                                        : ((mods & Qt::ShiftModifier) != 0
                                                               ? SelectMode::Add
                                                               : SelectMode::Replace));
                                        restyleNodeHandles(); // sans détruire la poignée
                                    }
                                    return;
                                }
                                if (sceneMmToModel(releasedSceneMm) == pos) {
                                    // Simple clic sur une poignée : sélection du nœud.
                                    selectNodes(objectId, {ref},
                                                (mods & Qt::ControlModifier) != 0
                                                    ? SelectMode::Toggle
                                                    : ((mods & Qt::ShiftModifier) != 0
                                                           ? SelectMode::Add
                                                           : SelectMode::Replace));
                                    restyleNodeHandles(); // sans détruire la poignée
                                    return;
                                }
                                QPointF newSceneMm = releasedSceneMm;
                                // Ctrl seul suspend l'accroche : Maj (verrou d'axe) la garde.
                                if (snapNodesAct_ != nullptr && snapNodesAct_->isChecked() &&
                                    (mods & Qt::ControlModifier) == 0) {
                                    newSceneMm =
                                        findNodeSnapMm(newSceneMm, objectId).value_or(newSceneMm);
                                }
                                const Vec2um newPos{to_micrometers(Millimeters{newSceneMm.x()}),
                                                    to_micrometers(Millimeters{-newSceneMm.y()})};
                                if (newPos == pos) {
                                    return;
                                }
                                // Diffère : refreshImage() détruirait cette poignée
                                // pendant son propre événement souris (crash).
                                QTimer::singleShot(
                                    0, this, [this, objectId, ref, pos, newPos, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveNodeCommand>(
                                                objectId, ref, pos, newPos),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                            };
                            auto* handle = new NodeHandleItem(
                                sceneMm, {},
                                // Aperçu pendant le glisser : coordonnées en mm dans la barre
                                // d'état et repère d'accroche si le nœud s'y colle.
                                [this, objectId, ref, pos](QPointF movedSceneMm) {
                                    if (selectedNodes_.size() >= 2 && isNodeSelected(ref)) {
                                        previewGroupMove(objectId,
                                                         sceneMmToModel(movedSceneMm) - pos);
                                        return;
                                    }
                                    showNodeDragFeedback(movedSceneMm, objectId);
                                },
                                // Clic droit sur un nœud : simplification manuelle d'une
                                // forme (typiquement après vectorisation d'une région
                                // segmentée — contour trop détaillé pour être exploitable
                                // tel quel).
                                [this, objectId, ref](QPoint globalPos) {
                                    QMenu nodeMenu(this);
                                    auto* deleteAct = nodeMenu.addAction(tr("Supprimer le nœud"));
                                    bool canDelete = false;
                                    if (auto* obj = project_.findObject(objectId)) {
                                        if (const auto* path =
                                                document::path_in(*obj, ref.set, ref.path)) {
                                            // 3 nœuds min. pour un chemin fermé, 2 pour un
                                            // chemin ouvert (cf. RemoveNodeCommand::apply).
                                            const std::size_t minNodes = path->closed ? 3 : 2;
                                            canDelete = path->nodes.size() > minNodes;
                                        }
                                    }
                                    deleteAct->setEnabled(canDelete);
                                    connect(deleteAct, &QAction::triggered, this,
                                            [this, objectId, ref] {
                                                undoStack_.execute(
                                                    std::make_unique<commands::RemoveNodeCommand>(
                                                        objectId, ref),
                                                    project_);
                                                refreshImage();
                                                updateActions();
                                            });
                                    nodeMenu.exec(globalPos);
                                });
                            handle->setReleasedWithModifiers(commitNodeMove);
                            nodeHandles_.emplace_back(ref, handle);
                            if (isNodeSelected(ref)) {
                                handle->setBrush(QBrush(AppTheme::instance().tokens().accent));
                            }
                            handle->setCancelled([this] {
                                hideGroupPreview();
                                hideNodeDragFeedback();
                                statusBar()->showMessage(tr("Déplacement annulé."), 2000);
                            });
                            scene_->addItem(handle);
                            baseItems_.append(handle);

                            if (node.type != geometry::NodeType::Smooth) {
                                continue;
                            }
                            const auto addBezierHandle = [&](bool isOut,
                                                             const std::optional<Vec2um>& tan) {
                                if (!tan) {
                                    return;
                                }
                                const Vec2um handlePos = pos + *tan;
                                const QPointF handleSceneMm(to_millimeters(handlePos.x).value,
                                                            -to_millimeters(handlePos.y).value);
                                auto* link = scene_->addLine(
                                    QLineF(sceneMm, handleSceneMm),
                                    QPen(AppTheme::instance().tokens().accent, 1.0));
                                link->setZValue(97);
                                baseItems_.append(link);

                                auto* handleItem = new NodeHandleItem(
                                    handleSceneMm,
                                    [this, objectId, ref, isOut, oldTan = tan](QPointF newSceneMm) {
                                        const Vec2um newHandlePos{
                                            to_micrometers(Millimeters{newSceneMm.x()}),
                                            to_micrometers(Millimeters{-newSceneMm.y()})};
                                        const auto* obj = project_.findObject(objectId);
                                        const auto* p =
                                            obj != nullptr
                                                ? document::path_in(*obj, ref.set, ref.path)
                                                : nullptr;
                                        if (p == nullptr || ref.node >= p->nodes.size()) {
                                            return;
                                        }
                                        const Vec2um newTan = newHandlePos - p->nodes[ref.node].pos;
                                        if (newTan == *oldTan) {
                                            return;
                                        }
                                        QTimer::singleShot(
                                            0, this, [this, objectId, ref, isOut, oldTan, newTan] {
                                                undoStack_.execute(
                                                    std::make_unique<
                                                        commands::SetNodeHandleCommand>(
                                                        objectId, ref, isOut, oldTan, newTan),
                                                    project_);
                                                refreshImage();
                                                updateActions();
                                            });
                                    });
                                handleItem->setPen(QPen(AppTheme::instance().tokens().accent, 1.5));
                                handleItem->setBrush(
                                    QBrush(AppTheme::instance().tokens().accent.lighter(160)));
                                handleItem->setToolTip(
                                    isOut ? tr("Poignée sortante — glisser pour incurver la "
                                               "courbe")
                                          : tr("Poignée entrante — glisser pour incurver la "
                                               "courbe"));
                                scene_->addItem(handleItem);
                                baseItems_.append(handleItem);
                            };
                            addBezierHandle(true, node.tan_out);
                            addBezierHandle(false, node.tan_in);
                        }
                    };
                    addHandles(set.outer, 0);
                    for (std::size_t h = 0; h < set.holes.size(); ++h) {
                        addHandles(set.holes[h], h + 1);
                    }
                }
            }
        }

        // Poignées de redimensionnement (4 coins de la boîte englobante) de
        // l'objet sélectionné, mode Sélection uniquement : glisser un coin
        // redimensionne la forme entière autour du coin diagonalement
        // opposé (fixe) — jusqu'ici, redimensionner exigeait de déplacer
        // chaque nœud un par un (même défaut remonté que pour le déplacement
        // de forme entière, § VectorObjectBodyItem plus haut).
        if (selectedObject_ && !hasMultiSelection() && currentTool_ == Tool::Select) {
            if (const auto* object = project_.findObject(*selectedObject_)) {
                std::optional<Micrometers> minX, maxX, minY, maxY;
                const auto scan = [&](const geometry::Path& path) {
                    for (const auto& node : path.nodes) {
                        if (!minX || node.pos.x < *minX)
                            minX = node.pos.x;
                        if (!maxX || node.pos.x > *maxX)
                            maxX = node.pos.x;
                        if (!minY || node.pos.y < *minY)
                            minY = node.pos.y;
                        if (!maxY || node.pos.y > *maxY)
                            maxY = node.pos.y;
                    }
                };
                for (const auto& set : object->paths) {
                    scan(set.outer);
                    for (const auto& hole : set.holes) {
                        scan(hole);
                    }
                }
                // Boîte dégénérée sur un axe (segment/point) : aucun coin
                // opposé exploitable comme ancrage sur cet axe, on renonce
                // plutôt que de diviser par zéro.
                if (minX && maxX && minY && maxY && *minX != *maxX && *minY != *maxY) {
                    const ObjectId objectId = object->id;
                    const auto generation = documentGeneration_;
                    const Vec2um corners[4] = {Vec2um{*minX, *minY}, Vec2um{*maxX, *minY},
                                               Vec2um{*maxX, *maxY}, Vec2um{*minX, *maxY}};
                    for (std::size_t i = 0; i < 4; ++i) {
                        const Vec2um corner = corners[i];
                        const Vec2um anchor = corners[(i + 2) % 4]; // coin diagonalement opposé
                        // Coins 0 et 2 (bas-gauche, haut-droite) : diagonale « / » ; coins 1 et
                        // 3 : « \ ». Chaque coin a le curseur de sa propre diagonale.
                        const Qt::CursorShape cursor =
                            i % 2 == 0 ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
                        const QPointF anchorScene = modelToSceneMm(anchor);
                        auto* handle = new ResizeHandleItem(
                            modelToSceneMm(corner),
                            [this, objectId, corner, anchor, generation](QPointF newSceneMm) {
                                hideResizePreview();
                                const Vec2um newCorner = sceneMmToModel(newSceneMm);
                                if (newCorner == corner) {
                                    return;
                                }
                                // Planchers : ni miroir involontaire ni forme écrasée à zéro
                                // (la poignée les applique déjà ; garde de défense).
                                const double sx = std::max(
                                    0.05, static_cast<double>((newCorner.x - anchor.x).value) /
                                              static_cast<double>((corner.x - anchor.x).value));
                                const double sy = std::max(
                                    0.05, static_cast<double>((newCorner.y - anchor.y).value) /
                                              static_cast<double>((corner.y - anchor.y).value));
                                // Diffère : refreshImage() détruirait cet item pendant
                                // son propre événement souris (même défaut que
                                // NodeHandleItem/VectorObjectBodyItem).
                                QTimer::singleShot(
                                    0, this, [this, objectId, anchor, sx, sy, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        undoStack_.execute(
                                            std::make_unique<commands::ScaleVectorObjectCommand>(
                                                objectId, anchor, sx, sy),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                            },
                            anchorScene, /*hasAnchor=*/true, cursor,
                            [this, anchorScene](QPointF movedSceneMm) {
                                showResizePreview(anchorScene, movedSceneMm);
                            });
                        handle->setCancelled([this] { hideResizePreview(); });
                        handle->setToolTip(tr("Glisser pour redimensionner (coin opposé fixe) — "
                                              "Maj : conserver les proportions, Échap : annuler"));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    }
                }
            }
        }
    }

    // Poignée de rotation + pivot de la sélection (une ou plusieurs formes), HP-OBJ-004.
    addRotationHandles();

    // Centre du remplissage radial / en spirale sélectionné (HP-STI-008 / 009).
    addFillCenterHandle();

    // Poignée de rotation du remplissage tatami sélectionné : un axe montrant
    // l'orientation des fils, avec un bouton à faire glisser. Le nouvel angle est
    // validé au relâchement (commande annulable) — pas de recalcul par pixel.
    if (auto* fill = currentFillObject()) {
        const auto& tatami = std::get<document::TatamiParams>(fill->params);
        const Vec2um cUm = embroideryCentroid(*fill);
        const QPointF c(to_millimeters(cUm.x).value, -to_millimeters(cUm.y).value);
        double radius = 12.0; // mm
        if (const auto* vec = project_.findObject(fill->source_vector)) {
            const QRectF bb = objectPainterPath(*vec).boundingRect();
            radius = std::clamp(0.45 * std::min(bb.width(), bb.height()), 6.0, 60.0);
        }
        const double theta = tatami.angle.radians; // repère physique (Y haut)
        const double dx = radius * std::cos(theta);
        const double dy = -radius * std::sin(theta); // scène : Y vers le bas
        QPen axisPen(AppTheme::instance().tokens().canvasHandle);
        axisPen.setCosmetic(true);
        axisPen.setWidth(2);
        auto* axis =
            scene_->addLine(QLineF(c.x() - dx, c.y() - dy, c.x() + dx, c.y() + dy), axisPen);
        axis->setZValue(98);
        baseItems_.append(axis);

        const ObjectId fillId = fill->id;
        const auto generation = documentGeneration_;
        auto* knob = new NodeHandleItem(
            QPointF(c.x() + dx, c.y() + dy),
            [this, fillId, c, generation](QPointF released) {
                double angle = std::atan2(-(released.y() - c.y()), released.x() - c.x());
                angle = std::fmod(angle, std::numbers::pi);
                if (angle < 0.0) {
                    angle += std::numbers::pi;
                }
                // Diffère la commande : refreshImage() reconstruit la scène et
                // détruirait cette poignée pendant son propre événement souris
                // (usage après libération → crash).
                QTimer::singleShot(0, this, [this, fillId, angle, generation] {
                    if (generation != documentGeneration_) {
                        return;
                    }
                    undoStack_.execute(
                        std::make_unique<commands::SetFillAngleCommand>(fillId, Angle{angle}),
                        project_);
                    refreshImage();
                    updateActions();
                });
            },
            [this, c](QPointF moved) { // aperçu : angle affiché pendant le glisser
                double angle = std::atan2(-(moved.y() - c.y()), moved.x() - c.x());
                angle = std::fmod(angle, std::numbers::pi);
                if (angle < 0.0) {
                    angle += std::numbers::pi;
                }
                statusBar()->showMessage(
                    tr("Orientation : %1°").arg(angle * 180.0 / std::numbers::pi, 0, 'f', 0));
            });
        knob->setPen(QPen(AppTheme::instance().tokens().canvasHandle, 1.5));
        knob->setBrush(QBrush(AppTheme::instance().tokens().canvasHandle.lighter(160)));
        knob->setCursor(Qt::CrossCursor);
        knob->setToolTip(tr("Glisser pour tourner l'orientation des fils"));
        scene_->addItem(knob);
        baseItems_.append(knob);
    }

    // Remplissage directionnel : champ, guides et ruptures (mode guides).
    renderDirectionGuides();
    renderGradientHandles();

    // Guides satin paramétriques. Chaque extrémité est indépendante mais reste
    // contrainte à son rail ; la validation monotone est partagée avec les
    // tests du cœur, donc un geste accepté ne disparaît pas à la génération.
    if (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked() && satinGuideTarget_) {
        if (const auto* obj = project_.findEmbroidery(*satinGuideTarget_)) {
            if (const auto* satin = std::get_if<document::SatinParams>(&obj->params)) {
                const ObjectId targetId = obj->id;
                const std::uint64_t generation = documentGeneration_;
                const auto guideJunctions = stitch_generation::satin_guide_junctions(*satin);
                for (std::size_t i = 0; i < satin->rungs.size(); ++i) {
                    const auto rung = satin->rungs[i];
                    const auto junction = guideJunctions[i];
                    const QPointF a(to_millimeters(rung.a.x).value,
                                    -to_millimeters(rung.a.y).value);
                    const QPointF b(to_millimeters(rung.b.x).value,
                                    -to_millimeters(rung.b.y).value);
                    QPen guidePen(AppTheme::instance().tokens().accent);
                    guidePen.setCosmetic(true);
                    const bool selected = selectedSatinGuide_ && *selectedSatinGuide_ == i;
                    guidePen.setWidthF(selected ? 3.0 : 1.5);
                    if (selected) {
                        guidePen.setColor(AppTheme::instance().tokens().accent.lighter(150));
                    }
                    auto* line = new SatinGuideItem(QLineF(a, b), [this, targetId, i, generation] {
                        QTimer::singleShot(0, this, [this, targetId, i, generation] {
                            if (generation != documentGeneration_ || !satinGuideTarget_ ||
                                *satinGuideTarget_ != targetId) {
                                return;
                            }
                            selectedSatinGuide_ = i;
                            displayImage(processed_);
                            updateActions();
                        });
                    });
                    line->setPen(guidePen);
                    line->setZValue(99);
                    line->setToolTip(
                        junction
                            ? tr("Guide satin #%1 — jonction structurelle verrouillée").arg(i + 1)
                        : rung.link_id
                            ? tr("Guide satin #%1 — lié : Maj+glisser une extrémité déplace tout "
                                 "le groupe, glisser seul ne modifie que cette section")
                                  .arg(i + 1)
                            : tr("Guide satin #%1 — cliquer pour sélectionner").arg(i + 1));
                    scene_->addItem(line);
                    baseItems_.append(line);

                    // Geste explicite du groupe lié (§ guides liés) : Maj+glisser une
                    // extrémité d'un guide portant un link_id déplace TOUT le groupe
                    // (une section par jonction) en préservant, pour chaque section,
                    // sa position normalisée entre la jonction et son propre voisin —
                    // jamais de coordonnée/angle recopié d'une section à l'autre. Un
                    // glisser sans Maj reste un geste local (édition d'angle) qui ne
                    // touche que cette section, comme avant.
                    const auto addEndpoint = [this, targetId, i, rung,
                                              generation](QPointF scenePos,
                                                          stitch_generation::SatinGuideSide side) {
                        auto* handle = new NodeHandleItem(scenePos, [this, targetId, i, rung,
                                                                     generation,
                                                                     side](QPointF released) {
                            const Vec2um desired{to_micrometers(Millimeters{released.x()}),
                                                 to_micrometers(Millimeters{-released.y()})};
                            const auto* current = project_.findEmbroidery(targetId);
                            const auto* satinNow =
                                current != nullptr
                                    ? std::get_if<document::SatinParams>(&current->params)
                                    : nullptr;
                            if (satinNow == nullptr) {
                                return;
                            }
                            const bool groupGesture =
                                rung.link_id.has_value() &&
                                (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
                            if (groupGesture) {
                                const auto edits = stitch_generation::move_satin_guide_group(
                                    project_, current->source_vector, *rung.link_id, targetId, side,
                                    desired);
                                if (!edits) {
                                    statusBar()->showMessage(
                                        tr("Déplacement du groupe refusé : intervalle "
                                           "épuisé ou jonction modifiée entretemps."));
                                    displayImage(processed_);
                                    return;
                                }
                                const bool changed = std::any_of(
                                    edits->begin(), edits->end(), [this](const auto& edit) {
                                        const auto* section =
                                            project_.findEmbroidery(edit.embroidery_id);
                                        const auto* sectionSatin =
                                            section != nullptr ? std::get_if<document::SatinParams>(
                                                                     &section->params)
                                                               : nullptr;
                                        return sectionSatin == nullptr ||
                                               edit.guide_index >= sectionSatin->rungs.size() ||
                                               sectionSatin->rungs[edit.guide_index] != edit.guide;
                                    });
                                if (!changed) {
                                    return; // Maj+clic sans mouvement : aucun historique
                                            // fantôme
                                }
                                QTimer::singleShot(0, this, [this, edits = *edits, generation] {
                                    if (generation != documentGeneration_) {
                                        return;
                                    }
                                    std::vector<commands::SatinGuideEdit> commandEdits;
                                    commandEdits.reserve(edits.size());
                                    for (const auto& edit : edits) {
                                        commandEdits.push_back(
                                            {edit.embroidery_id, edit.guide_index, edit.guide});
                                    }
                                    const auto groupCount = commandEdits.size();
                                    undoStack_.execute(
                                        std::make_unique<commands::MoveSatinGuidesCommand>(
                                            std::move(commandEdits)),
                                        project_);
                                    refreshImage();
                                    updateActions();
                                    statusBar()->showMessage(
                                        tr("%1 guides liés déplacés ensemble.").arg(groupCount));
                                });
                                return;
                            }
                            const auto moved = stitch_generation::move_satin_guide_endpoint(
                                *satinNow, i, side, desired);
                            if (!moved) {
                                statusBar()->showMessage(tr(
                                    "Guide refusé : il doit rester ordonné sur les deux rails."));
                                displayImage(processed_);
                                return;
                            }
                            if (*moved == rung) {
                                return;
                            }
                            QTimer::singleShot(
                                0, this, [this, targetId, i, moved = *moved, generation] {
                                    if (generation != documentGeneration_) {
                                        return;
                                    }
                                    undoStack_.execute(
                                        std::make_unique<commands::MoveSatinGuideCommand>(targetId,
                                                                                          i, moved),
                                        project_);
                                    refreshImage();
                                    updateActions();
                                });
                        });
                        handle->setPen(QPen(AppTheme::instance().tokens().accent, 1.5));
                        handle->setBrush(QBrush(AppTheme::instance().tokens().accent.lighter(160)));
                        handle->setToolTip(
                            side == stitch_generation::SatinGuideSide::RailA
                                ? tr("Guide satin #%1 — extrémité rail A").arg(i + 1)
                                : tr("Guide satin #%1 — extrémité rail B").arg(i + 1));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    };
                    if (!junction) {
                        addEndpoint(a, stitch_generation::SatinGuideSide::RailA);
                        addEndpoint(b, stitch_generation::SatinGuideSide::RailB);
                    }
                }
            }
        }
    }

    // Nœuds des rails d'une colonne satin (mode remodelage) : distinct du
    // mode guides ci-dessus (qui édite les barreaux transversaux) — ici, les
    // nœuds de rail_a/rail_b eux-mêmes. Rail A et rail B dans deux couleurs
    // dédiées pour rester lisibles quand les deux rails se croisent de près.
    if (railEditModeAct_ != nullptr && railEditModeAct_->isChecked() && railEditTarget_) {
        if (const auto* obj = project_.findEmbroidery(*railEditTarget_)) {
            if (const auto* satin = std::get_if<document::SatinParams>(&obj->params)) {
                const ObjectId targetId = obj->id;
                const std::uint64_t generation = documentGeneration_;
                const auto drawRail = [&](const geometry::Path& rail, commands::SatinRailSide side,
                                          const QColor& color) {
                    QPainterPath outline;
                    bool started = false;
                    for (const auto& node : rail.nodes) {
                        const QPointF p = modelToSceneMm(node.pos);
                        if (!started) {
                            outline.moveTo(p);
                            started = true;
                        } else {
                            outline.lineTo(p);
                        }
                    }
                    QPen railPen(color);
                    railPen.setCosmetic(true);
                    railPen.setWidth(2);
                    railPen.setStyle(Qt::DashLine);
                    auto* railItem = scene_->addPath(outline, railPen, Qt::NoBrush);
                    railItem->setZValue(98);
                    baseItems_.append(railItem);

                    for (std::size_t n = 0; n < rail.nodes.size(); ++n) {
                        const Vec2um pos = rail.nodes[n].pos;
                        const QPointF sceneMm = modelToSceneMm(pos);
                        auto* handle =
                            new NodeHandleItem(sceneMm, [this, targetId, side, n, pos,
                                                         generation](QPointF newSceneMm) {
                                const Vec2um newPos = sceneMmToModel(newSceneMm);
                                if (newPos == pos) {
                                    return;
                                }
                                // Diffère : refreshImage() détruirait cette poignée
                                // pendant son propre événement souris (crash) — même
                                // précaution que les autres poignées ci-dessus.
                                QTimer::singleShot(
                                    0, this, [this, targetId, side, n, pos, newPos, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveSatinRailNodeCommand>(
                                                targetId, side, n, pos, newPos),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                            });
                        handle->setPen(QPen(color, 1.5));
                        handle->setBrush(QBrush(color.lighter(160)));
                        handle->setToolTip(
                            tr("Rail %1, nœud #%2 — glisser pour déplacer")
                                .arg(side == commands::SatinRailSide::RailA ? "A" : "B")
                                .arg(n + 1));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    }
                };
                drawRail(satin->rail_a, commands::SatinRailSide::RailA, QColor(200, 90, 40));
                drawRail(satin->rail_b, commands::SatinRailSide::RailB, QColor(40, 130, 200));
            }
        }
    }

    // Poignées de points de couture (Lot 8.2, mode d'édition des points) :
    // un point déplaçable par entrée TopStitch/Stitch de la vue brute de
    // l'objet ciblé (stitchEditView_, tenu à jour par refreshImage). Visuellement
    // et conceptuellement distinctes des poignées de nœuds vectoriels
    // ci-dessus (couleur dédiée) : ici on déplace le point COUSU (retouche
    // locale), pas le nœud source (qui régénère tout).
    if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked() && stitchEditView_ &&
        stitchEditTarget_) {
        // Un GraphicsItem par poignée coûte cher (mémoire + événements) : au-delà
        // de ce seuil, le mode reste actif mais n'affiche aucune poignée plutôt
        // que de dégrader l'interface (même logique que `drawDots` ci-dessous
        // pour la broderie complète). `updateActions()` (appelé juste après
        // tout `refreshImage()`) sort proprement du mode et l'explique en barre
        // de statut dès qu'il détecte ce dépassement — ce garde-fou ici couvre
        // seulement le tout premier rendu, avant qu'`updateActions()` ait pu
        // tourner (cf. son bloc `stitchEditTooManyPoints`).
        const auto& view = *stitchEditView_;
        const ObjectId targetId = *stitchEditTarget_;
        // Le seuil porte sur les points RÉELLEMENT déplaçables (une passe
        // TopStitch/Stitch), pas sur `view.raw.size()` qui compte aussi les
        // sauts/points de sous-couche jamais dotés d'une poignée.
        const auto movableCount =
            std::count_if(view.raw.begin(), view.raw.end(), stitch_generation::is_movable_point);
        if (const auto* obj = project_.findEmbroidery(targetId);
            obj != nullptr && static_cast<std::size_t>(movableCount) <= kMaxEditableStitchHandles) {
            // Overrides indexés une fois par `base_index` : la boucle sur les
            // poignées ci-dessous ne doit pas redevenir O(points*overrides) en
            // rescannant `obj->overrides` pour chaque point (motif dense).
            std::unordered_map<std::size_t, Vec2um> movedOverrides;
            std::unordered_set<std::size_t> deletedIdx;
            movedOverrides.reserve(obj->overrides.size());
            for (const auto& ov : obj->overrides) {
                if (ov.moved_to) {
                    movedOverrides.emplace(ov.base_index, *ov.moved_to);
                }
                if (ov.deleted) {
                    deletedIdx.insert(ov.base_index);
                }
            }
            // La sélection n'a de sens que pour la vue brute qui l'a produite.
            if (stitchSelectionFingerprint_ != view.fingerprint) {
                stitchSelection_.clear();
                stitchSelectionFingerprint_ = view.fingerprint;
            }
            for (std::size_t i = 0; i < view.raw.size(); ++i) {
                const auto& cmd = view.raw[i];
                if (!stitch_generation::is_movable_point(cmd)) {
                    continue;
                }
                Vec2um pos = cmd.pos; // position affichée : retouche existante sinon brute
                if (auto it = movedOverrides.find(i); it != movedOverrides.end()) {
                    pos = it->second;
                }
                addStitchPointHandle(i, pos, stitchSelection_.count(i) != 0,
                                     deletedIdx.count(i) != 0);
            }
            // Points insérés (HP-EDIT-002) : poignées propres, déplaçables une à une.
            for (const auto& ov : obj->overrides) {
                for (std::size_t k = 0; k < ov.inserted_after.size(); ++k) {
                    addInsertedPointHandle(ov.base_index, k, ov.inserted_after[k]);
                }
            }
        }
    }

    // Carte des régions par-dessus l'image (mode d'affichage segmentation).
    if (showSegAct_ != nullptr && showSegAct_->isChecked() &&
        (project_.segmentation || previewSegmentation_)) {
        const double mmPerPx = project_.mm_per_px.value;
        // Aperçu d'une fusion de couleurs : la copie modifiée remplace la segmentation du
        // document (sans surbrillance : les identifiants de sélection n'y ont plus cours).
        const auto map =
            previewSegmentation_
                ? segmentation::render_map_multi(*previewSegmentation_, {})
                : segmentation::render_map_multi(*project_.segmentation, selectedRegionIds());
        const QImage mapImg(map.rgba.data(), map.width, map.height, map.width * 4,
                            QImage::Format_RGBA8888);
        auto* mapItem = scene_->addPixmap(QPixmap::fromImage(mapImg.copy()));
        mapItem->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        mapItem->setPos(-map.width * mmPerPx / 2.0, -map.height * mmPerPx / 2.0);
        mapItem->setOpacity(regionMapOpacity_);
        mapItem->setData(0, QStringLiteral("regionMap")); // retrouvé par setRegionMapOpacity
        baseItems_.append(mapItem);
    }
}

struct MainWindow::StitchMaps {
    stitch_render::PassDisplay passes; // HP-VIEW-003 : passes affichées
    std::unordered_map<std::uint64_t, bool> visible;
    std::unordered_map<std::uint64_t, QRgb> colorOf;

    [[nodiscard]] bool isVisible(std::uint64_t src) const {
        const auto it = visible.find(src);
        return it != visible.end() && it->second;
    }
    [[nodiscard]] QRgb colorFor(std::uint64_t src) const {
        const auto it = colorOf.find(src);
        return it != colorOf.end() ? it->second : QRgb{0};
    }
};

// Tracés d'un tronçon de séquence : un tracé cousu par couleur de fil (pour afficher la
// broderie en couleur), les sauts et les pastilles de pénétration.
struct MainWindow::StitchPaths {
    std::map<QRgb, QPainterPath> sewByColor;
    QPainterPath jump;
    QPainterPath dots;
};

MainWindow::StitchMaps MainWindow::buildStitchMaps() const {
    StitchMaps maps;
    maps.passes = viewDisplay_.passes;
    // Visibilité (filtres) et couleur de fil par objet de broderie.
    for (const auto& emb : project_.embroidery_objects) {
        maps.visible[emb.id.value] = objectPassesFilter(emb);
        maps.colorOf[emb.id.value] = qRgb(emb.rgb[0], emb.rgb[1], emb.rgb[2]);
    }
    // AD-04 : un design importé (DST aujourd'hui) porte ses commandes avec
    // `source == ObjectId{}` (0 = manuel/importé, cf. stitch/sequence.hpp) --
    // toujours visible en P0 (non transformable, aucun filtre ne s'y
    // applique). Une seule couleur pour tout le design (DST ne porte aucune
    // vraie couleur, roadmap §2 FMT-002) : noir par défaut si ses blocs n'en
    // indiquent aucune -- à revoir avec HP-FMT-003 (import PES, couleurs
    // réelles, qui pourra varier par bloc).
    if (project_.imported_design) {
        const auto& blocks = project_.imported_design->color_blocks;
        const std::array<std::uint8_t, 3> rgb =
            blocks.empty() ? std::array<std::uint8_t, 3>{0, 0, 0} : blocks.front().rgb;
        maps.visible[ObjectId{}.value] = true;
        maps.colorOf[ObjectId{}.value] = qRgb(rgb[0], rgb[1], rgb[2]);
    }
    return maps;
}

// Parcourt les commandes [from, to] (bornes incluses) en prolongeant `state` : le même code
// sert au rendu complet (from = 0) et à l'ajout incrémental de la simulation (from = state.next),
// de sorte qu'un tronçon ajouté est exactement ce que le rendu complet aurait dessiné.
void MainWindow::walkStitches(const StitchMaps& maps, int from, int to, SimWalkState& state,
                              bool drawDots, StitchPaths& out) const {
    const int total = static_cast<int>(sequence_->commands.size());
    for (int i = std::max(from, 0); i < total && i <= to; ++i) {
        const auto& cmd = sequence_->commands[static_cast<std::size_t>(i)];
        const QPointF p(to_millimeters(cmd.pos.x).value, -to_millimeters(cmd.pos.y).value);
        const std::uint64_t src = cmd.source.value;
        const bool vis = maps.isVisible(src);
        // HP-VIEW-003 : une passe masquée n'est pas dessinée, mais la continuité est conservée.
        const bool shown = vis && stitch_render::pass_visible(maps.passes, cmd.pass);
        switch (cmd.type) {
        case stitch::CommandType::Stitch:
            // On ne relie que deux points du MÊME objet (jamais entre objets).
            if (shown && state.hasPos && state.lastSource == src) {
                const QRgb key = maps.passes.distinct_colors ? [&] {
                    const auto t = stitch_render::pass_tint(cmd.pass);
                    return qRgb(t[0], t[1], t[2]);
                }()
                                                             : maps.colorFor(src);
                QPainterPath& path = out.sewByColor[key];
                path.moveTo(state.last);
                path.lineTo(p);
            }
            if (shown && drawDots) {
                out.dots.addEllipse(p, 0.15, 0.15);
            }
            state.last = p;
            state.hasPos = true;
            state.lastSource = src;
            if (vis) {
                state.needle = p;
                state.hasNeedle = true;
            }
            break;
        case stitch::CommandType::Jump:
            if (vis && maps.passes.travel && state.hasPos && state.lastSource == src) {
                out.jump.moveTo(state.last);
                out.jump.lineTo(p);
            }
            state.last = p;
            state.hasPos = true;
            state.lastSource = src;
            if (vis) {
                state.needle = p;
                state.hasNeedle = true;
            }
            break;
        default:
            state.hasPos = false; // ColorChange / End : rompt la continuité
            break;
        }
        state.next = i + 1;
    }
}

void MainWindow::addStitchItems(const StitchPaths& paths, bool drawDots, bool skipEmpty) {
    for (const auto& [rgb, path] : paths.sewByColor) {
        QColor c = QColor::fromRgb(rgb);
        // Un fil très clair serait invisible sur fond blanc : on l'assombrit un peu.
        if (c.lightnessF() > 0.85) {
            c = c.darker(140);
        }
        QPen sewPen(c);
        sewPen.setCosmetic(true);
        sewPen.setWidth(2);
        auto* sewItem = scene_->addPath(path, sewPen);
        sewItem->setZValue(20);
        stitchItems_.append(sewItem);
    }

    const Tokens& tk = AppTheme::instance().tokens();
    if (!skipEmpty || !paths.jump.isEmpty()) {
        QPen jumpPen(tk.canvasJump);
        jumpPen.setCosmetic(true);
        jumpPen.setStyle(Qt::DashLine);
        auto* jumpItem = scene_->addPath(paths.jump, jumpPen);
        jumpItem->setZValue(20);
        stitchItems_.append(jumpItem);
    }

    if (drawDots) {
        auto* dotsItem = scene_->addPath(paths.dots, Qt::NoPen, QBrush(tk.canvasStitch));
        dotsItem->setZValue(21);
        stitchItems_.append(dotsItem);
    }
}

void MainWindow::renderStitches() {
    for (QGraphicsItem* it : stitchItems_) {
        scene_->removeItem(it);
        delete it;
    }
    stitchItems_.clear();
    simMarker_ = nullptr;
    simWalkValid_ = false;
    refreshDensityOverlay();    // HP-VIEW-005 (main_window_density.cpp)
    refreshPullMapOverlay();    // tirage prédit (main_window_pull_preview.cpp)
    refreshOrderJumpsOverlay(); // sauts à vide du panneau Ordre (main_window_order_jumps.cpp)
    refreshFabricRiskOverlay(); // risque tissu (main_window_fabric_risk.cpp)

    if (stitchLayerSuspended_ || showStitchesAct_ == nullptr || !showStitchesAct_->isChecked() ||
        !sequence_) {
        return;
    }
    if (renderRealisticStitches()) {
        return;
    }

    const int total = static_cast<int>(sequence_->commands.size());
    const int limit = simulating() ? simStep_ : total;
    // Les pastilles de pénétration (une ellipse par point) coûtent cher : on
    // ne les dessine que pour les petits motifs et hors simulation.
    const bool drawDots = !simulating() && total <= 4000;

    const StitchMaps maps = buildStitchMaps();
    StitchPaths paths;
    SimWalkState state;
    walkStitches(maps, 0, limit, state, drawDots, paths);
    addStitchItems(paths, drawDots, /*skipEmpty=*/false);
    addFunctionMarkers(); // HP-VIEW-003 (main_window_view_display.cpp)

    if (simulating()) {
        simWalk_ = state;
        simWalkValid_ = true;
        placeSimulationMarker();
    }
}

void MainWindow::placeSimulationMarker() {
    if (simMarker_ != nullptr) {
        stitchItems_.removeOne(simMarker_);
        scene_->removeItem(simMarker_);
        delete simMarker_;
        simMarker_ = nullptr;
    }
    if (!simulating() || !simWalk_.hasNeedle) {
        return;
    }
    const Tokens& tk = AppTheme::instance().tokens();
    auto* marker = scene_->addEllipse(simWalk_.needle.x() - 0.6, simWalk_.needle.y() - 0.6, 1.2,
                                      1.2, QPen(Qt::NoPen), QBrush(tk.error));
    marker->setZValue(30);
    stitchItems_.append(marker);
    simMarker_ = marker;
}

// Avance incrémentale de la simulation : ne dessine que les commandes nouvelles (O(delta) par
// pas, O(n) au total au lieu de O(n²) avec un rendu complet à chaque pas). Retombe sur le rendu
// complet quand l'état dessiné ne peut pas être prolongé (retour en arrière, couche masquée,
// trop d'éléments accumulés).
void MainWindow::appendSimulation(int newLimit) {
    if (!sequence_ || showStitchesAct_ == nullptr || !showStitchesAct_->isChecked()) {
        renderStitches();
        return;
    }
    const int total = static_cast<int>(sequence_->commands.size());
    const int last = std::min(newLimit, total - 1);
    constexpr int kMaxIncrementalItems = 800;
    if (!simWalkValid_ || last + 1 < simWalk_.next || stitchItems_.size() > kMaxIncrementalItems) {
        renderStitches();
        return;
    }
    if (last + 1 == simWalk_.next) {
        return; // rien de nouveau à dessiner
    }
    const StitchMaps maps = buildStitchMaps();
    StitchPaths paths;
    walkStitches(maps, simWalk_.next, last, simWalk_, /*drawDots=*/false, paths);
    addStitchItems(paths, /*drawDots=*/false, /*skipEmpty=*/true);
    placeSimulationMarker();
}

} // namespace openstitch::desktop
