// SPDX-License-Identifier: Apache-2.0
// Simulation de couture et statistiques.
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

void MainWindow::buildSimulationToolbar() {
    simToolbar_ = new QToolBar(tr("Simulation"), this);
    simToolbar_->setObjectName(QStringLiteral("simToolbar"));
    simToolbar_->setMovable(false);
    addToolBar(Qt::BottomToolBarArea, simToolbar_);

    simPlayAct_ = simToolbar_->addAction(tr("▶ Lecture"));
    simPlayAct_->setCheckable(true);
    simPlayAct_->setToolTip(tr("Lit la couture point par point."));
    connect(simPlayAct_, &QAction::toggled, this, &MainWindow::toggleSimulation);
    // Sortie explicite : tant que la simulation est « en cours » (même en pause ou arrivée à la
    // fin), le rendu réaliste reste désactivé (HP-UX-022).
    simExitAct_ = simToolbar_->addAction(tr("⏹ Fin"));
    simExitAct_->setObjectName(QStringLiteral("action_simExit"));
    simExitAct_->setToolTip(
        tr("Quitte la simulation et affiche la couture complète (le rendu réaliste redevient "
           "disponible)."));
    connect(simExitAct_, &QAction::triggered, this, &MainWindow::exitSimulation);
    simPrevColorAct_ = simToolbar_->addAction(tr("⏮ Couleur"));
    simPrevColorAct_->setObjectName(QStringLiteral("action_simPrevColor"));
    simPrevColorAct_->setToolTip(tr("Saute au début du bloc de couleur précédent."));
    connect(simPrevColorAct_, &QAction::triggered, this, [this] { jumpSimulationColor(-1); });
    simNextColorAct_ = simToolbar_->addAction(tr("Couleur ⏭"));
    simNextColorAct_->setObjectName(QStringLiteral("action_simNextColor"));
    simNextColorAct_->setToolTip(tr("Saute au début du bloc de couleur suivant."));
    connect(simNextColorAct_, &QAction::triggered, this, [this] { jumpSimulationColor(+1); });

    simSlider_ = new QSlider(Qt::Horizontal, simToolbar_);
    simSlider_->setMinimum(0);
    simSlider_->setEnabled(false);
    simSlider_->setAccessibleName(tr("Position dans la simulation"));
    connect(simSlider_, &QSlider::valueChanged, this, &MainWindow::onSimSliderMoved);
    simToolbar_->addWidget(simSlider_);

    simLabel_ = new QLabel(tr("— / —"), simToolbar_);
    simLabel_->setMinimumWidth(96);
    simLabel_->setAccessibleName(tr("Progression de la simulation"));
    simToolbar_->addWidget(simLabel_);

    // Vitesse : multiplicateur de l'avance par pas (la base s'adapte déjà à la taille du motif).
    simSpeedCombo_ = new QComboBox(simToolbar_);
    simSpeedCombo_->setAccessibleName(tr("Vitesse de simulation"));
    simSpeedCombo_->setToolTip(tr("Vitesse de lecture de la simulation"));
    simSpeedCombo_->addItem(tr("×0,25"), 0.25);
    simSpeedCombo_->addItem(tr("×1"), 1.0);
    simSpeedCombo_->addItem(tr("×4"), 4.0);
    simSpeedCombo_->addItem(tr("×16"), 16.0);
    simSpeedCombo_->addItem(tr("×64"), 64.0);
    // HP-SIM-001 : cadence réelle d'une machine (700 points/min), indépendante de la taille.
    simSpeedCombo_->addItem(tr("Machine"), -1.0);
    simSpeedCombo_->setCurrentIndex(1);
    simToolbar_->addWidget(simSpeedCombo_);

    // Couleur et objet en cours de couture.
    simSwatch_ = new QLabel(simToolbar_);
    simSwatch_->setFixedSize(14, 14);
    simSwatch_->setAccessibleName(tr("Couleur du fil courant"));
    simToolbar_->addWidget(simSwatch_);
    simObjectLabel_ = new QLabel(simToolbar_);
    simObjectLabel_->setMinimumWidth(140);
    simObjectLabel_->setAccessibleName(tr("Objet en cours de couture"));
    simToolbar_->addWidget(simObjectLabel_);

    // HP-SIM-002/004 : pas à pas, objet précédent/suivant, temps de couture écoulé/total.
    simNavBar_ = new SimulationNavBar(this);
    addToolBar(Qt::BottomToolBarArea, simNavBar_);
    connect(simNavBar_, &SimulationNavBar::stepRequested, this, [this](int step) {
        if (simTimer_->isActive() && simPlayAct_->isChecked()) {
            simPlayAct_->setChecked(false); // un saut manuel met la lecture en pause
        }
        simSlider_->setValue(step);
    });

    simTimer_ = new QTimer(this);
    simTimer_->setInterval(16); // ~60 pas/seconde
    connect(simTimer_, &QTimer::timeout, this, &MainWindow::onSimTick);

    // Un masquage demandé par l'utilisateur (menu Panneaux) l'emporte sur l'affichage
    // automatique de la rangée du bas (cf. refreshThreadStrip) : retenu dans une propriété.
    for (QToolBar* bar : {simToolbar_, static_cast<QToolBar*>(simNavBar_), threadStripBar_}) {
        connect(bar->toggleViewAction(), &QAction::triggered, bar,
                [bar](bool shown) { bar->setProperty("userHidden", !shown); });
    }

    // Les contrôles sont grisés sans séquence ; la rangée n'apparaît qu'avec elle.
    updateSimulationRange();
    refreshThreadStrip();
}

