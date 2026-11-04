// SPDX-License-Identifier: Apache-2.0
// Menu contextuel du canevas, suppression, duplication, décalage.
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

bool MainWindow::stitchParamsForType(const document::EmbroideryObject& emb, int type,
                                     document::StitchParams& params, std::string& label,
                                     std::optional<std::vector<geometry::PathSet>>& restoredContour,
                                     QString& error) {
    switch (type) {
    case 0: { // contour cousu
        document::RunningStitchParams rp;
        rp.repeats = 3;
        params = rp;
        label = "Type : contour";
        return true;
    }
    case 1: // tatami
        params = document::TatamiParams{};
        label = "Type : tatami";
        return true;
    case 5: // motif répété le long du contour (HP-STI-002)
        params = document::MotifRunParams{};
        label = "Type : motif le long du contour";
        return true;
    case 6: // remplissage à motif (HP-STI-006)
        params = document::MotifFillParams{};
        label = "Type : remplissage à motif";
        return true;
    case 4: // blanket / zigzag le long du contour (HP-STI-003)
        params = document::BlanketStitchParams{};
        label = "Type : blanket";
        return true;
    case 7: // remplissage concentrique (HP-STI-007)
        params = document::ContourFillParams{};
        label = "Type : remplissage concentrique";
        return true;
    case 8: // remplissage radial (HP-STI-008)
        params = document::RadialFillParams{};
        label = "Type : remplissage radial";
        return true;
    case 9: // remplissage en spirale (HP-STI-009)
        params = document::SpiralFillParams{};
        label = "Type : remplissage en spirale";
        return true;
    case 10: // remplissage croisé (HP-STI-014)
        params = document::CrosshatchFillParams{};
        label = "Type : remplissage croisé";
        return true;
    case 11: // point de croix (HP-STI-012)
        params = document::CrossStitchParams{};
        label = "Type : point de croix";
        return true;
    case 12: // appliqué (HP-SPEC-001)
        params = document::AppliqueParams{};
        label = "Type : appliqué";
        return true;
    case 3: { // remplissage directionnel (réglages du tatami repris s'il y a lieu)
        auto directional = directionalParamsFor(emb);
        if (!directional) {
            error = tr("Aucun contour source pour le remplissage directionnel.");
            return false;
        }
        params = std::move(*directional);
        label = "Type : remplissage directionnel";
        return true;
    }
    case 2: { // satin par squelette et traversées orientées
        const auto* currentSource = project_.findObject(emb.source_vector);
        if (currentSource == nullptr || currentSource->paths.empty()) {
            error = tr("Aucun contour source pour construire le satin.");
            return false;
        }
        // Le satin suit la région telle que segmentée, pas le contour agrandi par le
        // recouvrement des tatamis voisins (sinon il déborde de sa zone).
        restoredContour = pristineSatinContour(*currentSource);
        document::VectorObject restoredCopy;
        const document::VectorObject* source = currentSource;
        if (restoredContour) {
            restoredCopy = *currentSource;
            restoredCopy.paths = *restoredContour;
            source = &restoredCopy;
        }
        document::AutoSatinParams sp;
        sp.strategy = document::kDefaultAutoSatinStrategy;
        AutoSatinPreview preview = previewAutoSatin(*source, sp);
        QString methodNote;
        if (preview.columns == 0) {
            // Le squelette refuse la forme : on essaie les autres méthodes. La méthode retenue
            // est écrite sur l'objet (visible dans l'inspecteur) et nommée dans l'historique.
            if (const auto alt = firstWorkingAutoSatinStrategy(*source, sp);
                alt && *alt != sp.strategy) {
                sp.strategy = *alt;
                preview = previewAutoSatin(*source, sp);
                methodNote = autoSatinStrategyLabel(*alt);
            }
        }
        if (preview.columns == 0) {
            // Refus nommé, jamais contourné par une géométrie de moindre qualité.
            error = tr("Aucune colonne satin n'a pu être construite pour cette région :\n%1\n\n"
                       "Essayez un tatami.")
                        .arg(preview.messages.isEmpty() ? tr("forme non exploitable")
                                                        : preview.messages.join(QLatin1Char('\n')));
            return false;
        }
        params = sp;
        label = methodNote.isEmpty() ? std::string("Type : satin")
                                     : ("Type : satin (" + methodNote.toStdString() + ")");
        return true;
    }
    default:
        error = tr("Type de points inconnu.");
        return false;
    }
}

