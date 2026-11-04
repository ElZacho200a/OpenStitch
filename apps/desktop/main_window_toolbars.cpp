// SPDX-License-Identifier: Apache-2.0
// Barres d'outils, palette d'outils et changement d'outil.
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

void MainWindow::buildMainToolbar() {
    mainToolbar_ = addToolBar(tr("Barre principale"));
    mainToolbar_->setObjectName(QStringLiteral("mainToolbar"));
    mainToolbar_->setMovable(false);
    mainToolbar_->setIconSize(QSize(18, 18));

    // Les mêmes QAction que dans les menus : grisées, raccourcies et décrites de la même façon.
    const auto addShared = [this](QAction* act, const QIcon& icon) {
        act->setIcon(icon);
        QString tip = act->text();
        tip.remove(QLatin1Char('&'));
        tip.remove(QStringLiteral("…"));
        if (!act->shortcut().isEmpty()) {
            tip += QStringLiteral(" (%1)").arg(act->shortcut().toString(QKeySequence::NativeText));
        }
        act->setToolTip(tip);
        mainToolbar_->addAction(act);
    };
    addShared(newProjectAct_, icons::newProject());
    addShared(openImageAct_, icons::openImage());
    addShared(loadProjectAct_, icons::openProject());
    addShared(saveProjectAct_, icons::save());
    mainToolbar_->addSeparator();
    undoAct_->setIcon(icons::undo());
    redoAct_->setIcon(icons::redo());
    mainToolbar_->addAction(undoAct_);
    mainToolbar_->addAction(redoAct_);
    mainToolbar_->addSeparator();
    addShared(zoomOutAct_, icons::zoomOut());
    addShared(fitCanvasAct_, icons::fit());
    addShared(zoomInAct_, icons::zoomIn());
    mainToolbar_->addSeparator();
    addShared(analyzeAct_, icons::analyze());
    showStitchesAct_->setIcon(icons::stitches());
    mainToolbar_->addAction(showStitchesAct_);
    addShared(exportDstAct_, icons::exportDst());
    // Action principale de la fenêtre : bouton plein, en texte (l'icône grise n'aurait pas le
    // contraste voulu sur le fond d'accent).
    if (auto* exportButton =
            qobject_cast<QToolButton*>(mainToolbar_->widgetForAction(exportDstAct_))) {
        exportButton->setProperty("primary", true);
        exportButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
}

void MainWindow::buildContextToolbar() {
    contextToolbar_ = addToolBar(tr("Barre contextuelle"));
    contextToolbar_->setObjectName(QStringLiteral("contextToolbar"));
    contextToolbar_->setMovable(false);
    updateContextToolbar();
}

document::EmbroideryObject* MainWindow::resolveSelectedEmbroidery() {
    document::EmbroideryObject* emb = nullptr;
    if (selectedEmbroidery_) {
        emb = project_.findEmbroidery(*selectedEmbroidery_);
    }
    if (emb == nullptr && selectedObject_ && !hasMultiSelection()) {
        emb = embroideryForVector(*selectedObject_);
    }
    return emb;
}

stitch_generation::ObjectEditState MainWindow::editStateOf(ObjectId id) const {
    for (const auto& [candidate, state] : editStates_) {
        if (candidate == id) {
            return state;
        }
    }
    return stitch_generation::ObjectEditState::Clean; // absent du cache = Clean (jamais retouché)
}

void MainWindow::updateContextToolbar() {
    if (contextToolbar_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    const bool hasMulti = hasMultiSelection();
    const bool hasVec = selectedObject_.has_value() && !hasMulti;
    const bool hasReg = selectedRegion_ && project_.segmentation;
    const int pts = sequence_ ? static_cast<int>(sequence_->commands.size()) : 0;

    // Signature de l'état : évite de reconstruire (et de faire clignoter) la barre
    // quand rien de pertinent n'a changé.
    QString sig;
    if (emb != nullptr) {
        // Etat retouche (Lot 8.2) et mode d'edition inclus dans la signature :
        // sans eux, la barre ne se reconstruirait pas quand seul l'un des deux
        // change (meme objet/type toujours selectionne).
        sig =
            QStringLiteral("E%1t%2s%3m%4g%5i%6r%7")
                .arg(emb->id.value)
                .arg(stitchTypeIndex(*emb))
                .arg(static_cast<int>(editStateOf(emb->id)))
                .arg(stitchEditModeAct_->isChecked() ? 1 : 0)
                .arg(satinGuideModeAct_->isChecked() ? 1 : 0)
                .arg(selectedSatinGuide_ ? static_cast<qlonglong>(*selectedSatinGuide_) : -1)
                .arg(railEditModeAct_->isChecked() ? 1 : 0) +
            QStringLiteral("d%1g%2")
                .arg(directionGuideModeAct_->isChecked() ? 1 : 0)
                .arg(gradientEditModeAct_ != nullptr && gradientEditModeAct_->isChecked() ? 1 : 0);
    } else if (hasMulti) {
        sig = QStringLiteral("M%1").arg(multiSelection_.size());
    } else if (hasVec) {
        sig = QStringLiteral("V%1").arg(selectedObject_->value);
    } else if (hasReg) {
        sig = QStringLiteral("R%1").arg(selectedRegion_->value);
    } else {
        sig = QStringLiteral("N%1_%2x%3")
                  .arg(pts)
                  .arg(project_.canvas.width.value)
                  .arg(project_.canvas.height.value);
    }
    if (sig == contextSig_) {
        return;
    }
    contextSig_ = sig;
    // QToolBar::clear() ne détruit ni les actions ni les widgets créés par addAction(texte)
    // / addWidget() : ils s'accumulaient comme enfants de la barre à chaque reconstruction.
    // On détruit ceux que la barre possède ; les actions persistantes (membres *_Act_,
    // parentées à la fenêtre) sont seulement retirées. Détachés tout de suite (le compte
    // d'enfants reste stable) mais détruits en différé : cette méthode peut être appelée
    // depuis le slot d'un des boutons qu'on retire.
    const QList<QAction*> previous = contextToolbar_->actions();
    contextToolbar_->clear();
    for (QAction* old : previous) {
        if (old->parent() != contextToolbar_) {
            continue;
        }
        if (auto* widgetAct = qobject_cast<QWidgetAction*>(old)) {
            if (QWidget* w = widgetAct->defaultWidget()) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
        }
        old->setParent(nullptr);
        old->deleteLater();
    }
    // Le layout de la barre crée un QToolButton par action simple et ne le détruit qu'en
    // différé (deleteLater) : tant que la boucle d'évènements n'a pas tourné, ils restent
    // enfants de la barre. On les détache tout de suite (leur destruction différée suit son
    // cours) ; le bouton d'extension (« >> ») est permanent et reste en place. Dépend d'un
    // détail interne de Qt : l'objectName "qt_toolbar_ext_button" du QToolBarExtension
    // (stable de Qt 5 à 6.8) ; s'il changeait, le bouton serait détaché puis détruit, et la
    // barre n'afficherait plus le menu de débordement (sans plantage).
    for (auto* stale : contextToolbar_->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
        if (stale->objectName() != QLatin1String("qt_toolbar_ext_button")) {
            stale->hide();
            stale->setParent(nullptr);
            stale->deleteLater();
        }
    }

    if (emb != nullptr) {
        const ObjectId id = emb->id;
        contextToolbar_->addWidget(new ElidedTextLabel(tr("Type de points :"), contextToolbar_));
        const int current = stitchTypeIndex(*emb);
        const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin"), tr("Directionnel")};
        for (int t = 0; t < 4; ++t) {
            auto* btn = new QToolButton(contextToolbar_);
            btn->setText(names[t]);
            btn->setAccessibleName(tr("Type de points : %1").arg(names[t]));
            btn->setCheckable(true);
            btn->setAutoExclusive(true); // un seul type actif : annoncé comme un groupe
            btn->setChecked(t == current);
            connect(btn, &QToolButton::clicked, this, [this, id, t] { setStitchType(id, t); });
            contextToolbar_->addWidget(btn);
        }
        // Types sans bouton direct : un menu « Plus » (concentrique, radial, spirale, croisé,
        // motifs).
        {
            auto* more = new QToolButton(contextToolbar_);
            more->setText(tr("Plus ▾"));
            more->setObjectName(QStringLiteral("button_contextStitchTypeMore"));
            more->setAccessibleName(tr("Autres types de points"));
            more->setPopupMode(QToolButton::InstantPopup);
            auto* moreMenu = new QMenu(more);
            for (int t = kStitchTypeDirectButtons; t < kStitchTypeCount; ++t) {
                auto* act = moreMenu->addAction(stitchTypeLabel(t));
                act->setCheckable(true);
                act->setChecked(t == current);
                connect(act, &QAction::triggered, this, [this, id, t] { setStitchType(id, t); });
            }
            more->setMenu(moreMenu);
            contextToolbar_->addWidget(more);
        }
        if (emb->is_tatami()) {
            contextToolbar_->addSeparator();
            auto* rot = contextToolbar_->addAction(tr("Orientation…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
            contextToolbar_->addAction(autoDirectionGuideAct_);
            contextToolbar_->addAction(twoColorBlendAct_);
            contextToolbar_->addAction(gradientEditModeAct_);
        }
        if (emb->is_directional()) {
            contextToolbar_->addSeparator();
            contextToolbar_->addAction(autoDirectionGuideAct_);
            contextToolbar_->addAction(twoColorBlendAct_);
            contextToolbar_->addAction(gradientEditModeAct_);
            contextToolbar_->addAction(directionGuideModeAct_);
            if (directionGuideModeAct_->isChecked()) {
                contextToolbar_->addAction(drawDirectionGuideAct_);
                contextToolbar_->addAction(drawBreakLineAct_);
            }
        }
        contextToolbar_->addSeparator();
        contextToolbar_->addAction(stitchEditModeAct_);
        if (emb->is_satin()) {
            // Un seul bouton visible par défaut (rails + guides ensemble) ;
            // le sous-menu Broderie ▸ Réglages fins du satin reste
            // disponible pour n'afficher qu'un seul des deux aspects.
            contextToolbar_->addAction(satinEditModeAct_);
            if (satinGuideModeAct_->isChecked()) {
                contextToolbar_->addAction(addSatinGuideAct_);
                contextToolbar_->addAction(removeSatinGuideAct_);
            }
        }
        const auto editState = editStateOf(id);
        if (editState == stitch_generation::ObjectEditState::ManuallyEdited) {
            contextToolbar_->addWidget(new ElidedTextLabel(tr("✎ retouché"), contextToolbar_));
        } else if (editState == stitch_generation::ObjectEditState::Dirty) {
            auto* warn = new ElidedTextLabel(tr("⚠ retouches obsolètes"), contextToolbar_);
            warn->setToolTip(tr("La géométrie ou l'ordre de couture a changé depuis les dernières "
                                "retouches : elles ne sont plus appliquées."));
            contextToolbar_->addWidget(warn);
        }
        if (editState != stitch_generation::ObjectEditState::Clean) {
            auto* discard = contextToolbar_->addAction(tr("Abandonner les retouches"));
            connect(discard, &QAction::triggered, this, [this, id] { discardOverrides(id); });
        }
    } else if (hasMulti || hasVec) {
        // HP-UX-028 : les mêmes quatre boutons « Type de points » pour une ou N formes ; un clic
        // crée (ou convertit) la couture au dernier réglage, en un pas d'annulation, sans dialogue.
        if (hasMulti) {
            contextToolbar_->addWidget(new ElidedTextLabel(
                tr("%1 objets sélectionnés · type de points :").arg(multiSelection_.size()),
                contextToolbar_));
        } else {
            contextToolbar_->addWidget(
                new ElidedTextLabel(tr("Objet vectoriel · type de points :"), contextToolbar_));
        }
        const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin"), tr("Directionnel")};
        for (int t = 0; t < 4; ++t) {
            auto* btn = new QToolButton(contextToolbar_);
            btn->setText(names[t]);
            btn->setObjectName(QStringLiteral("button_contextStitchType%1").arg(t));
            btn->setAccessibleName(tr("Type de points : %1").arg(names[t]));
            connect(btn, &QToolButton::clicked, this, [this, t] { setStitchTypeForSelection(t); });
            contextToolbar_->addWidget(btn);
        }
        // Types sans bouton direct : un menu « Plus » (concentrique, radial, spirale, croisé,
        // motifs).
        {
            auto* more = new QToolButton(contextToolbar_);
            more->setText(tr("Plus ▾"));
            more->setObjectName(QStringLiteral("button_contextStitchTypeMore"));
            more->setAccessibleName(tr("Autres types de points"));
            more->setPopupMode(QToolButton::InstantPopup);
            auto* moreMenu = new QMenu(more);
            for (int t = kStitchTypeDirectButtons; t < kStitchTypeCount; ++t) {
                auto* act = moreMenu->addAction(stitchTypeLabel(t));
                act->setCheckable(true);
                act->setChecked(false);
                connect(act, &QAction::triggered, this,
                        [this, t] { setStitchTypeForSelection(t); });
            }
            more->setMenu(moreMenu);
            contextToolbar_->addWidget(more);
        }
    } else if (hasReg) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 = region->pixel_count * mmPerPx * mmPerPx;
            const std::vector<RegionId> picked = selectedRegionIds();
            QString label = tr("Région %1 · %2 mm²").arg(region->id.value).arg(areaMm2, 0, 'f', 1);
            if (picked.size() > 1) {
                std::size_t pixels = 0;
                for (const RegionId r : picked) {
                    if (const auto* rr = project_.segmentation->find(r)) {
                        pixels += rr->pixel_count;
                    }
                }
                label = tr("%1 régions · %2 mm²")
                            .arg(picked.size())
                            .arg(static_cast<double>(pixels) * mmPerPx * mmPerPx, 0, 'f', 1);
            }
            contextToolbar_->addWidget(new ElidedTextLabel(label, contextToolbar_));
        }
        contextToolbar_->addAction(mergeSelectionAct_); // Fusionner la sélection (>= 2 régions)
        contextToolbar_->addAction(mergeAct_);          // Fusionner avec… (mode)
        auto* recolorButton = contextToolbar_->addAction(tr("Recolorer…"));
        connect(recolorButton, &QAction::triggered, this, &MainWindow::recolorSelectedRegion);
        // Les actions du menu elles-mêmes (libellé, raccourci et état identiques partout).
        contextToolbar_->addAction(deleteSelectionAct_);
        contextToolbar_->addAction(vectorizeRegionAct_);
    } else {
        if (sequence_) {
            const auto st = stitch::compute_stats(*sequence_);
            const double w = to_millimeters(st.bounds.max.x - st.bounds.min.x).value;
            const double h = to_millimeters(st.bounds.max.y - st.bounds.min.y).value;
            contextToolbar_->addWidget(new ElidedTextLabel(
                tr("Motif : %1 × %2 mm · %3 points · %4 changement(s) de couleur")
                    .arg(w, 0, 'f', 1)
                    .arg(h, 0, 'f', 1)
                    .arg(st.stitches)
                    .arg(st.color_changes),
                contextToolbar_));
        } else {
            contextToolbar_->addWidget(new ElidedTextLabel(
                tr("Ouvrez une image, ou sélectionnez une région ou un objet."), contextToolbar_));
        }
        auto* hoop = contextToolbar_->addAction(
            tr("Cadre : %1 × %2 mm…")
                .arg(to_millimeters(project_.canvas.width).value, 0, 'f', 0)
                .arg(to_millimeters(project_.canvas.height).value, 0, 'f', 0));
        connect(hoop, &QAction::triggered, this, &MainWindow::setHoopSize);
    }
}

void MainWindow::buildToolPalette() {
    toolPalette_ = new QToolBar(tr("Outils"), this);
    toolPalette_->setObjectName(QStringLiteral("toolPalette"));
    toolPalette_->setMovable(false);
    toolPalette_->setIconSize(QSize(20, 20));
    addToolBar(Qt::LeftToolBarArea, toolPalette_);

    auto* group = new QActionGroup(this);
    const auto addTool = [&](const QIcon& icon, const QString& text, Tool tool,
                             const QKeySequence& key) {
        auto* act = toolPalette_->addAction(icon, text);
        act->setCheckable(true);
        // « Nom (aide) » devient « Nom (touche) » puis l'aide sur la ligne suivante.
        const int paren = text.indexOf(QStringLiteral(" ("));
        if (paren > 0 && text.endsWith(QLatin1Char(')'))) {
            const QString shortName = text.left(paren);
            const QString help = text.mid(paren + 2, text.size() - paren - 3);
            act->setToolTip(tr("%1 (%2)\n%3").arg(shortName, key.toString(), help));
        } else {
            act->setToolTip(tr("%1 (%2)").arg(text, key.toString()));
        }
        act->setShortcut(key);
        group->addAction(act);
        connect(act, &QAction::triggered, this, [this, tool] { setTool(tool); });
        return act;
    };
    toolSelectAct_ =
        addTool(icons::select(), tr("Sélection"), Tool::Select, QKeySequence(Qt::Key_V));
    toolPanAct_ = addTool(icons::pan(), tr("Déplacer la vue"), Tool::Pan, QKeySequence(Qt::Key_H));
    toolRectAct_ = addTool(icons::rect(), tr("Recadrer l'image (glisser un cadre)"), Tool::Rect,
                           QKeySequence(Qt::Key_M));
    toolDrawRectAct_ = addTool(icons::drawRect(), tr("Dessiner un rectangle"), Tool::DrawRectangle,
                               QKeySequence(Qt::Key_R));
    toolDrawEllipseAct_ = addTool(icons::ellipse(), tr("Dessiner une ellipse (Maj = cercle)"),
                                  Tool::DrawEllipse, QKeySequence(Qt::Key_O));
    toolDrawPolygonAct_ = addTool(icons::polygon(), tr("Dessiner un polygone (segments droits)"),
                                  Tool::DrawPolygon, QKeySequence(Qt::Key_P));
    toolDrawPolygonRegularAct_ =
        addTool(icons::regularPolygon(), tr("Dessiner un polygone régulier (glisser un cadre)"),
                Tool::DrawPolygonRegular, QKeySequence(Qt::Key_G));
    polygonSidesSpin_ = new QSpinBox(toolPalette_);
    polygonSidesSpin_->setRange(3, 12);
    polygonSidesSpin_->setValue(6);
    polygonSidesSpin_->setToolTip(
        tr("Nombre de côtés du polygone régulier (molette ou flèches ↑ ↓)"));
    polygonSidesSpin_->setMaximumWidth(48);
    polygonSidesSpin_->setAlignment(Qt::AlignCenter);
    polygonSidesSpin_->setButtonSymbols(
        QAbstractSpinBox::NoButtons); // trop étroit pour les flèches
    toolPalette_->addWidget(polygonSidesSpin_);
    toolDrawBezierAct_ =
        addTool(icons::bezierCurve(),
                tr("Dessiner une courbe de Bézier (clic = coin, clic-glisser = nœud lisse)"),
                Tool::DrawBezier, QKeySequence(Qt::Key_B));
    openPathAct_ = toolPalette_->addAction(icons::openPath(), tr("Tracé ouvert"));
    openPathAct_->setObjectName(QStringLiteral("action_openPath"));
    openPathAct_->setCheckable(true);
    openPathAct_->setShortcut(QKeySequence(Qt::Key_U));
    openPathAct_->setToolTip(
        tr("Tracé ouvert (U)\nCoché : le polygone (P) et la courbe de Bézier (B) se terminent sans "
           "être fermés — une ligne ou une courbe ouverte, pour un point de contour ou un satin de "
           "bordure. 2 points suffisent."));
    connect(openPathAct_, &QAction::toggled, this, [this](bool open) {
        statusBar()->showMessage(open ? tr("Tracé ouvert : les prochains polygones et courbes "
                                           "ne seront pas fermés.")
                                      : tr("Tracé fermé : les prochains polygones et courbes "
                                           "seront fermés."),
                                 6000);
    });
    // Outil « Ligne » (HP-VEC-002) : raccourci vers le polygone en mode tracé ouvert (clics,
    // Entrée ou double-clic pour terminer sans fermer).
    auto* lineToolAct = toolPalette_->addAction(icons::openPath(), tr("Ligne"));
    lineToolAct->setObjectName(QStringLiteral("action_lineTool"));
    lineToolAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    lineToolAct->setToolTip(tr("Ligne (Maj+L)\nClics successifs, Entrée pour terminer SANS fermer "
                               "(ligne ou polyligne ouverte). Active le tracé ouvert."));
    connect(lineToolAct, &QAction::triggered, this, [this] {
        openPathAct_->setChecked(true);
        setTool(Tool::DrawPolygon);
    });
    toolDrawFreeformAct_ = addTool(icons::freeform(), tr("Dessiner à main levée (lasso)"),
                                   Tool::DrawFreeform, QKeySequence(Qt::Key_L));
    toolDrawSatinColumnAct_ =
        addTool(icons::satinColumn(), tr("Colonne satin (clics alternés côté A / côté B)"),
                Tool::DrawSatinColumn, QKeySequence(Qt::Key_S));
    toolCutAct_ = addTool(
        icons::knife(), tr("Couteau (tracez une ligne pour découper les formes qu'elle traverse)"),
        Tool::Cut, QKeySequence(Qt::Key_K));
    if (shapeMenu_ != nullptr) {
        shapeMenu_->addAction(toolCutAct_);
    }
    attachMeasureEyedropperTools();
    attachBrushTool();
    attachBucketTool();
    attachClickToStitchTool();
    attachDetailZonesTool();
    toolSelectAct_->setChecked(true);

    toolPalette_->addSeparator();
    // Boutons génériques Terminer/Annuler : le double-clic reste disponible
    // pour polygone/bézier (Entrée aussi, cf. plus bas), mais un moyen visible
    // et cliquable de valider une forme manquait — défaut remonté en usage
    // réel (le double-clic seul n'est pas découvrable).
    finishDrawAct_ = toolPalette_->addAction(icons::checkmark(), tr("Terminer la forme (Entrée)"));
    finishDrawAct_->setToolTip(tr("Terminer la forme en cours (Entrée)"));
    finishDrawAct_->setEnabled(false);
    connect(finishDrawAct_, &QAction::triggered, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            finishPolygon();
        } else if (currentTool_ == Tool::DrawBezier) {
            finishBezier();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            finishSatinColumn();
        } else if (drawingDirectionGuide()) {
            finishDirectionGuide();
        }
    });
    cancelDrawAct_ = toolPalette_->addAction(icons::cancelDraw(), tr("Annuler le tracé (Échap)"));
    cancelDrawAct_->setToolTip(tr("Annuler le tracé en cours (Échap)"));
    cancelDrawAct_->setEnabled(false);
    connect(cancelDrawAct_, &QAction::triggered, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            cancelPolygonDraw();
        } else if (currentTool_ == Tool::DrawBezier) {
            cancelBezierDraw();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            cancelSatinColumnDraw();
        } else if (drawingDirectionGuide()) {
            cancelDirectionGuideDraw();
        }
    });

    // Échap : revient à la Sélection, annule le mode fusion en cours et
    // quitte le mode d'édition des points (Lot 8.2) s'il est actif -- pas
    // d'annulation du glisser en cours (l'architecture ne le permet pas
    // proprement : les poignées sont détruites/reconstruites à chaque
    // rafraîchissement, cf. renderBase), seulement une sortie propre du mode.
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut_ = escape;
    connect(escape, &QShortcut::activated, this, [this] {
        // Rien à annuler (aucun mode, outil de sélection) : Échap désélectionne les régions.
        const bool nothingToCancel =
            !mergeAct_->isChecked() && !stitchEditModeAct_->isChecked() &&
            !satinEditModeAct_->isChecked() && !satinGuideModeAct_->isChecked() &&
            !railEditModeAct_->isChecked() && !directionGuideModeAct_->isChecked() &&
            currentTool_ == Tool::Select;
        if (nothingToCancel && !selectedRegionIds().empty()) {
            selectRegions({}, SelectMode::Replace);
            return;
        }
        // Rien d'autre à relâcher : Échap désélectionne aussi les objets (HP-UX-018), sans
        // avoir à cliquer dans un « vide » souvent rempli de formes.
        if (nothingToCancel) {
            const Selection current = currentSelection();
            if (current.embroidery.has_value() || !current.objects.empty()) {
                setSelection({});
                displayImage(processed_);
                updateActions();
                return;
            }
        }
        if (mergeAct_->isChecked()) {
            mergeAct_->setChecked(false);
        }
        if (stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (satinEditModeAct_->isChecked()) {
            satinEditModeAct_->setChecked(false);
        }
        if (satinGuideModeAct_->isChecked()) {
            satinGuideModeAct_->setChecked(false);
        }
        if (railEditModeAct_->isChecked()) {
            railEditModeAct_->setChecked(false);
        }
        if (directionGuideModeAct_->isChecked()) {
            directionGuideModeAct_->setChecked(false);
        }
        setTool(Tool::Select);
    });

    // Terminaison/retrait de point, communs à tout outil de tracé multi-clics
    // (polygone/bézier/satin) : Entrée/Retour termine (comme un double-clic
    // ou le bouton Terminer ci-dessus), Retour arrière retire le dernier
    // point posé. Sans effet hors de ces outils.
    const auto finishDrawShortcut = [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            finishPolygon();
        } else if (currentTool_ == Tool::DrawBezier) {
            finishBezier();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            finishSatinColumn();
        } else if (drawingDirectionGuide()) {
            finishDirectionGuide();
        }
    };
    auto* drawFinishReturn = new QShortcut(QKeySequence(Qt::Key_Return), this);
    drawReturnShortcut_ = drawFinishReturn;
    connect(drawFinishReturn, &QShortcut::activated, this, finishDrawShortcut);
    auto* drawFinishEnter = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    drawEnterShortcut_ = drawFinishEnter;
    connect(drawFinishEnter, &QShortcut::activated, this, finishDrawShortcut);
    auto* drawBackspace = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    drawBackspaceShortcut_ = drawBackspace;
    connect(drawBackspace, &QShortcut::activated, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            removeLastPolygonVertex();
        } else if (currentTool_ == Tool::DrawBezier) {
            removeLastBezierPoint();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            removeLastSatinColumnPoint();
        } else if (drawingDirectionGuide()) {
            removeLastDirectionGuidePoint();
        }
    });
    // Ces trois touches ne servent qu'au tracé / aux modes : désactivées le reste du temps,
    // sans quoi elles étaient volées à toute la fenêtre (Entrée dans un champ de
    // l'inspecteur ou une liste, Retour arrière...).
    updateShortcutsState();
}