void MainWindow::updateSimulationInfo(int step) {
    if (simNavBar_ != nullptr) {
        simNavBar_->setStep(step);
    }
    {
        std::optional<ObjectId> current;
        if (sequence_ && step >= 0 && step < static_cast<int>(sequence_->commands.size())) {
            const ObjectId source = sequence_->commands[static_cast<std::size_t>(step)].source;
            for (const auto& emb : project_.embroidery_objects) {
                if (emb.id == source) {
                    current = source;
                    break;
                }
            }
        }
        if (current != simHighlightId_) {
            simHighlightId_ = current;
            applySimulationHighlight();
        }
    }
    if (simSwatch_ == nullptr || simObjectLabel_ == nullptr) {
        return;
    }
    if (!sequence_ || step < 0 || step >= static_cast<int>(sequence_->commands.size())) {
        simSwatch_->clear();
        simObjectLabel_->clear();
        return;
    }
    const auto& cmd = sequence_->commands[static_cast<std::size_t>(step)];
    QString name = tr("Design importé");
    std::array<std::uint8_t, 3> rgb{0, 0, 0};
    if (project_.imported_design && !project_.imported_design->color_blocks.empty()) {
        rgb = project_.imported_design->color_blocks.front().rgb;
    }
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.id == cmd.source) {
            name = QString::fromStdString(emb.name);
            rgb = emb.rgb;
            break;
        }
    }
    simSwatch_->setPixmap(
        icons::colorSwatch(QColor(rgb[0], rgb[1], rgb[2]), 12).pixmap(QSize(12, 12)));
    // « fil k/m » : rang du bloc de couleur courant parmi les blocs de la séquence.
    const std::vector<int> starts = colorBlockStarts();
    const auto block = std::upper_bound(starts.begin(), starts.end(), step) - starts.begin();
    const QString thread =
        tr("fil %1/%2").arg(std::max<std::ptrdiff_t>(block, 1)).arg(starts.size());
    simObjectLabel_->setText(simObjectLabel_->fontMetrics().elidedText(
        tr("« %1 » · %2").arg(name, thread), Qt::ElideRight, simObjectLabel_->minimumWidth()));
    simObjectLabel_->setToolTip(tr("%1 — %2").arg(name, thread));
}