void MainWindow::setStitchType(ObjectId embroideryId, int type) {
    auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr || stitchTypeIndex(*emb) == type) {
        return;
    }
    document::StitchParams params;
    std::string label;
    std::optional<std::vector<geometry::PathSet>> restoredContour; // satin : contour brut
    QString error;
    if (!stitchParamsForType(*emb, type, params, label, restoredContour, error)) {
        QMessageBox::warning(this, type == 2 ? tr("Satin impossible") : tr("Conversion impossible"),
                             error);
        return;
    }
    // `ConvertFillGroupCommand` (pas `SetStitchTypeCommand` seule) : un
    // réseau satin auto-généré en plusieurs sections partage un seul
    // `source_vector` entre plusieurs `EmbroideryObject` -- `embroideryId`
    // ici n'en désigne qu'UN (le premier trouvé par `embroideryForVector`).
    // Sans ce garde-fou, changer de type ne convertirait que cette
    // première section et laisserait les autres en satin réel, inchangées
    // -- exactement le défaut réel signalé (« résidu de satin qui reste
    // même en revenant en tatami », 2026-09-04, cf. le commentaire de la
    // commande pour le détail complet). Elle ne regroupe que les sections satin à rails,
    // pas les couches indépendantes d'un fondu partageant elles aussi un source_vector.
    using Convert = commands::ConvertFillGroupCommand;
    auto convert = std::make_unique<Convert>(embroideryId, std::move(params), std::move(label));
    if (restoredContour) {
        auto group = std::make_unique<commands::CompositeCommand>("Type : satin");
        group->add(std::make_unique<commands::SetVectorPathsCommand>(
            emb->source_vector, *restoredContour, "Contour brut de la région"));
        group->add(std::move(convert));
        undoStack_.execute(std::move(group), project_);
    } else {
        undoStack_.execute(std::move(convert), project_);
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
}

