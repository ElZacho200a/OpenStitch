// SPDX-License-Identifier: Apache-2.0
// Clics, sélection, rectangle, suppression et état de sélection.
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

void MainWindow::onCanvasClicked(QPointF posMm) {
    // En mode « Déplacer la vue », un clic ne sélectionne rien.
    if (currentTool_ == Tool::Pan) {
        return;
    }
    if (currentTool_ == Tool::Text) {
        placeTextAt(posMm);
        return;
    }
    if (currentTool_ == Tool::Brush || currentTool_ == Tool::DetailBrush) {
        return; // le pinceau passe par brushPressedMm / brushMovedMm / brushReleasedMm
    }
    if (currentTool_ == Tool::Measure) {
        return; // la règle passe par measureDragMm/measureFinishedMm, jamais par un clic
    }
    if (currentTool_ == Tool::Eyedropper) {
        eyedropperAt(posMm);
        return;
    }
    if (currentTool_ == Tool::Bucket) {
        bucketFillAt(posMm);
        return;
    }
    if (currentTool_ == Tool::ClickToStitch) {
        clickToStitchAt(posMm);
        return;
    }
    if (drawingDirectionGuide()) {
        addDirectionGuidePoint(posMm);
        return;
    }
    if (currentTool_ == Tool::DrawPolygon) {
        if (!snapSuspended()) {
            posMm = findSnapPointMm(posMm).value_or(posMm);
        }
        const Vec2um v = sceneMmToModel(posMm);
        if (!pendingPolygonVertices_.empty()) {
            // Séquence Qt d'un double-clic : press/release/DOUBLECLICK/release —
            // la seconde pression émet aussi un clic simple, quasi au même
            // point que le dernier sommet posé. Ignoré ici pour ne pas poser
            // un sommet fantôme juste avant la fermeture (onCanvasDoubleClicked).
            const auto& last = pendingPolygonVertices_.back();
            const double dx = static_cast<double>(v.x.value - last.x.value);
            const double dy = static_cast<double>(v.y.value - last.y.value);
            if (dx * dx + dy * dy < 4.0) { // < 2 µm de l'un à l'autre
                return;
            }
        }
        pendingPolygonVertices_.push_back(v);
        updatePolygonPreview(posMm);
        updateDrawActionsState();
        statusBar()->showMessage(
            tr("Polygone : %1 sommet(s) — Entrée/double-clic/bouton ✓ pour fermer, Échap pour "
               "annuler, Retour arrière pour retirer le dernier sommet")
                .arg(pendingPolygonVertices_.size()));
        return;
    }
    if (currentTool_ == Tool::DrawSatinColumn) {
        if (!snapSuspended()) {
            posMm = findSnapPointMm(posMm).value_or(posMm);
        }
        const Vec2um v = sceneMmToModel(posMm);
        if (!pendingSatinPoints_.empty()) {
            // Même garde anti-doublon que le polygone : la séquence Qt d'un
            // double-clic pose aussi un clic simple juste avant.
            const auto& last = pendingSatinPoints_.back();
            const double dx = static_cast<double>(v.x.value - last.x.value);
            const double dy = static_cast<double>(v.y.value - last.y.value);
            if (dx * dx + dy * dy < 4.0) {
                return;
            }
        }
        pendingSatinPoints_.push_back(v);
        updateSatinColumnPreview(posMm);
        updateDrawActionsState();
        const bool nextIsSideB = pendingSatinPoints_.size() % 2 == 0;
        const auto pairs = pendingSatinPoints_.size() / 2;
        statusBar()->showMessage(
            nextIsSideB ? tr("Colonne satin : cliquez le côté B de la paire — Échap : annuler")
                        : tr("Colonne satin : %1 paire(s) posée(s) — cliquez le côté A de la paire "
                             "suivante — Entrée/double-clic : terminer (2 paires min.), Retour "
                             "arrière : retirer le dernier point")
                              .arg(pairs));
        return;
    }
    // Couche Vecteurs masquée : un clic « sans effet » sur une forme se dit au lieu de se taire.
    if (!showVectorsAct_->isChecked() && currentTool_ == Tool::Select && !mergeMode_ &&
        !objectsAtPointMm(project_, posMm).empty()) {
        statusBar()->showMessage(tr("Une forme vectorielle est sous le curseur, mais la couche "
                                    "Vecteurs est masquée (touche 3 pour l'afficher)."),
                                 6000);
    }
    // Priorité aux objets vectoriels lorsqu'ils sont affichés (hors fusion).
    if (showVectorsAct_->isChecked() && !mergeMode_) {
        // Même prédicat que Maj/Ctrl+clic et « sélectionner dessous » : visible, filtres
        // d'affichage, tolérance de clic (HP-UX-019). Le plus haut gagne ; un re-clic au même
        // point passe à l'objet suivant dessous (HP-UX-020).
        const std::vector<ObjectId> under = selectableObjectsAt(posMm);
        std::optional<ObjectId> hit;
        if (!under.empty()) {
            hit = cycleTarget(under, posMm);
        }
        if (hit) {
            // la sélection au canevas prime (Replace : T4b route Maj/Ctrl vers
            // applySelectionClick)
            setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {*hit}});
            const auto* object = project_.findObject(*hit);
            QString message = tr("Objet « %1 » — %2 morceau(x), nœuds déplaçables")
                                  .arg(QString::fromStdString(object->name))
                                  .arg(object->paths.size());
            if (under.size() > 1) {
                message += tr(" — %1/%2 sous le curseur (cliquez à nouveau pour le suivant)")
                               .arg(cycleIndex_ + 1)
                               .arg(under.size());
            }
            statusBar()->showMessage(message);
            displayImage(processed_);
            updateActions();
            return;
        }
        // Repli : colonne satin manuelle, dont l'objet vectoriel source est
        // délibérément invisible (§ colonne satin manuelle) — clic direct sur
        // le ruban (rail_a/rail_b), pas sur un objet vectoriel affiché. Un
        // guide satin (SatinGuideItem) n'a PAS le flag ItemIsMovable, donc un
        // clic dessus atteint aussi ce gestionnaire EN PLUS du sien propre —
        // reconstruire la scène ici, synchrone, détruirait l'item pendant son
        // propre mousePressEvent (même piège que les poignées ailleurs dans
        // ce fichier) : différé d'un cycle d'événements, comme elles.
        //
        // Inactif en mode guides/rails (satinGuideModeAct_/railEditModeAct_) :
        // dans ces modes, un clic sur le canevas cible délibérément un guide
        // ou un nœud de l'objet DÉJÀ ciblé, jamais une bascule de sélection
        // vers un autre objet satin (surtout critique si deux objets ont des
        // rubans qui se recouvrent, ex. un réseau à jonctions).
        const bool satinModeActive =
            (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked()) ||
            (railEditModeAct_ != nullptr && railEditModeAct_->isChecked());
        if (!satinModeActive) {
            if (auto* satinHit = satinEmbroideryAt(posMm)) {
                const ObjectId hitId = satinHit->id;
                const std::uint64_t generation = documentGeneration_;
                QTimer::singleShot(0, this, [this, hitId, generation] {
                    if (generation != documentGeneration_) {
                        return;
                    }
                    const auto* emb = project_.findEmbroidery(hitId);
                    if (emb == nullptr) {
                        return;
                    }
                    setSelection({.region = std::nullopt, .embroidery = hitId, .objects = {}});
                    statusBar()->showMessage(tr("Colonne satin « %1 » sélectionnée")
                                                 .arg(QString::fromStdString(emb->name)));
                    displayImage(processed_);
                    updateActions();
                });
                return;
            }
            // Repli « clic dans le vide » : ne touche PAS selectedEmbroidery_ en
            // mode guides/rails (cf. commentaire ci-dessus) — seul selectedObject_
            // (sélection d'objet vectoriel) est concerné dans ce cas, exactement
            // le comportement d'avant l'ajout du satin manuel.
            if (selectedObject_ || selectedEmbroidery_) {
                editSelection([](Selection& sel) {
                    sel.objects.clear();
                    sel.embroidery.reset();
                });
                displayImage(processed_);
                // updateActions() manquait ici (contrairement aux autres chemins
                // de sélection) : nécessaire pour que le mode d'édition des
                // points (Lot 8.2) sorte proprement quand la sélection est
                // perdue par un clic dans le vide plutôt que reportée sur un
                // autre objet.
                updateActions();
            }
        } else if (selectedObject_) {
            editSelection([](Selection& sel) { sel.objects.clear(); });
            displayImage(processed_);
            updateActions();
        }
    }

    if (!showSegAct_->isChecked() || !project_.segmentation) {
        return;
    }
    const auto px = mmToImagePixel(posMm);
    const auto clicked =
        px ? segmentation::region_at(*project_.segmentation, px->x(), px->y()) : std::nullopt;

    if (mergeMode_ && selectedRegion_ && clicked && !isRegionSelected(*clicked)) {
        // Toutes les régions sélectionnées sont absorbées par la région cliquée (couleur gardée).
        // Annulé par l'utilisateur (objets vectoriels liés) : le mode reste actif pour recliquer.
        const std::vector<RegionId> sources = selectedRegionIds();
        mergeAct_->setChecked(false);
        if (!mergeRegions(sources, *clicked)) {
            mergeAct_->setChecked(true);
        }
        return;
    }
    if (mergeMode_) {
        // Le mode reste actif : dire pourquoi ce clic n'a rien fusionné, au lieu de retomber
        // silencieusement dans une sélection simple.
        QString hint = tr("Fusion : cliquez la région cible sur la carte — Échap : annuler");
        if (clicked && selectedRegion_) {
            hint = tr("Fusion : cliquez une région NON sélectionnée (la cible) — Échap : annuler");
        }
        statusBar()->showMessage(hint);
        return;
    }

    // Clic simple : la sélection de régions est remplacée par la région cliquée (ou vidée).
    editSelection([clicked](Selection& sel) {
        sel.region = clicked;
        sel.extraRegions.clear();
    });
    if (clicked) {
        announceRegionSelection();
    }
    displayImage(processed_);
    updateActions();
}