void MainWindow::applySimulationHighlight() {
    // Liste Ordre : ligne de l'objet courant en gras sur fond d'accent (la sélection n'est pas
    // touchée).
    if (orderList_ != nullptr) {
        const QColor soft = palette().color(QPalette::Highlight).lighter(170);
        for (int i = 0; i < orderList_->count(); ++i) {
            QListWidgetItem* item = orderList_->item(i);
            const bool current =
                simHighlightId_ && static_cast<std::size_t>(i) < orderRows_.size() &&
                project_.embroidery_objects[orderRows_[std::size_t(i)]].id == *simHighlightId_;
            QFont f = item->font();
            f.setBold(current);
            item->setFont(f);
            item->setData(Qt::BackgroundRole, current ? QVariant(QBrush(soft)) : QVariant());
        }
    }
    if (documentPanel_ != nullptr) {
        documentPanel_->setPlaybackObject(simHighlightId_);
    }
    // Film couleur : pastille du fil de l'objet courant.
    if (threadStrip_ != nullptr) {
        int index = -1;
        if (simHighlightId_ && sequence_) {
            const auto usage = stitch_analysis::thread_usage(project_, *sequence_, &threadLibrary_);
            for (std::size_t u = 0; u < usage.size() && index < 0; ++u) {
                if (std::find(usage[u].objects.begin(), usage[u].objects.end(), *simHighlightId_) !=
                    usage[u].objects.end()) {
                    index = static_cast<int>(u);
                }
            }
        }
        threadStrip_->setCurrent(index);
    }
}

void MainWindow::updateSimulationRange() {
    const bool hasSeq = sequence_ && !sequence_->commands.empty();
    if (simNavBar_ != nullptr) {
        simNavBar_->setSequence(sequence_ ? &*sequence_ : nullptr);
    }
    simPlayAct_->setEnabled(hasSeq);
    simExitAct_->setEnabled(hasSeq);
    simPrevColorAct_->setEnabled(hasSeq);
    simNextColorAct_->setEnabled(hasSeq);
    simSlider_->setEnabled(hasSeq);
    simSpeedCombo_->setEnabled(hasSeq);
    // Le document a changé (cette fonction suit chaque régénération de la séquence) : une
    // simulation en cours porte sur l'ancienne séquence ; elle est réinitialisée plutôt que
    // de continuer sur des points qui n'existent plus.
    if (simulating()) {
        simTimer_->stop();
        if (simPlayAct_->isChecked()) {
            QSignalBlocker block(simPlayAct_);
            simPlayAct_->setChecked(false);
        }
        simPlayAct_->setText(tr("▶ Lecture"));
        simStep_ = -1;
        simWalkValid_ = false;
    }
    if (!hasSeq) {
        simTimer_->stop();
        simPlayAct_->setChecked(false);
        simStep_ = -1;
        simLabel_->setText(tr("— / —"));
        updateSimulationInfo(-1);
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    simSlider_->blockSignals(true);
    simSlider_->setMaximum(n);
    simSlider_->setEnabled(true);
    simSlider_->setValue(n);
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(n).arg(n));
    updateSimulationInfo(-1);
}

void MainWindow::toggleSimulation(/* play */) {
    flushPendingRefresh();
    if (!sequence_ || sequence_->commands.empty()) {
        simPlayAct_->setChecked(false);
        return;
    }
    if (simPlayAct_->isChecked()) {
        // Démarre (ou redémarre depuis le début si on était à la fin).
        if (!simulating() || simStep_ >= static_cast<int>(sequence_->commands.size()) - 1) {
            simStep_ = 0;
            simAccum_ = 0.0;
            renderStitches();
        }
        simPlayAct_->setText(tr("⏸ Pause"));
        simTimer_->start();
    } else {
        simPlayAct_->setText(tr("▶ Lecture"));
        simTimer_->stop();
    }
}

void MainWindow::onSimTick() {
    if (!sequence_) {
        simTimer_->stop();
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    // Avance proportionnellement à la taille (motif long = plus rapide), multipliée par la
    // vitesse choisie ; le reste fractionnaire (x0,25) s'accumule d'un pas au suivant.
    const double speed = simSpeedCombo_ != nullptr ? simSpeedCombo_->currentData().toDouble() : 1.0;
    if (speed < 0.0) { // « Machine » : pas par tick = cadence machine x durée du tick
        simAccum_ += stitch_render::simulation_machine_steps_per_second({}) *
                     (static_cast<double>(simTimer_->interval()) / 1000.0);
    } else {
        simAccum_ += std::max(1, (n + 1) / 400) * (speed > 0.0 ? speed : 1.0);
    }
    const int advance = static_cast<int>(simAccum_);
    if (advance < 1) {
        return;
    }
    simAccum_ -= advance;
    simStep_ = std::min(n, simStep_ + advance);
    simSlider_->blockSignals(true);
    simSlider_->setValue(simStep_);
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(simStep_).arg(n));
    updateSimulationInfo(simStep_);
    appendSimulation(simStep_); // seule la couche points change, et seulement le tronçon neuf
    if (simStep_ >= n) {
        // Fin de lecture : on sort de la simulation (couture complète, rendu réaliste possible).
        exitSimulation();
    }
}