void MainWindow::setTool(Tool tool) {
    // Quitter l'outil Polygone en cours de tracé annule proprement le tracé
    // (aperçu détruit, sommets oubliés) — jamais de polygone à moitié posé
    // qui survivrait à un changement d'outil.
    if (currentTool_ == Tool::DrawPolygon && tool != Tool::DrawPolygon) {
        cancelPolygonDraw();
    }
    const bool leavingStroke =
        (currentTool_ == Tool::DrawFreeform || currentTool_ == Tool::Cut) && tool != currentTool_;
    if (leavingStroke) {
        cancelFreeformDraw();
    }
    if (tool != Tool::DrawFreeform) {
        lassoSelectArmed_ = false; // le lasso de sélection ne survit pas à un changement d'outil
    }
    if (currentTool_ == Tool::DrawSatinColumn && tool != Tool::DrawSatinColumn) {
        cancelSatinColumnDraw();
    }
    if (currentTool_ == Tool::DrawBezier && tool != Tool::DrawBezier) {
        cancelBezierDraw();
    }
    if (drawingDirectionGuide() && tool != currentTool_) {
        cancelDirectionGuideDraw();
    }
    // Un changement d'outil peut laisser le repère d'accroche affiché à une
    // position qui ne correspond plus à rien (ex. sorti du mode Polygone
    // sans bouger la souris) : masqué jusqu'au prochain mouvement pertinent.
    updateSnapIndicator(std::nullopt);
    currentTool_ = tool;

    // Synchronise les cases d'outils (sans réémettre).
    const auto sync = [](QAction* act, bool on) {
        if (act == nullptr)
            return;
        QSignalBlocker block(act);
        act->setChecked(on);
    };
    sync(toolSelectAct_, tool == Tool::Select);
    sync(toolPanAct_, tool == Tool::Pan);
    sync(toolRectAct_, tool == Tool::Rect);
    sync(toolDrawRectAct_, tool == Tool::DrawRectangle);
    sync(toolDrawEllipseAct_, tool == Tool::DrawEllipse);
    sync(toolDrawPolygonAct_, tool == Tool::DrawPolygon);
    sync(toolDrawPolygonRegularAct_, tool == Tool::DrawPolygonRegular);
    sync(toolDrawBezierAct_, tool == Tool::DrawBezier);
    sync(toolDrawFreeformAct_, tool == Tool::DrawFreeform);
    sync(toolDrawSatinColumnAct_, tool == Tool::DrawSatinColumn);
    sync(toolCutAct_, tool == Tool::Cut);
    sync(toolTextAct_, tool == Tool::Text);
    if (cropAct_ != nullptr) {
        QSignalBlocker block(cropAct_);
        cropAct_->setChecked(tool == Tool::Rect);
    }

    // Applique au canevas : Rectangle = recadrage ; Rectangle/Ellipse dessinés
    // = cadre élastique générique ; Polygone = clics successifs ; Main levée =
    // glisser continu ; sinon vue libre. dragMode() est recalculé par
    // CanvasView lui-même à partir de TOUS ses booléens à chaque appel (cf.
    // CanvasView::updateDragMode) : ordre d'appel indifférent, plus de
    // clobbering entre ces six appels (défaut trouvé par revue -- voir le
    // commentaire détaillé dans canvas_view.cpp).
    view_->setCropMode(tool == Tool::Rect);
    view_->setBoxDrawMode(tool == Tool::DrawRectangle || tool == Tool::DrawEllipse ||
                          tool == Tool::DrawPolygonRegular);
    view_->setPolygonDrawMode(tool == Tool::DrawPolygon || tool == Tool::DrawDirectionGuide ||
                              tool == Tool::DrawBreakLine);
    view_->setBezierDrawMode(tool == Tool::DrawBezier);
    view_->setFreeformDrawMode(tool == Tool::DrawFreeform || tool == Tool::Cut);
    view_->setSatinPairDrawMode(tool == Tool::DrawSatinColumn);
    if (tool == Tool::Pan) {
        view_->setCursor(Qt::OpenHandCursor);
        view_->viewport()->setCursor(Qt::OpenHandCursor);
    } else if (tool == Tool::Select) {
        view_->setCursor(Qt::ArrowCursor);
        view_->viewport()->setCursor(Qt::ArrowCursor);
    }

    if (toolLabel_ != nullptr) {
        const QString name = tool == Tool::Select          ? tr("Sélection")
                             : tool == Tool::Pan           ? tr("Déplacer la vue")
                             : tool == Tool::Rect          ? tr("Recadrer l'image")
                             : tool == Tool::DrawRectangle ? tr("Dessiner un rectangle")
                             : tool == Tool::DrawEllipse   ? tr("Dessiner une ellipse")
                             : tool == Tool::DrawPolygon   ? tr("Dessiner un polygone")
                             : tool == Tool::DrawPolygonRegular
                                 ? tr("Dessiner un polygone régulier")
                             : tool == Tool::DrawBezier   ? tr("Dessiner une courbe de Bézier")
                             : tool == Tool::DrawFreeform ? tr("Dessiner à main levée")
                             : tool == Tool::DrawDirectionGuide ? tr("Guide de direction")
                             : tool == Tool::DrawBreakLine      ? tr("Ligne de rupture")
                             : tool == Tool::Cut                ? tr("Couteau")
                             : tool == Tool::Text               ? tr("Texte")
                                                                : tr("Colonne satin");
        toolLabel_->setText(tr("Outil : %1").arg(name));
    }
    // Rappel explicite du geste attendu à l'activation de l'outil : défaut
    // trouvé en usage réel (rectangle/ellipse ne réagissaient « à rien » en
    // apparence pour qui cliquait sans glisser — le mécanisme fonctionnait,
    // mais rien n'indiquait qu'un GLISSER était nécessaire).
    if (tool == Tool::Rect) {
        statusBar()->showMessage(
            tr("Glissez le cadre à conserver — l'image sera recadrée au relâchement (Échap : "
               "annuler). Pour dessiner un rectangle, utilisez l'outil R."));
    } else if (tool == Tool::DrawRectangle || tool == Tool::DrawEllipse) {
        statusBar()->showMessage(
            tr("Cliquez-glissez sur le canevas pour dessiner le cadre, puis relâchez."));
    } else if (tool == Tool::DrawPolygonRegular) {
        statusBar()->showMessage(
            tr("Cliquez-glissez pour dessiner le polygone (inscrit dans le cadre) — réglez le "
               "nombre de côtés dans la palette d'outils."));
    } else if (tool == Tool::DrawPolygon) {
        statusBar()->showMessage(
            tr("Cliquez pour poser chaque sommet — Entrée/double-clic/bouton ✓ pour "
               "terminer (3 sommets min.), Échap pour annuler."));
    } else if (tool == Tool::DrawBezier) {
        statusBar()->showMessage(
            tr("Cliquez (coin) ou cliquez-glissez (nœud lisse) pour poser chaque point — "
               "Entrée/double-clic/bouton ✓ pour terminer (2 nœuds min.), Échap pour annuler."));
    } else if (tool == Tool::DrawFreeform) {
        statusBar()->showMessage(tr("Cliquez-glissez pour tracer la forme à main levée."));
    } else if (tool == Tool::Text) {
        statusBar()->showMessage(
            tr("Texte : cliquez sur le canevas pour poser le texte (bord gauche de la ligne de "
               "base), puis réglez police, hauteur et type de point."));
    } else if (tool == Tool::Cut) {
        statusBar()->showMessage(
            tr("Couteau : cliquez-glissez une ligne à travers la ou les formes à découper "
               "(sans sélection, toutes les formes traversées sont coupées)."));
    } else if (tool == Tool::DrawSatinColumn) {
        statusBar()->showMessage(
            tr("Cliquez alternativement côté A puis côté B de chaque paire — Entrée/"
               "double-clic/bouton ✓ pour terminer (2 paires min.), Échap pour annuler."));
    } else if (tool == Tool::DrawDirectionGuide) {
        statusBar()->showMessage(
            tr("Cliquez les points de la courbe que le fil doit suivre — Entrée/double-clic "
               "pour terminer (2 points min.), Échap pour annuler."));
    } else if (tool == Tool::DrawBreakLine) {
        statusBar()->showMessage(
            tr("Cliquez les points de la ligne de rupture, d'un bord de la forme à l'autre — "
               "Entrée/double-clic pour terminer, Échap pour annuler."));
    }
    syncMeasureEyedropperTool(tool);
    syncBrushTool(tool);
    syncBucketTool(tool);
    syncClickToStitchTool(tool);
    syncDetailZonesTool(tool); // après syncBrushTool : réutilise le geste du pinceau
    updateDrawActionsState();
    updateInteractionContext();
    hideHoverHighlight();
}

} // namespace openstitch::desktop
