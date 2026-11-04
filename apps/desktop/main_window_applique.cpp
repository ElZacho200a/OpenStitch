// SPDX-License-Identifier: Apache-2.0
// Objet appliqué (HP-SPEC-001) : câblage UNIQUEMENT. La conversion passe par
// `ConvertFillGroupCommand` (annulable) ; la génération et l'export de découpe vivent dans
// `stitch_generation` / `project_ops`.
#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <QStatusBar>

#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/project_ops/applique_cut.hpp"

namespace openstitch::desktop {

void MainWindow::buildAppliqueMenu(QMenu* embMenu) {
    appliqueConvertAct_ = embMenu->addAction(tr("Convertir en appliqué"));
    appliqueConvertAct_->setObjectName(QStringLiteral("action_appliqueConvert"));
    appliqueConvertAct_->setToolTip(
        tr("Ligne de placement, arrêt, point de maintien, arrêt, puis satin de couverture."));
    connect(appliqueConvertAct_, &QAction::triggered, this,
            &MainWindow::convertSelectionToApplique);
    appliqueCutAct_ = embMenu->addAction(tr("Exporter le contour de découpe…"));
    appliqueCutAct_->setObjectName(QStringLiteral("action_appliqueCut"));
    appliqueCutAct_->setToolTip(
        tr("Contour du tissu des appliqués (SVG ou DXF) pour un traceur ou un cutter."));
    connect(appliqueCutAct_, &QAction::triggered, this, &MainWindow::exportAppliqueCut);
}

bool MainWindow::convertSelectionToApplique() {
    const std::vector<ObjectId> selection = selectedEmbroideryIds();
    if (selection.empty()) {
        statusBar()->showMessage(tr("Sélectionnez un objet de broderie à convertir."), 5000);
        return false;
    }
    const auto* emb = project_.findEmbroidery(selection.front());
    const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
    if (source == nullptr || source->paths.empty()) {
        statusBar()->showMessage(tr("Aucun contour source pour l'appliqué."), 5000);
        return false;
    }
    undoStack_.execute(std::make_unique<commands::ConvertFillGroupCommand>(
                           selection.front(), document::AppliqueParams{}, "Type : appliqué"),
                       project_);
    refreshImage();
    setWindowModified(!undoStack_.isClean());
    updateActions();
    statusBar()->showMessage(tr("Objet converti en appliqué — Ctrl+Z pour annuler."), 5000);
    return true;
}

bool MainWindow::exportAppliqueCutTo(const QString& path) {
    const auto report = project_ops::export_applique_cut(project_, path.toStdWString());
    if (!report) {
        statusBar()->showMessage(QString::fromStdString(report.error().message), 8000);
        return false;
    }
    statusBar()->showMessage(
        tr("Contour de découpe écrit : %1 contour(s).").arg(static_cast<qulonglong>(report->paths)),
        8000);
    return true;
}

void MainWindow::exportAppliqueCut() {
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Exporter le contour de découpe"), QString(),
                                     tr("SVG de découpe (*.svg);;DXF (*.dxf)"));
    if (!path.isEmpty()) {
        exportAppliqueCutTo(path);
    }
}

} // namespace openstitch::desktop