std::vector<int> MainWindow::colorBlockStarts() const {
    std::vector<int> starts{0};
    if (!sequence_) {
        return starts;
    }
    for (std::size_t i = 0; i < sequence_->commands.size(); ++i) {
        if (sequence_->commands[i].type == stitch::CommandType::ColorChange) {
            starts.push_back(static_cast<int>(i));
        }
    }
    return starts;
}

void MainWindow::jumpSimulationColor(int direction) {
    if (!sequence_ || sequence_->commands.empty()) {
        return;
    }
    const int last = static_cast<int>(sequence_->commands.size()) - 1;
    const int current = simulating() ? simStep_ : last;
    const std::vector<int> starts = colorBlockStarts();
    int target = direction > 0 ? last : 0;
    if (direction > 0) {
        const auto it = std::upper_bound(starts.begin(), starts.end(), current);
        target = it == starts.end() ? last : *it;
    } else {
        const auto it = std::lower_bound(starts.begin(), starts.end(), current);
        target = it == starts.begin() ? 0 : *(it - 1);
    }
    simSlider_->setValue(target); // onSimSliderMoved : quitte la simulation si on atteint la fin
}

void MainWindow::exitSimulation() {
    if (!simulating()) {
        return;
    }
    simTimer_->stop();
    if (simPlayAct_->isChecked()) {
        QSignalBlocker block(simPlayAct_);
        simPlayAct_->setChecked(false);
    }
    simPlayAct_->setText(tr("▶ Lecture"));
    simStep_ = -1;
    simWalkValid_ = false;
    if (sequence_ && !sequence_->commands.empty()) {
        const int n = static_cast<int>(sequence_->commands.size()) - 1;
        QSignalBlocker block(simSlider_);
        simSlider_->setValue(n);
        simLabel_->setText(tr("%1 / %2").arg(n).arg(n));
    }
    updateSimulationInfo(-1);
    renderStitches();
}

void MainWindow::onSimSliderMoved(int value) {
    const int n = static_cast<int>(sequence_ ? sequence_->commands.size() : 1) - 1;
    if (value >= n) {
        // Curseur tout à droite : c'est la couture complète, pas une simulation en cours.
        if (simulating()) {
            exitSimulation();
        }
        return;
    }
    simStep_ = value;
    simLabel_->setText(tr("%1 / %2").arg(value).arg(n));
    updateSimulationInfo(value);
    appendSimulation(value);
}

namespace {
// Résumé chiffré d'une séquence, partagé par la fenêtre Statistiques (et lisible par tout
// futur écran qui doit afficher les mêmes chiffres sous la même forme).
QString statsSummaryText(const stitch::StitchStats& stats) {
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    return QObject::tr("Points : %1\nSauts : %2\nCoupes : %3\nChangements de couleur : %4\n"
                       "Dimensions : %5 × %6 mm\nFil cousu estimé : %7 m")
        .arg(stats.stitches)
        .arg(stats.jumps)
        .arg(stats.trims)
        .arg(stats.color_changes)
        .arg(wMm, 0, 'f', 1)
        .arg(hMm, 0, 'f', 1)
        .arg(stats.thread_length_um / 1e9, 0, 'f', 2);
}
} // namespace

void MainWindow::showStatistics() {
    flushPendingRefresh();
    if (!sequence_) {
        statusBar()->showMessage(tr("Statistiques indisponibles : générez d'abord des points."),
                                 6000);
        return;
    }
    QMessageBox::information(this, tr("Statistiques de broderie"),
                             statsSummaryText(stitch::compute_stats(*sequence_)));
}

} // namespace openstitch::desktop
