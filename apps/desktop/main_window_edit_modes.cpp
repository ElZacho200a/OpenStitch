// SPDX-License-Identifier: Apache-2.0
// Modes d'édition des points et des guides/rails satin.
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

// Entrée/sortie du mode d'édition des points (Lot 8.2). L'action n'est
// activable (cf. updateActions -> stitchEditModeAct_->setEnabled) que pour un
// objet de broderie sélectionné et non Dirty ; ce garde-fou reste néanmoins
// revérifié ici (défense en profondeur : un déclenchement programmatique ou
// une course sélection/activation ne doit jamais laisser le mode actif sans
// cible valide).
void MainWindow::onStitchEditModeToggled(bool on) {
    stitchEditTarget_.reset();
    stitchEditView_.reset();
    stitchSelection_.clear();
    if (on) {
        // Poser la nouvelle cible AVANT de quitter un autre mode : ses slots relancent
        // updateActions(), qui annulerait l'activation si la cible était encore absente.
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (auto view = stitch_generation::edit_view(project_, emb->id);
                view && view->state != stitch_generation::ObjectEditState::Dirty) {
                stitchEditTarget_ = emb->id;
                stitchEditView_ = std::move(*view);
            }
        }
        if (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked()) {
            satinGuideModeAct_->setChecked(false);
        }
        if (directionGuideModeAct_ != nullptr && directionGuideModeAct_->isChecked()) {
            directionGuideModeAct_->setChecked(false);
        }
        if (gradientEditModeAct_ != nullptr && gradientEditModeAct_->isChecked()) {
            gradientEditModeAct_->setChecked(false);
        }
        // Modes exclusifs : le mode rails (et le bouton unifié qui l'agrège) cède la place.
        // Décochés SANS passer par leur slot : onSatinRailEditModeToggled(false) relancerait
        // updateActions() avant que stitchEditTarget_ soit posé (symétrique du défaut inverse).
        if (railEditModeAct_ != nullptr && railEditModeAct_->isChecked()) {
            QSignalBlocker block(railEditModeAct_);
            railEditModeAct_->setChecked(false);
            railEditTarget_.reset();
        }
        if (satinEditModeAct_ != nullptr && satinEditModeAct_->isChecked()) {
            QSignalBlocker block(satinEditModeAct_);
            satinEditModeAct_->setChecked(false);
        }
        if (!stitchEditTarget_) {
            // Contexte devenu invalide entre le clic et ce traitement (perte de
            // sélection, objet Dirty) : on annule silencieusement l'activation.
            QSignalBlocker block(stitchEditModeAct_);
            stitchEditModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(tr(
                "Édition des points : clic pour sélectionner (Ctrl/Maj : ajouter, Ctrl+A : tout), "
                "glisser pour déplacer, Suppr pour supprimer, double-clic sur un trait pour "
                "insérer. Échap pour quitter."));
        }
    }
    displayImage(processed_); // fait apparaître/disparaître les poignées
    updateActions();
}

void MainWindow::onSatinGuideModeToggled(bool on) {
    satinGuideTarget_.reset();
    selectedSatinGuide_.reset();
    if (on) {
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (const auto* satin = std::get_if<document::SatinParams>(&emb->params);
                satin != nullptr && satin->rungs.size() >= 2) {
                satinGuideTarget_ = emb->id;
            }
        }
        if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (!satinGuideTarget_) {
            QSignalBlocker block(satinGuideModeAct_);
            satinGuideModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Guides satin : glissez une extrémité le long de son rail (Maj+glisser un "
                   "guide lié déplace tout le groupe). Échap pour quitter."));
        }
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::onSatinRailEditModeToggled(bool on) {
    railEditTarget_.reset();
    if (on) {
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (emb->is_satin()) {
                railEditTarget_ = emb->id;
            }
        }
        // Ne coupe PLUS le mode guides (satinGuideModeAct_) : les deux
        // coexistent délibérément depuis l'introduction du mode unifié
        // (satinEditModeAct_, § remodelage satin) — rails et guides
        // s'affichent ensemble, plus besoin de choisir entre les deux.
        // La cible rails est posée AVANT de couper le mode points : sa sortie relance
        // updateActions(), qui décocherait sinon aussitôt le mode rails (cible absente).
        if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (!railEditTarget_) {
            QSignalBlocker block(railEditModeAct_);
            railEditModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Rails satin : glissez un nœud pour le déplacer, double-clic sur un rail "
                   "pour ajouter un nœud. Échap pour quitter."));
        }
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::addSatinGuide() {
    if (!satinGuideTarget_) {
        return;
    }
    auto* obj = project_.findEmbroidery(*satinGuideTarget_);
    auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
    if (satin == nullptr) {
        return;
    }
    if (selectedSatinGuide_) {
        if (const auto junction =
                stitch_generation::satin_guide_junction(*satin, *selectedSatinGuide_)) {
            const auto refs =
                stitch_generation::satin_junction_guides(project_, obj->source_vector, *junction);
            const auto linkId =
                stitch_generation::next_satin_guide_link_id(project_, obj->source_vector);
            if (!linkId) {
                statusBar()->showMessage(
                    tr("Impossible d'ajouter les guides : identifiants de liaison épuisés."));
                return;
            }
            std::vector<commands::SatinGuideAddition> additions;
            additions.reserve(refs.size());
            std::optional<std::size_t> selected;
            for (const auto& ref : refs) {
                if (std::any_of(additions.begin(), additions.end(), [&](const auto& addition) {
                        return addition.id == ref.embroidery_id;
                    })) {
                    statusBar()->showMessage(
                        tr("Impossible d'ajouter les guides : une section boucle sur la même "
                           "jonction."));
                    return;
                }
                const auto* section = project_.findEmbroidery(ref.embroidery_id);
                const auto* sectionSatin =
                    section != nullptr ? std::get_if<document::SatinParams>(&section->params)
                                       : nullptr;
                const auto insertion = sectionSatin != nullptr
                                           ? stitch_generation::make_satin_guide_next_to_junction(
                                                 *sectionSatin, ref.guide_index)
                                           : std::nullopt;
                if (!insertion) {
                    statusBar()->showMessage(
                        tr("Impossible d'ajouter les guides de jonction : une section n'a "
                           "aucun intervalle admissible."));
                    return;
                }
                auto linkedGuide = insertion->guide;
                linkedGuide.link_id = *linkId;
                additions.push_back({ref.embroidery_id, std::move(linkedGuide), insertion->index});
                if (ref.embroidery_id == obj->id) {
                    selected = insertion->index;
                }
            }
            if (additions.empty() || !selected) {
                statusBar()->showMessage(
                    tr("Impossible d'ajouter les guides : réseau de jonction incomplet."));
                return;
            }
            const auto branchCount = additions.size();
            undoStack_.execute(
                std::make_unique<commands::AddSatinGuidesCommand>(std::move(additions)), project_);
            selectedSatinGuide_ = selected;
            refreshImage();
            updateActions();
            statusBar()->showMessage(tr("%1 guides internes ajoutés autour de la jonction #%2.")
                                         .arg(branchCount)
                                         .arg(*junction));
            return;
        }
    }
    const auto insertion = stitch_generation::make_satin_guide_in_largest_gap(*satin);
    if (!insertion) {
        statusBar()->showMessage(
            tr("Impossible d'ajouter un guide : aucun intervalle assez large."));
        return;
    }
    const std::size_t selected = insertion->index;
    undoStack_.execute(std::make_unique<commands::AddSatinGuideCommand>(obj->id, insertion->guide,
                                                                        insertion->index),
                       project_);
    selectedSatinGuide_ = selected;
    refreshImage();
    updateActions();
}

