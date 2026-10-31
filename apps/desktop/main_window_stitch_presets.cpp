// SPDX-License-Identifier: Apache-2.0
// Préréglages de points (HP-ENG-013) : câblage UNIQUEMENT. Un préréglage est un `StitchParams`
// nommé stocké dans le projet (`Project::stitch_presets`) ; chaque action est une ICommand
// annulable (un pas d'annulation, même pour plusieurs objets sélectionnés).
#include <QAction>
#include <QInputDialog>
#include <QMenu>
#include <QStatusBar>

#include "main_window.hpp"
#include "openstitch/commands/stitch_preset_commands.hpp"

namespace openstitch::desktop {

void MainWindow::buildStitchPresetMenu(QMenu* embMenu) {
    stitchPresetMenu_ = embMenu->addMenu(tr("Préréglages de points"));
    stitchPresetMenu_->setObjectName(QStringLiteral("menu_stitchPresets"));
    stitchPresetMenu_->setToolTip(
        tr("Jeux de réglages de couture nommés, enregistrés dans le projet."));
    connect(stitchPresetMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildStitchPresetMenu);
    rebuildStitchPresetMenu();
}

void MainWindow::rebuildStitchPresetMenu() {
    if (stitchPresetMenu_ == nullptr) {
        return;
    }
    stitchPresetMenu_->clear();
    const std::vector<ObjectId> selection = selectedEmbroideryIds();

    stitchPresetSaveAct_ =
        stitchPresetMenu_->addAction(tr("Enregistrer les réglages de la sélection…"));
    stitchPresetSaveAct_->setObjectName(QStringLiteral("action_stitchPresetSave"));
    stitchPresetSaveAct_->setEnabled(!selection.empty());
    connect(stitchPresetSaveAct_, &QAction::triggered, this,
            &MainWindow::saveStitchPresetFromSelection);
    stitchPresetMenu_->addSeparator();

    if (project_.stitch_presets.empty()) {
        auto* none = stitchPresetMenu_->addAction(tr("(aucun préréglage)"));
        none->setEnabled(false);
        return;
    }
    for (std::size_t i = 0; i < project_.stitch_presets.size(); ++i) {
        const QString name = QString::fromStdString(project_.stitch_presets[i].name);
        auto* apply = stitchPresetMenu_->addAction(tr("Appliquer « %1 »").arg(name));
        apply->setObjectName(QStringLiteral("action_stitchPresetApply_%1").arg(i));
        apply->setEnabled(!selection.empty());
        connect(apply, &QAction::triggered, this,
                [this, i] { applyStitchPreset(static_cast<int>(i)); });
    }
    stitchPresetMenu_->addSeparator();
    auto* removeMenu = stitchPresetMenu_->addMenu(tr("Supprimer un préréglage"));
    for (std::size_t i = 0; i < project_.stitch_presets.size(); ++i) {
        const QString name = QString::fromStdString(project_.stitch_presets[i].name);
        auto* remove = removeMenu->addAction(name);
        remove->setObjectName(QStringLiteral("action_stitchPresetDelete_%1").arg(i));
        connect(remove, &QAction::triggered, this,
                [this, i] { deleteStitchPreset(static_cast<int>(i)); });
    }
}

void MainWindow::saveStitchPresetFromSelection() {
    const std::vector<ObjectId> selection = selectedEmbroideryIds();
    if (selection.empty()) {
        statusBar()->showMessage(tr("Sélectionnez un objet de broderie à enregistrer."), 5000);
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Préréglage de points"), tr("Nom du préréglage :"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (ok && !name.isEmpty()) {
        saveStitchPreset(name);
    }
}

void MainWindow::saveStitchPreset(const QString& name) {
    const std::vector<ObjectId> selection = selectedEmbroideryIds();
    const QString trimmed = name.trimmed();
    if (selection.empty() || trimmed.isEmpty()) {
        return;
    }
    const auto* source = project_.findEmbroidery(selection.front());
    if (source == nullptr) {
        return;
    }
    undoStack_.execute(
        std::make_unique<commands::SaveStitchPresetCommand>(trimmed.toStdString(), source->params),
        project_);
    setWindowModified(!undoStack_.isClean());
    updateActions();
    rebuildStitchPresetMenu();
    statusBar()->showMessage(tr("Préréglage « %1 » enregistré.").arg(trimmed), 5000);
}

void MainWindow::applyStitchPreset(int index) {
    const std::vector<ObjectId> selection = selectedEmbroideryIds();
    if (selection.empty() || index < 0 ||
        static_cast<std::size_t>(index) >= project_.stitch_presets.size()) {
        return;
    }
    const document::StitchPreset preset = project_.stitch_presets[static_cast<std::size_t>(index)];
    undoStack_.execute(std::make_unique<commands::ApplyStitchPresetCommand>(selection, preset),
                       project_);
    refreshImage();
    setWindowModified(!undoStack_.isClean());
    updateActions();
    statusBar()->showMessage(tr("Préréglage « %1 » appliqué — Ctrl+Z pour annuler.")
                                 .arg(QString::fromStdString(preset.name)),
                             5000);
}

void MainWindow::deleteStitchPreset(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= project_.stitch_presets.size()) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RemoveStitchPresetCommand>(index), project_);
    setWindowModified(!undoStack_.isClean());
    updateActions();
    rebuildStitchPresetMenu();
}

} // namespace openstitch::desktop