void MainWindow::onCanvasContextMenu(QPointF posMm, QPoint globalPos) {
    const auto hit = objectAt(posMm);
    // Carte des régions affichée, aucun objet sous le curseur : menu de la région pointée.
    if (!hit && showSegAct_ != nullptr && showSegAct_->isChecked() && project_.segmentation &&
        satinEmbroideryAt(posMm) == nullptr) {
        const auto px = mmToImagePixel(posMm);
        if (px) {
            if (const auto region =
                    segmentation::region_at(*project_.segmentation, px->x(), px->y())) {
                showRegionContextMenu(*region, globalPos);
                return;
            }
        }
    }
    QMenu menu(this);
    document::EmbroideryObject* emb = nullptr;
    std::optional<ObjectId> vecId; // objet vectoriel à supprimer/dupliquer, si pertinent

    if (hit && hasMultiSelection() && isObjectSelected(*hit)) {
        // Clic droit sur un membre de la multi-sélection : on la garde ; seules les
        // entrées qui valent pour tout l'ensemble sont proposées (les entrées
        // mono-objet -- type de points, dupliquer, décaler -- sont absentes).
        auto* title = menu.addAction(tr("%1 objets").arg(multiSelection_.size()));
        title->setEnabled(false);
        menu.addSeparator();
        auto* multiTypeMenu = menu.addMenu(tr("Type de points (tous)"));
        for (int t = 0; t < kStitchTypeCount; ++t) {
            auto* act = multiTypeMenu->addAction(stitchTypeLabel(t));
            connect(act, &QAction::triggered, this, [this, t] { setStitchTypeForSelection(t); });
        }
        menu.addSeparator();
        auto* deleteAllAct = menu.addAction(tr("&Supprimer %1 objets").arg(multiSelection_.size()));
        deleteAllAct->setObjectName(QStringLiteral("contextDeleteSelection"));
        connect(deleteAllAct, &QAction::triggered, this, &MainWindow::deleteSelection);
    } else if (hit) {
        setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {*hit}});
        const auto* vec = project_.findObject(*hit);
        emb = embroideryForVector(*hit);
        if (vec != nullptr) {
            vecId = vec->id;
            auto* title = menu.addAction(QString::fromStdString(vec->name));
            title->setEnabled(false);
            menu.addSeparator();
        }
    } else if (auto* satinHit = satinEmbroideryAt(posMm)) {
        // Repli : colonne satin manuelle (objet vectoriel source invisible,
        // cf. satinEmbroideryAt) — sans quoi ni le menu « Type de points » ni
        // le débogage ne seraient jamais accessibles pour ces objets.
        emb = satinHit;
        vecId = emb->source_vector; // proxy caché : sa suppression entraîne le satin
        setSelection({.region = std::nullopt, .embroidery = emb->id, .objects = {}});
        auto* title = menu.addAction(QString::fromStdString(emb->name));
        title->setEnabled(false);
        menu.addSeparator();
    }

    if (emb != nullptr) {
        const ObjectId embId = emb->id;
        auto* typeMenu = menu.addMenu(tr("Type de points"));
        const int current = stitchTypeIndex(*emb);
        for (int t = 0; t < kStitchTypeCount; ++t) {
            auto* act = typeMenu->addAction(stitchTypeLabel(t));
            act->setCheckable(true);
            act->setChecked(t == current);
            connect(act, &QAction::triggered, this, [this, embId, t] { setStitchType(embId, t); });
        }
        if (emb->is_tatami()) {
            auto* rot = menu.addAction(tr("Orientation du remplissage…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
        }
        if (emb->is_tatami() || emb->is_directional()) {
            auto* contour = menu.addAction(tr("Convertir en remplissage concentrique"));
            connect(contour, &QAction::triggered, this,
                    [this, embId] { convertToContourFill(embId); });
        }
        if (emb->is_tatami() || emb->is_directional()) {
            auto* autoGuide = menu.addAction(tr("Generer un guide depuis la forme"));
            connect(autoGuide, &QAction::triggered, this,
                    &MainWindow::generateDirectionGuideFromShape);
        }
        if (emb->is_directional()) {
            auto* guides = menu.addAction(tr("Guides de direction…"));
            connect(guides, &QAction::triggered, this, [this, embId] {
                editSelection([embId](Selection& sel) { sel.embroidery = embId; });
                directionGuideModeAct_->setChecked(true);
            });
        }
        if (hit) {
            auto* addLayer = menu.addAction(tr("Ajouter une couche…"));
            addLayer->setObjectName(QStringLiteral("contextAddLayer"));
            connect(addLayer, &QAction::triggered, this,
                    [this, embId] { addLayerInteractive(embId); });
        }
        // Nombre de couches de la forme : précise l'effet des entrées de suppression ci-dessous.
        const auto layerCount =
            std::count_if(project_.embroidery_objects.begin(), project_.embroidery_objects.end(),
                          [&](const auto& e) { return e.source_vector == emb->source_vector; });
        menu.addSeparator();
        const std::uint32_t embColorKey = colorKeyOf(emb->rgb);
        const int embFilterType = stitchFilterFamily(stitchTypeIndex(*emb));
        connect(menu.addAction(tr("Afficher uniquement cette couleur")), &QAction::triggered, this,
                [this, embColorKey] { isolateFilterColor(embColorKey); });
        connect(menu.addAction(tr("Afficher uniquement ce type de points")), &QAction::triggered,
                this, [this, embFilterType] { isolateFilterType(embFilterType); });
        menu.addSeparator();
        // Supprimer la broderie seule n'a de sens que si la forme source
        // reste utilisable après (cas d'un vrai objet vectoriel visible,
        // convertible en un autre type) — pas pour le proxy caché du satin
        // manuel, où « Supprimer » plus bas fait déjà tout disparaître.
        if (hit) {
            auto* removeStitchAct =
                menu.addAction(layerCount > 1 ? tr("Supprimer la couche « %1 » (garder la forme)")
                                                    .arg(QString::fromStdString(emb->name))
                                              : tr("Supprimer la broderie (garder la forme)"));
            connect(removeStitchAct, &QAction::triggered, this,
                    [this, embId] { deleteEmbroideryObjectOnly(embId); });
        }
        // Entrée de diagnostic réservée aux développeurs : masquée sauf si
        // OPENSTITCH_DEBUG=1 (variable d'environnement) ou clé QSettings « debug/menu » à vrai.
        if (debugMenuEnabled()) {
            auto* debugAct = menu.addAction(tr("&Déboguer : afficher toutes les données…"));
            connect(debugAct, &QAction::triggered, this, [this, embId] { showDebugDump(embId); });
        }
    } else if (hit && vecId) {
        // HP-UX-028 : une forme sans couture propose les mêmes quatre types de points (création
        // immédiate, un pas d'annulation, zéro dialogue).
        auto* typeMenu = menu.addMenu(tr("Type de points"));
        typeMenu->setObjectName(QStringLiteral("contextStitchTypeMenu"));
        for (int t = 0; t < kStitchTypeCount; ++t) {
            auto* act = typeMenu->addAction(stitchTypeLabel(t));
            connect(act, &QAction::triggered, this, [this, t] { setStitchTypeForSelection(t); });
        }
    }

    if (vecId) {
        menu.addSeparator();
        const ObjectId targetVecId = *vecId;
        if (hit) {
            auto* dupAct = menu.addAction(tr("Dupliquer"));
            connect(dupAct, &QAction::triggered, this,
                    [this, targetVecId] { duplicateVectorObject(targetVecId); });
            auto* offsetAct = menu.addAction(tr("Décaler…"));
            connect(offsetAct, &QAction::triggered, this,
                    [this, targetVecId] { offsetVectorObject(targetVecId); });
        }
        // Pas de raccourci Suppr ici : « Supprimer la sélection »
        // (action_deleteRegion, universelle) le porte déjà au niveau fenêtre — un
        // second raccourci identique créerait une ambiguïté Qt au lieu d'agir.
        const auto shapeLayers =
            std::count_if(project_.embroidery_objects.begin(), project_.embroidery_objects.end(),
                          [&](const auto& e) { return e.source_vector == targetVecId; });
        auto* deleteAct = menu.addAction(
            shapeLayers > 1 ? tr("&Supprimer la forme et ses %1 couches").arg(shapeLayers)
                            : tr("&Supprimer"));
        connect(deleteAct, &QAction::triggered, this,
                [this, targetVecId] { deleteVectorObject(targetVecId); });
    }

    // Outils de sélection (Édition ▸ Sélection) : mêmes actions, accessibles au clic droit.
    if (const auto* selectionMenu = findChild<QMenu*>(QStringLiteral("menu_selection"))) {
        if (!menu.isEmpty()) {
            menu.addSeparator();
        }
        QMenu* shared = menu.addMenu(tr("Sélection"));
        for (QAction* act : selectionMenu->actions()) {
            if (act->menu() == nullptr) {
                shared->addAction(act);
            }
        }
    }

    // Filtres d'affichage actifs : toujours pouvoir tout réafficher, même sur le fond vide.
    const bool filtersActive =
        !hiddenColors_.empty() || minAreaMm2_ > 0.0 ||
        std::find(showType_.begin(), showType_.end(), false) != showType_.end();
    if (filtersActive) {
        if (!menu.isEmpty()) {
            menu.addSeparator();
        }
        connect(menu.addAction(tr("Tout afficher (filtres)")), &QAction::triggered, this,
                &MainWindow::showAllFilters);
    }

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
    // Calques accessibles partout (même sans objet sous le curseur).
    auto* layers = menu.addMenu(tr("Plans d'affichage"));
    for (QAction* act : {showImageAct_, showSegAct_, showVectorsAct_, showStitchesAct_}) {
        if (act != nullptr) {
            layers->addAction(act);
        }
    }

    displayImage(processed_); // reflète la sélection éventuelle
    updateActions();
    menu.exec(globalPos);
}

void MainWindow::deleteVectorObject(ObjectId id) {
    const auto* obj = project_.findObject(id);
    const QString name = obj != nullptr ? QString::fromStdString(obj->name) : tr("objet");
    undoStack_.execute(std::make_unique<commands::RemoveVectorObjectCommand>(id), project_);
    // L'objet supprimé quitte la sélection (principal compris : le précédent
    // est promu) ; la broderie rattachée disparaît avec lui : si elle était
    // ciblée, la sélection ne doit pas pointer dans le vide.
    pruneSelection();
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("« %1 » supprimé.").arg(name));
}