void MainWindow::removeSelectedSatinGuide() {
    if (!satinGuideTarget_ || !selectedSatinGuide_) {
        return;
    }
    auto* obj = project_.findEmbroidery(*satinGuideTarget_);
    auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
    if (satin == nullptr || satin->rungs.size() <= 2 ||
        *selectedSatinGuide_ >= satin->rungs.size()) {
        return;
    }
    if (const auto junction =
            stitch_generation::satin_guide_junction(*satin, *selectedSatinGuide_)) {
        statusBar()->showMessage(tr("Guide de jonction #%1 verrouillé : ajoutez un guide interne "
                                    "pour orienter la section.")
                                     .arg(*junction));
        return;
    }
    // Un guide lié (link_id) représente une identité logique unique répartie
    // sur plusieurs sections : le supprimer d'une seule section laisserait les
    // autres avec un lien orphelin. On supprime donc le groupe entier comme
    // une transaction atomique unique, ou on refuse en totalité.
    if (const auto linkId = satin->rungs[*selectedSatinGuide_].link_id) {
        const auto refs =
            stitch_generation::satin_linked_guides(project_, obj->source_vector, *linkId);
        if (refs.size() < 2) {
            statusBar()->showMessage(
                tr("Impossible de supprimer le groupe : identifiants de liaison incohérents."));
            return;
        }
        std::vector<commands::SatinGuideRemoval> removals;
        removals.reserve(refs.size());
        for (const auto& ref : refs) {
            const auto* section = project_.findEmbroidery(ref.embroidery_id);
            const auto* sectionSatin =
                section != nullptr ? std::get_if<document::SatinParams>(&section->params) : nullptr;
            if (sectionSatin == nullptr || sectionSatin->rungs.size() <= 2) {
                statusBar()->showMessage(
                    tr("Impossible de supprimer le groupe : une section perdrait tous ses "
                       "guides internes."));
                return;
            }
            removals.push_back({ref.embroidery_id, ref.guide_index});
        }
        const auto groupCount = removals.size();
        undoStack_.execute(
            std::make_unique<commands::RemoveSatinGuidesCommand>(std::move(removals)), project_);
        selectedSatinGuide_.reset();
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Groupe de %1 guides liés supprimé.").arg(groupCount));
        return;
    }
    undoStack_.execute(
        std::make_unique<commands::RemoveSatinGuideCommand>(obj->id, *selectedSatinGuide_),
        project_);
    selectedSatinGuide_.reset();
    refreshImage();
    updateActions();
}

void MainWindow::discardOverrides(ObjectId id) {
    const auto* obj = project_.findEmbroidery(id);
    if (obj == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Abandonner les retouches"),
        tr("Abandonner définitivement les retouches manuelles de « %1 » ? Les points "
           "reviendront à la géométrie générée. Cette action reste annulable (Ctrl+Z).")
            .arg(QString::fromStdString(obj->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::DiscardOverridesCommand>(id), project_);
    refreshImage();
    updateActions();
}

} // namespace openstitch::desktop