bool MainWindow::regionFullyInside(RegionId id, int x0, int y0, int x1, int y1) const {
    const auto* region = project_.segmentation ? project_.segmentation->find(id) : nullptr;
    if (region == nullptr) {
        return false;
    }
    const auto& seg = *project_.segmentation;
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, seg.width - 1);
    y1 = std::min(y1, seg.height - 1);
    std::size_t inside = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            inside += seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                                 static_cast<std::size_t>(x)] == id.value;
        }
    }
    return inside == region->pixel_count;
}

bool MainWindow::isRegionSelected(RegionId id) const {
    const std::vector<RegionId> ids = selectedRegionIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void MainWindow::onSelectionClicked(QPointF posMm, SelectMode mode) {
    if (currentTool_ != Tool::Select) {
        return;
    }
    if (mode == SelectMode::Replace) {
        onCanvasClicked(posMm); // chemin historique, inchangé
        return;
    }
    // Maj/Ctrl + clic : ajout/bascule d'un objet vectoriel (affiché, hors fusion).
    // Régions : Maj/Ctrl + clic = sélection multiple de régions (cf. plus bas) ; broderies en
    // mono-sélection (Maj/Ctrl = Replace, documenté).
    if (showVectorsAct_->isChecked() && !mergeMode_) {
        const std::vector<ObjectId> under = selectableObjectsAt(posMm);
        if (!under.empty()) {
            applySelectionClick(under.front(), mode);
            return;
        }
        if (satinEmbroideryAt(posMm) != nullptr) {
            onCanvasClicked(posMm);
            return;
        }
    }
    if (showSegAct_->isChecked() && project_.segmentation) {
        const auto px = mmToImagePixel(posMm);
        const auto region =
            px ? segmentation::region_at(*project_.segmentation, px->x(), px->y()) : std::nullopt;
        if (region) {
            if (mergeMode_) {
                onCanvasClicked(posMm); // la région cliquée est la cible de la fusion
            } else {
                // Maj + clic : ajoute la région ; Ctrl + clic : la bascule (sélection multiple).
                selectRegions({*region}, mode);
            }
            return;
        }
    }
    applySelectionClick(std::nullopt, mode); // vide : sans effet pour Maj/Ctrl
}

void MainWindow::onSelectionRectangle(QRectF rectMm, SelectMode mode, bool crossing) {
    // Carte des régions affichée et aucun objet vectoriel touché par le cadre : le cadre
    // sélectionne des régions (fenêtre : régions entièrement dans le cadre ; croisement :
    // régions que le cadre touche). Objets affichés ET touchés : ce sont eux qui sont saisis,
    // et un message dit comment atteindre les régions (sinon le cadre semblait ne rien faire).
    const bool vectorsShown = showVectorsAct_->isChecked();
    std::vector<ObjectId> objectHits;
    if (vectorsShown && currentTool_ == Tool::Select && !mergeMode_) {
        objectHits = objectsInRectangleMm(project_, rectMm, crossing);
        // Même règle que le rendu : un objet masqué par les filtres d'affichage (type, couleur,
        // taille) n'est pas saisi par le cadre, sauf s'il est déjà sélectionné (donc affiché).
        const std::vector<ObjectId> selected = currentSelection().objects;
        objectHits.erase(std::remove_if(objectHits.begin(), objectHits.end(),
                                        [&](ObjectId id) {
                                            const document::VectorObject* vec =
                                                project_.findObject(id);
                                            // Calque verrouillé (HP-OBJ-024) : jamais saisi par le
                                            // cadre.
                                            if (vec != nullptr && project_.shapeEditLocked(id)) {
                                                return true;
                                            }
                                            return vec != nullptr &&
                                                   std::find(selected.begin(), selected.end(),
                                                             id) == selected.end() &&
                                                   !vectorPassesFilter(*vec);
                                        }),
                         objectHits.end());
    }
    const bool regionsUsable = currentTool_ == Tool::Select && !mergeMode_ &&
                               showSegAct_->isChecked() && project_.segmentation &&
                               !processed_.empty();
    if (regionsUsable && vectorsShown && !objectHits.empty()) {
        statusBar()->showMessage(
            tr("Le cadre a saisi des objets vectoriels. Masquez-les (menu Affichage) pour "
               "sélectionner des régions au cadre."),
            6000);
    }
    if (regionsUsable && objectHits.empty()) {
        const double mmPerPx = project_.mm_per_px.value;
        const double left = -processed_.width * mmPerPx / 2.0;
        const double top = -processed_.height * mmPerPx / 2.0;
        const auto toPx = [&](double mm, double origin) {
            return static_cast<int>(std::floor((mm - origin) / mmPerPx));
        };
        const int x0 = toPx(rectMm.left(), left);
        const int y0 = toPx(rectMm.top(), top);
        const int x1 = toPx(rectMm.right(), left);
        const int y1 = toPx(rectMm.bottom(), top);
        std::vector<RegionId> hits =
            segmentation::regions_in_rect(*project_.segmentation, x0, y0, x1, y1);
        if (!crossing) {
            hits.erase(
                std::remove_if(hits.begin(), hits.end(),
                               [&](RegionId id) { return !regionFullyInside(id, x0, y0, x1, y1); }),
                hits.end());
        }
        // Remplacer/Ajouter/Basculer comme pour un clic ; cadre vide en mode Replace : désélection.
        if (hits.empty() && mode == SelectMode::Replace) {
            editSelection([](Selection& sel) {
                sel.region.reset();
                sel.extraRegions.clear();
                sel.objects.clear(); // cadre vide : aussi la sélection d'objets (comme sans carte)
                sel.embroidery.reset();
            });
            displayImage(processed_);
            updateActions();
        } else if (!hits.empty()) {
            selectRegions(hits, mode);
        }
        return;
    }
    if (currentTool_ != Tool::Select || !vectorsShown || mergeMode_) {
        return;
    }
    // Fenêtre (glisser vers la droite) : objets entièrement dans le cadre ;
    // croisement (vers la gauche) : objets que le cadre coupe. Détection pure.
    applySelectionRectangle(objectHits, mode);
}

void MainWindow::onSelectBelow(QPointF posMm, QPoint globalPos, SelectMode mode) {
    if (currentTool_ == Tool::Select && !showVectorsAct_->isChecked() && !mergeMode_) {
        statusBar()->showMessage(
            tr("La couche Vecteurs est masquée (touche 3 pour l'afficher) : rien à sélectionner."),
            6000);
        return;
    }
    if (currentTool_ != Tool::Select || !showVectorsAct_->isChecked() || mergeMode_) {
        return;
    }
    const std::vector<ObjectId> under = selectableObjectsAt(posMm);
    if (under.empty()) {
        statusBar()->showMessage(tr("Aucun objet sous le curseur."));
        return;
    }
    if (selectBelowMenu_ != nullptr) {
        selectBelowMenu_->close();
    }
    // Menu non bloquant (popup) : les objets sous le point, du plus haut au plus bas.
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("selectBelowMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setAccessibleName(tr("Objets sous le curseur"));
    // Noms vides ou en double : suffixe « (#id) » pour les distinguer.
    QMap<QString, int> nameCount;
    for (const ObjectId id : under) {
        if (const auto* object = project_.findObject(id)) {
            ++nameCount[QString::fromStdString(object->name)];
        }
    }
    for (const ObjectId id : under) {
        const auto* object = project_.findObject(id);
        if (object == nullptr) {
            continue;
        }
        QString label = QString::fromStdString(object->name);
        const bool ambiguous = object->name.empty() || nameCount.value(label) > 1;
        if (object->name.empty()) {
            label = tr("(sans nom)");
        }
        if (ambiguous) {
            label += QStringLiteral(" (#%1)").arg(id.value);
        }
        auto* act = menu->addAction(label);
        QPixmap swatch(12, 12);
        swatch.fill(QColor(object->rgb[0], object->rgb[1], object->rgb[2]));
        act->setIcon(QIcon(swatch));
        connect(act, &QAction::triggered, this,
                [this, id, mode] { applySelectionClick(id, mode); });
    }
    selectBelowMenu_ = menu;
    menu->popup(globalPos);
}

// SELECTION-MUTATOR:BEGIN
// Seul bloc autorisé à écrire selectedObject_/selectedRegion_/selectedEmbroidery_/
// multiSelection_ (garde tests/check_selection_single_mutator.cmake). Tout autre
// site passe par setSelection()/editSelection().
void MainWindow::setSelection(Selection selection) {
    selection = desktop::normalize_selection(std::move(selection), [this](RegionId r) {
        return project_.segmentation && project_.segmentation->find(r) != nullptr;
    });
    std::vector<ObjectId> objects = std::move(selection.objects);
    selectedRegion_ = selection.region;
    extraRegions_ = std::move(selection.extraRegions);
    selectedEmbroidery_ = selection.embroidery;
    if (objects.empty()) {
        selectedObject_.reset();
    } else {
        selectedObject_ = objects.back();
    }
    if (objects.size() > 1) {
        multiSelection_ = std::move(objects);
    } else {
        multiSelection_.clear(); // cas legacy : 0 ou 1 objet porté par selectedObject_
    }
    Q_ASSERT(multiSelection_.empty() ||
             (selectedObject_ && multiSelection_.back() == *selectedObject_));
    Q_ASSERT(multiSelection_.empty() || (!selectedRegion_ && !selectedEmbroidery_));
}

// SELECTION-MUTATOR:END

void MainWindow::translateObjects(const std::vector<ObjectId>& ids, Vec2um delta, bool coalesce) {
    if (ids.empty()) {
        return;
    }
    if (ids.size() == 1) {
        auto cmd = std::make_unique<commands::TranslateVectorObjectCommand>(ids.front(), delta);
        cmd->setCoalescable(coalesce);
        undoStack_.execute(std::move(cmd), project_);
    } else {
        auto composite = std::make_unique<commands::CompositeCommand>(
            tr("Déplacer %1 objets").arg(ids.size()).toStdString());
        std::string key = "translate-set";
        for (const ObjectId id : ids) {
            auto cmd = std::make_unique<commands::TranslateVectorObjectCommand>(id, delta);
            cmd->setCoalescable(coalesce);
            key += ":" + std::to_string(id.value);
            composite->add(std::move(cmd));
        }
        if (coalesce) {
            composite->setMergeKey(std::move(key));
        }
        undoStack_.execute(std::move(composite), project_);
    }
    refreshImage();
    updateActions();
}

void MainWindow::editSelection(const std::function<void(Selection&)>& edit) {
    Selection sel = currentSelection();
    edit(sel);
    setSelection(std::move(sel));
}

MainWindow::Selection MainWindow::currentSelection() const {
    return Selection{selectedRegion_, selectedEmbroidery_, selectedObjectIds(), extraRegions_};
}

std::vector<ObjectId> MainWindow::selectedObjectIds() const {
    if (!multiSelection_.empty()) {
        return multiSelection_;
    }
    if (selectedObject_) {
        return {*selectedObject_};
    }
    return {};
}

bool MainWindow::isObjectSelected(ObjectId id) const {
    if (!multiSelection_.empty()) {
        return std::find(multiSelection_.begin(), multiSelection_.end(), id) !=
               multiSelection_.end();
    }
    return selectedObject_ && *selectedObject_ == id;
}

void MainWindow::pruneSelection() {
    Selection sel = currentSelection();
    bool changed = false;
    if (sel.region &&
        (!project_.segmentation || project_.segmentation->find(*sel.region) == nullptr)) {
        sel.region.reset();
        changed = true;
    }
    if (sel.embroidery && project_.findEmbroidery(*sel.embroidery) == nullptr) {
        sel.embroidery.reset();
        changed = true;
    }
    const auto dead = [this](ObjectId id) { return project_.findObject(id) == nullptr; };
    const auto removed = std::erase_if(sel.objects, dead);
    changed = changed || removed != 0;
    if (changed) {
        setSelection(std::move(sel)); // le dernier restant devient le principal
    }
}

bool MainWindow::checkSelectionInvariants() const {
    if (!desktop::selection_state_is_consistent(selectedObject_, multiSelection_,
                                                selectedRegion_ || selectedEmbroidery_)) {
        return false;
    }
    for (const ObjectId id : selectedObjectIds()) {
        if (project_.findObject(id) == nullptr) {
            return false;
        }
    }
    if (selectedEmbroidery_ && project_.findEmbroidery(*selectedEmbroidery_) == nullptr) {
        return false;
    }
    if (selectedRegion_ &&
        (!project_.segmentation || project_.segmentation->find(*selectedRegion_) == nullptr)) {
        return false;
    }
    return true;
}

void MainWindow::selectionChanged() {
    Q_ASSERT(checkSelectionInvariants());
    displayImage(processed_);
    updateActions();
}

void MainWindow::applySelectionClick(std::optional<ObjectId> hit, SelectMode mode) {
    Selection sel = currentSelection();
    if (!hit) {
        // Clic dans le vide : Replace désélectionne objets et broderie (la région
        // reste à la charge du repli région de onCanvasClicked) ; Maj/Ctrl : rien.
        if (mode != SelectMode::Replace) {
            return;
        }
        sel.objects.clear();
        sel.embroidery.reset();
    } else {
        if (project_.findObject(*hit) == nullptr) {
            return;
        }
        const auto it = std::find(sel.objects.begin(), sel.objects.end(), *hit);
        switch (mode) {
        case SelectMode::Replace:
            sel.objects = {*hit};
            break;
        case SelectMode::Add:
            if (it != sel.objects.end()) {
                return; // déjà présent : sans effet (le principal ne change pas)
            }
            sel.objects.push_back(*hit);
            break;
        case SelectMode::Toggle:
            if (it != sel.objects.end()) {
                sel.objects.erase(it); // retirer le principal promeut le précédent
            } else {
                sel.objects.push_back(*hit); // devient le principal
            }
            break;
        }
        sel.region.reset();
        sel.embroidery.reset();
    }
    setSelection(std::move(sel));
    selectionChanged();
}

void MainWindow::applySelectionRectangle(const std::vector<ObjectId>& hits, SelectMode mode) {
    Selection sel = currentSelection();
    std::vector<ObjectId> valid;
    for (const ObjectId id : hits) {
        if (project_.findObject(id) != nullptr &&
            std::find(valid.begin(), valid.end(), id) == valid.end()) {
            valid.push_back(id);
        }
    }
    switch (mode) {
    case SelectMode::Replace:
        sel.objects = valid; // rectangle vide : désélectionne les objets
        break;
    case SelectMode::Add: {
        bool added = false;
        for (const ObjectId id : valid) {
            if (std::find(sel.objects.begin(), sel.objects.end(), id) == sel.objects.end()) {
                sel.objects.push_back(id);
                added = true;
            }
        }
        if (!added) {
            return;
        }
        break;
    }
    case SelectMode::Toggle: {
        if (valid.empty()) {
            return;
        }
        for (const ObjectId id : valid) {
            const auto it = std::find(sel.objects.begin(), sel.objects.end(), id);
            if (it != sel.objects.end()) {
                sel.objects.erase(it);
            } else {
                sel.objects.push_back(id);
            }
        }
        break;
    }
    }
    sel.region.reset();
    sel.embroidery.reset();
    setSelection(std::move(sel));
    selectionChanged();
}

void MainWindow::deleteSelection() {
    // Mode « Éditer les points » avec des points sélectionnés : Suppr retire ces POINTS. Sans
    // sélection de points, le comportement historique demeure (supprime l'objet, annulable).
    if (stitchPointEditActive() && !stitchSelection_.empty()) {
        editSelectedStitchPoints(0);
        return;
    }
    // Objet de broderie choisi (dock Ordre/Document) : la broderie seule, forme
    // source conservée -- sauf proxy invisible (colonne satin manuelle), dont la
    // suppression emporte la source (comme « Supprimer » du menu contextuel).
    if (selectedEmbroidery_) {
        const auto* emb = project_.findEmbroidery(*selectedEmbroidery_);
        if (emb == nullptr) {
            return;
        }
        const QString name = QString::fromStdString(emb->name);
        const auto* source = project_.findObject(emb->source_vector);
        if (source != nullptr && !source->visible) {
            undoStack_.execute(std::make_unique<commands::RemoveVectorObjectCommand>(source->id),
                               project_);
        } else {
            undoStack_.execute(removeEmbroideryCommand(emb->id), project_);
        }
        setSelection({});
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("« %1 » supprimé.").arg(name));
        return;
    }
    // Région (comportement historique inchangé, RemoveRegionCommand) : testée APRÈS la
    // broderie car le dock Ordre garde la région en sélectionnant une broderie.
    if (selectedRegion_ && project_.segmentation) {
        deleteSelectedRegion();
        return;
    }
    // Objet(s) vectoriel(s) : un seul pas d'annulation (CompositeCommand si > 1).
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty()) {
        return;
    }
    if (ids.size() == 1) {
        deleteVectorObject(ids.front()); // exécute, élague la sélection, rafraîchit
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Supprimer %1 objets").arg(ids.size()).toStdString());
    for (const ObjectId id : ids) {
        composite->add(std::make_unique<commands::RemoveVectorObjectCommand>(id));
    }
    undoStack_.execute(std::move(composite), project_);
    setSelection({});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 objets supprimés.").arg(ids.size()));
}

void MainWindow::deleteSelectedRegion() {
    deleteSelectedRegions();
}

void MainWindow::recolorSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    const auto* region = project_.segmentation->find(*selectedRegion_);
    if (region == nullptr) {
        return;
    }
    const QColor initial(region->rgb[0], region->rgb[1], region->rgb[2]);
    const std::vector<RegionId> ids = selectedRegionIds();
    const QColor color =
        QColorDialog::getColor(initial, this,
                               ids.size() == 1 ? tr("Couleur de la région")
                                               : tr("Couleur des %1 régions").arg(ids.size()));
    if (!color.isValid()) {
        return;
    }
    recolorRegions(ids, {static_cast<std::uint8_t>(color.red()),
                         static_cast<std::uint8_t>(color.green()),
                         static_cast<std::uint8_t>(color.blue())});
}

} // namespace openstitch::desktop
