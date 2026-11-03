// SPDX-License-Identifier: Apache-2.0
// Fichier > Exporter l'image… (HP-FMT-011) : image de présentation du motif (PNG / JPEG). Aucun
// calcul ici : le rendu vient de `stitch_render::render_sequence` (via export_image.cpp) sur la
// séquence EFFECTIVE déjà en cache (`sequence_`), jamais sur la séquence brute ; Qt ne sert qu'à
// l'écriture du fichier.
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>

#include "busy_indicator.hpp"
#include "export_image.hpp"
#include "export_image_dialog.hpp"
#include "fabric_params.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

void MainWindow::buildExportImageAction(QMenu* fileMenu) {
    exportImageAct_ = fileMenu->addAction(tr("Exporter l'i&mage (PNG, JPEG)…"));
    exportImageAct_->setObjectName(QStringLiteral("action_exportImage"));
    exportImageAct_->setToolTip(
        tr("Image du motif brodé à la résolution choisie (marge, fond de tissu opaque ou "
           "transparent, qualité), pour un devis ou un client."));
    connect(exportImageAct_, &QAction::triggered, this, &MainWindow::exportImage);
}

void MainWindow::exportImage() {
    flushPendingRefresh(); // HP-PERF-009
    const QString title = tr("Exporter l'image");
    if (!sequence_ || stitch::compute_stats(*sequence_).stitches == 0) {
        QMessageBox::information(this, title, tr("Générez d'abord des points de broderie."));
        return;
    }
    const auto stats = stitch::compute_stats(*sequence_);
    const double widthMm = (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
    const double heightMm = (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;

    ExportImageDialog dialog(widthMm, heightMm,
                             withProjectFabric(realistic_.prefs.params, project_), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ExportImageSettings settings = dialog.settings();
    const QString ext = imageFileExtension(settings.format);
    const QString filter = settings.format == ImageFileFormat::Jpeg ? tr("Image JPEG (*.jpg)")
                                                                    : tr("Image PNG (*.png)");
    QString file = QFileDialog::getSaveFileName(this, title, suggestedFilePath(ext), filter);
    if (file.isEmpty()) {
        return;
    }
    // Extension forcée ; « .jpeg » reste accepté pour un JPEG.
    const QString suffix = QFileInfo(file).suffix();
    const bool okSuffix = suffix.compare(ext, Qt::CaseInsensitive) == 0 ||
                          (settings.format == ImageFileFormat::Jpeg &&
                           suffix.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0);
    if (!okSuffix) {
        file += QLatin1Char('.') + ext;
    }

    // Calcul sur un instantané, hors du fil d'interface dès qu'il peut dépasser ~100 ms.
    const stitch::StitchSequence sequenceSnapshot = *sequence_;
    QImage image;
    if (exportImageIsHeavy(stats, settings)) {
        BusyIndicator busy(this, tr("Rendu de l'image en cours…"), {autosaveTimer_, simTimer_});
        const document::Project snapshot = project_;
        image = busy.run([&] { return render_export_image(snapshot, sequenceSnapshot, settings); });
    } else {
        image = render_export_image(project_, sequenceSnapshot, settings);
    }
    if (image.isNull()) {
        QMessageBox::warning(this, title, tr("Le motif ne contient aucun point à dessiner."));
        return;
    }
    QString error;
    if (!write_export_image(image, file, settings, &error)) {
        QMessageBox box(QMessageBox::Warning, title,
                        tr("Le fichier %1 n'a pas pu être écrit. L'ancien fichier, s'il existait, "
                           "est intact.")
                            .arg(QFileInfo(file).fileName()),
                        QMessageBox::Ok, this);
        box.setDetailedText(error);
        box.exec();
        return;
    }
    rememberLastDirectory(file);
    offerRevealInFolder(file, tr("Image exportée : %1 (%2 × %3 px).")
                                  .arg(QFileInfo(file).fileName())
                                  .arg(image.width())
                                  .arg(image.height()));
}

} // namespace openstitch::desktop
