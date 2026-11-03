// SPDX-License-Identifier: Apache-2.0
// Aide > Manuel d'utilisation, aide contextuelle (Maj+F1) et tutoriel guidé (HP-HELP-001/002).
// Aucune logique métier : choix de la section du manuel selon le panneau qui a le focus ou
// l'outil actif, et ouverture de l'image d'exemple générée par le code.
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QMessageBox>
#include <QStandardPaths>
#include <QWidget>

#include "help_dialogs.hpp"
#include "help_manual.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

QString MainWindow::contextualHelpHeading() const {
    // Panneau ancré qui contient le widget ayant le focus : sa section prime sur l'outil.
    const QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr) {
        const struct {
            const QDockWidget* dock;
            const char* panel;
        } docks[] = {{propertiesDock_, "properties"}, {documentDock_, "document"},
                     {workflowDock_, "workflow"},     {analysisDock_, "analysis"},
                     {orderDock_, "order"},           {filterDock_, "filters"},
                     {threadDock_, "threads"},        {colorsDock_, "colors"},
                     {historyDock_, "history"}};
        for (const auto& entry : docks) {
            if (entry.dock != nullptr && (entry.dock == focus || entry.dock->isAncestorOf(focus))) {
                return manualHeadingForPanel(QString::fromLatin1(entry.panel));
            }
        }
    }
    return manualHeadingForTool(currentTool_);
}

void MainWindow::showManual(const QString& heading) {
    if (manualDialog_ == nullptr) {
        auto* dialog = new ManualDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        manualDialog_ = dialog;
    }
    manualDialog_->show();
    manualDialog_->raise();
    manualDialog_->activateWindow();
    if (!heading.isEmpty()) {
        manualDialog_->showTopic(heading);
    }
}

void MainWindow::showContextualHelp() {
    showManual(contextualHelpHeading());
}

void MainWindow::startGuidedTour() {
    // Image d'exemple générée (aucun fichier embarqué, aucun droit d'auteur) : écrite dans le
    // dossier temporaire puis ouverte par le chemin d'import normal (dialogue de taille).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(dir).filePath(QStringLiteral("openstitch_tutoriel.png"));
    if (!tutorialSampleImage().save(path)) {
        QMessageBox::warning(this, tr("Tutoriel"),
                             tr("Impossible de créer l'image d'exemple dans le dossier "
                                "temporaire."));
        return;
    }
    showQuickStartDialog();
    openImageFile(path);
}

} // namespace openstitch::desktop