void MainWindow::deleteEmbroideryObjectOnly(ObjectId id) {
    const auto* obj = project_.findEmbroidery(id);
    const QString name = obj != nullptr ? QString::fromStdString(obj->name) : tr("objet");
    undoStack_.execute(removeEmbroideryCommand(id), project_);
    editSelection([id](Selection& sel) {
        if (sel.embroidery == id) {
            sel.embroidery.reset();
        }
    });
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Broderie « %1 » supprimée (forme conservée).").arg(name));
}

void MainWindow::duplicateVectorObject(ObjectId id) {
    const auto* source = project_.findObject(id);
    if (source == nullptr) {
        return;
    }
    document::VectorObject copy = *source;
    copy.id = project_.object_ids.next();
    copy.name = tr("%1 (copie)").arg(QString::fromStdString(source->name)).toStdString();
    const ObjectId copyId = copy.id;
    // Léger décalage (2 mm) pour que la copie reste visible distinctement au
    // lieu de se superposer exactement à l'original.
    constexpr Micrometers kOffset{2'000};
    const QString sourceName = QString::fromStdString(source->name);
    // La copie emporte TOUTES les couches de couture de la forme (mêmes réglages, même ordre) ;
    // la translation déplace aussi leur géométrie propre (guides, rails). Un seul pas d'annulation.
    auto composite = std::make_unique<commands::CompositeCommand>(tr("Dupliquer").toStdString());
    composite->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(copy)));
    const int layers = addEmbroideryCopies(*composite, id, copyId);
    composite->add(std::make_unique<commands::TranslateVectorObjectCommand>(
        copyId, Vec2um{kOffset, -kOffset}));
    undoStack_.execute(std::move(composite), project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {copyId}});
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        layers > 1 ? tr("« %1 » dupliqué avec ses %2 couches.").arg(sourceName).arg(layers)
                   : tr("« %1 » dupliqué.").arg(sourceName));
}

int MainWindow::addEmbroideryCopies(commands::CompositeCommand& composite, ObjectId sourceId,
                                    ObjectId copyId) {
    int count = 0;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.source_vector != sourceId) {
            continue;
        }
        document::EmbroideryObject copy = emb;
        copy.id = project_.object_ids.next();
        copy.source_vector = copyId;
        composite.add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(copy)));
        ++count;
    }
    return count;
}

void MainWindow::duplicateAndTranslate(const std::vector<ObjectId>& ids, Vec2um delta) {
    // Copie(s) + translation des COPIES = UN pas d'annulation ; les originaux ne bougent pas.
    auto composite =
        std::make_unique<commands::CompositeCommand>(tr("Dupliquer en déplaçant").toStdString());
    std::vector<ObjectId> copies;
    for (const ObjectId id : ids) {
        const auto* source = project_.findObject(id);
        if (source == nullptr) {
            continue;
        }
        document::VectorObject copy = *source;
        copy.id = project_.object_ids.next();
        copy.name = tr("%1 (copie)").arg(QString::fromStdString(source->name)).toStdString();
        copies.push_back(copy.id);
        composite->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(copy)));
        addEmbroideryCopies(*composite, id, copies.back());
        composite->add(
            std::make_unique<commands::TranslateVectorObjectCommand>(copies.back(), delta));
    }
    if (copies.empty()) {
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = copies});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 objet(s) dupliqué(s) et déplacé(s).").arg(copies.size()));
}

void MainWindow::offsetVectorObject(ObjectId id) {
    bool ok = false;
    const double offsetMm = QInputDialog::getDouble(
        this, tr("Décaler la forme"),
        tr("Décalage (mm) — positif : agrandit vers l'extérieur ; négatif : rétrécit vers "
           "l'intérieur"),
        2.0, -50.0, 50.0, 2, &ok);
    if (!ok || offsetMm == 0.0) {
        return;
    }
    // inset_path_set : delta > 0 = retrait intérieur -- convention inverse de
    // « positif agrandit » côté utilisateur, d'où le signe opposé ici.
    offsetVectorObjectCore(id, to_micrometers(Millimeters{-offsetMm}));
}

void MainWindow::offsetVectorObjectCore(ObjectId id, Micrometers delta) {
    const auto* source = project_.findObject(id);
    if (source == nullptr) {
        return;
    }
    // Aplatit d'abord les courbes (nœuds Lisses) : Clipper2 (sous
    // inset_path_set) travaille sur des polygones bruts, sans notion de
    // tangente -- même raison qu'à l'export DXF (geometry::flatten est la
    // même fonction, même tolérance).
    static constexpr Micrometers kFlattenTolerance{50};
    const auto flattenToPath = [](const geometry::Path& path) {
        const geometry::Polyline flat = geometry::flatten(path, kFlattenTolerance);
        geometry::Path out;
        out.closed = flat.closed;
        out.nodes.reserve(flat.points.size());
        for (const Vec2um& p : flat.points) {
            out.nodes.push_back(
                geometry::PathNode{p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
        }
        return out;
    };

    document::VectorObject result;
    result.id = project_.object_ids.next();
    result.name = tr("%1 (décalé)").arg(QString::fromStdString(source->name)).toStdString();
    result.rgb = source->rgb;
    for (const auto& set : source->paths) {
        geometry::PathSet flatSet;
        flatSet.outer = flattenToPath(set.outer);
        for (const auto& hole : set.holes) {
            flatSet.holes.push_back(flattenToPath(hole));
        }
        const auto offsetResult = geometry::inset_path_set(flatSet, delta);
        if (!offsetResult) {
            QMessageBox::warning(this, tr("Décalage impossible"),
                                 QString::fromStdString(offsetResult.error().message));
            return;
        }
        for (auto& offsetSet : *offsetResult) {
            result.paths.push_back(std::move(offsetSet));
        }
    }
    if (result.paths.empty()) {
        statusBar()->showMessage(tr("Décalage trop important — la forme disparaît complètement."));
        return;
    }

    const ObjectId newId = result.id;
    std::vector<document::VectorObject> batch;
    batch.push_back(std::move(result));
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::move(batch), std::vector<document::EmbroideryObject>{}, "Décalage"),
                       project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {newId}});
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Forme décalée créée."));
}

} // namespace openstitch::desktop
