// SPDX-License-Identifier: Apache-2.0
// Segmentation : dialogue « Segmenter l'image… » non modal avec aperçu en direct. Membres de
// MainWindow séparés de main_window.cpp. Aucune logique métier ici : la segmentation vient de
// libs/segmentation ; l'aperçu travaille sur une COPIE de l'image de travail, dans un fil, et
// s'affiche sur le canevas sans toucher au document ni à l'historique d'annulation. Seule la
// validation (« Segmenter ») modifie le document, par la même commande annulable qu'avant.
#include <QAction>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QStatusBar>
#include <QThread>

#include <algorithm>
#include <memory>

#include "busy_indicator.hpp"
#include "main_window.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "segmentation_dialog.hpp"

namespace openstitch::desktop {

// Une demande de calcul : ses réglages, son numéro (un résultat périmé est ignoré) et son résultat,
// écrit par le fil de calcul puis lu par le fil d'interface une fois le fil terminé.
struct MainWindow::SegmentationJob {
    segmentation::SegmentationOptions options;
    segmentation::DetailMask mask; // copie du masque de détail à la demande (le fil la lit)
    std::uint64_t generation{0};
    std::optional<Result<segmentation::Segmentation>> result;
    double seconds{0.0};
};

namespace {

bool sameOptions(const segmentation::SegmentationOptions& a,
                 const segmentation::SegmentationOptions& b) {
    return a.max_colors == b.max_colors && a.min_region_px == b.min_region_px &&
           a.smoothing_radius_px == b.smoothing_radius_px &&
           a.detail_smoothing_radius_px == b.detail_smoothing_radius_px &&
           a.detail_min_region_px == b.detail_min_region_px;
}

// Le masque du projet n'est utilisable que s'il a la taille de l'image segmentée.
segmentation::DetailMask usableMask(const segmentation::DetailMask& mask, const image::Image& img) {
    return mask.matches(img.width, img.height) ? mask : segmentation::DetailMask{};
}

segmentation::SegmentationOptions withMask(segmentation::SegmentationOptions options,
                                           const segmentation::DetailMask& mask) {
    options.detail_mask = mask.any_detail() ? &mask : nullptr;
    return options;
}

bool sameImage(const image::Image& a, const image::Image& b) {
    return a.width == b.width && a.height == b.height && a.rgba == b.rgba;
}

} // namespace

void MainWindow::openSegmentationDialog() {
    if (!project_.hasImage() || processed_.empty()) {
        return;
    }
    if (segmentDialog_ != nullptr) {
        segmentDialog_->raise();
        segmentDialog_->activateWindow();
        return;
    }
    segmentSnapshot_ = std::make_shared<const image::Image>(processed_);
    segmentCached_.reset();
    segmentCachedOptions_.reset();
    segmentCachedMask_ = {};
    segmentPending_.reset();
    auto* dialog = new SegmentationDialog(project_.mm_per_px.value, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    segmentDialog_ = dialog;
    dialog->setSourceThumbnail(*segmentSnapshot_);
    connect(dialog, &SegmentationDialog::optionsChanged, this,
            &MainWindow::requestSegmentationPreview);
    connect(dialog, &SegmentationDialog::previewToggled, this, [this](bool enabled) {
        if (enabled && segmentDialog_ != nullptr) {
            requestSegmentationPreview(segmentDialog_->options());
        } else {
            // Un résultat encore en vol ne doit jamais réafficher la carte
            // après que l'utilisateur a choisi de voir la source.
            segmentPending_.reset();
            hideSegmentationPreview();
        }
    });
    connect(dialog, &SegmentationDialog::applyRequested, this,
            &MainWindow::applySegmentationFromDialog);
    connect(dialog, &SegmentationDialog::paintDetailRequested, this,
            [this] { setTool(Tool::DetailBrush); });
    connect(dialog, &QDialog::finished, this, [this] { onSegmentationDialogClosed(); });
    dialog->show();
    refreshDetailZonesOverlay(); // les zones déjà peintes apparaissent avec le dialogue
    requestSegmentationPreview(dialog->options());
}

void MainWindow::requestSegmentationPreview(const segmentation::SegmentationOptions& options) {
    if (segmentDialog_ == nullptr || !segmentDialog_->previewEnabled()) {
        return;
    }
    if (segmentCached_ && segmentCachedOptions_ && sameOptions(*segmentCachedOptions_, options) &&
        segmentCachedMask_ == usableMask(project_.detail_mask, *segmentSnapshot_)) {
        showSegmentationPreview(*segmentCached_, segmentCachedSeconds_); // déjà calculé
        return;
    }
    if (segmentThread_ != nullptr) {
        segmentPending_ = options; // un calcul tourne : seule la dernière demande sera lancée après
        return;
    }
    startSegmentationPreview(options);
}

void MainWindow::startSegmentationPreview(const segmentation::SegmentationOptions& options) {
    auto job = std::make_shared<SegmentationJob>();
    job->options = options;
    job->mask = usableMask(project_.detail_mask, *segmentSnapshot_);
    job->options.detail_mask = job->mask.any_detail() ? &job->mask : nullptr;
    job->generation = ++segmentGeneration_;
    segmentJob_ = job;
    segmentDialog_->setBusy(true);
    const std::shared_ptr<const image::Image> snapshot = segmentSnapshot_;
    QThread* thread = QThread::create([job, snapshot] {
        QElapsedTimer timer;
        timer.start();
        job->result = segmentation::segment(*snapshot, job->options);
        job->seconds = static_cast<double>(timer.elapsed()) / 1000.0;
    });
    segmentThread_ = thread;
    // `finished` part du fil de calcul : la connexion vers cette fenêtre est mise en file.
    connect(thread, &QThread::finished, this, &MainWindow::onSegmentationPreviewReady);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::onSegmentationPreviewReady() {
    // Le fil a fini (il sera supprimé plus tard par deleteLater) : il ne compte plus comme « en
    // cours », sinon une demande reçue avant sa suppression resterait en attente.
    segmentThread_ = nullptr;
    const std::shared_ptr<SegmentationJob> job = std::move(segmentJob_);
    segmentJob_.reset();
    if (job == nullptr || segmentDialog_ == nullptr || !job->result) {
        return; // dialogue fermé pendant le calcul : résultat abandonné
    }
    // Le fil est terminé : le suivant (dernière demande reçue pendant ce calcul) peut démarrer.
    const std::optional<segmentation::SegmentationOptions> pending = std::move(segmentPending_);
    segmentPending_.reset();
    if (!*job->result) {
        segmentDialog_->setFailed(tr("Aperçu impossible avec ces réglages."));
    } else {
        segmentCachedOptions_ = job->options;
        segmentCachedOptions_->detail_mask = nullptr; // le pointeur ne survit pas au job
        segmentCachedMask_ = job->mask;
        segmentCachedSeconds_ = job->seconds;
        segmentCached_ = std::move(**job->result);
        if (!pending && segmentDialog_->previewEnabled() &&
            sameOptions(job->options, segmentDialog_->options()) &&
            job->mask == usableMask(project_.detail_mask, *segmentSnapshot_)) {
            showSegmentationPreview(*segmentCached_, job->seconds);
        }
    }
    if (pending && segmentDialog_->previewEnabled()) {
        if (segmentCached_ && segmentCachedOptions_ &&
            sameOptions(*segmentCachedOptions_, *pending) &&
            segmentCachedMask_ == usableMask(project_.detail_mask, *segmentSnapshot_)) {
            requestSegmentationPreview(*pending);
        } else {
            // Le fil précédent a émis `finished` : il n'est plus en cours.
            startSegmentationPreview(*pending);
        }
    }
}

void MainWindow::showSegmentationPreview(const segmentation::Segmentation& seg, double seconds) {
    if (segmentDialog_ != nullptr) {
        // Régions plus petites que le seuil de remplissage de l'auto-numérisation : elles ne
        // deviendront qu'un contour.
        std::size_t small = 0;
        const double minFillMm2 = autodigitize::AutoOptions{}.min_fill_area_mm2;
        const double mm2PerPx = project_.mm_per_px.value * project_.mm_per_px.value;
        for (const auto& slot : seg.region_slots) {
            if (slot && static_cast<double>(slot->pixel_count) * mm2PerPx < minFillMm2) {
                ++small;
            }
        }
        segmentDialog_->setStats({.regions = seg.region_count(),
                                  .colors = segmentation::color_classes(seg).size(),
                                  .smallRegions = small,
                                  .seconds = seconds});
        segmentDialog_->setSegmentationThumbnail(seg);
    }
    previewSegmentation_ = seg;
    if (!showSegAct_->isChecked()) {
        segmentAutoShownMap_ = true;
        showSegAct_->setChecked(true); // redessine la carte (signal toggled)
    } else {
        displayImage(processed_);
    }
}

void MainWindow::hideSegmentationPreview() {
    previewSegmentation_.reset();
    if (segmentAutoShownMap_ && showSegAct_->isChecked()) {
        showSegAct_->setChecked(false);
    } else {
        displayImage(processed_);
    }
    segmentAutoShownMap_ = false;
}

void MainWindow::applySegmentationFromDialog(const segmentation::SegmentationOptions& options) {
    if (!project_.hasImage() || processed_.empty() || segmentDialog_ == nullptr) {
        return;
    }
    // Resegmenter remplace les régions existantes (fusions, couleurs, régions supprimées à la main
    // comprises) : confirmation avant de les écraser. Le dialogue reste ouvert si on refuse.
    if (!confirmDestroySegmentation(tr("Segmenter à nouveau"))) {
        return;
    }
    segmentDialog_->rememberSettings();

    std::optional<segmentation::Segmentation> seg;
    const bool reusable = segmentCached_ && segmentCachedOptions_ &&
                          sameOptions(*segmentCachedOptions_, options) && segmentSnapshot_ &&
                          sameImage(*segmentSnapshot_, processed_) &&
                          segmentCachedMask_ == usableMask(project_.detail_mask, processed_);
    if (reusable) {
        seg = std::move(segmentCached_); // l'aperçu EST le résultat : pas de second calcul
        segmentCached_.reset();
    } else {
        std::optional<BusyIndicator> busy;
        busy.emplace(this, tr("Segmentation de l'image en cours…"),
                     std::initializer_list<QTimer*>{autosaveTimer_, simTimer_});
        const segmentation::DetailMask mask = usableMask(project_.detail_mask, processed_);
        const segmentation::SegmentationOptions withDetail = withMask(options, mask);
        auto computed = busy->run([&] { return segmentation::segment(processed_, withDetail); });
        busy.reset();
        if (!computed) {
            QMessageBox::warning(this, tr("Segmentation impossible"),
                                 tr("L'image n'a pas pu être segmentée avec ces réglages.\n\n%1")
                                     .arg(QString::fromStdString(computed.error().message)));
            return;
        }
        seg = std::move(*computed);
    }
    ++segmentGeneration_; // un calcul d'aperçu encore en cours devient caduc
    segmentPending_.reset();
    previewSegmentation_.reset();
    segmentAutoShownMap_ = false; // la carte reste affichée : c'est le résultat
    const auto regionCount = seg->region_count();
    undoStack_.execute(std::make_unique<commands::SetSegmentationCommand>(std::move(*seg)),
                       project_);
    editSelection([](Selection& sel) { sel.region.reset(); });
    showSegAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Segmentation : %1 régions").arg(regionCount));
    segmentDialog_->close();
}

void MainWindow::onSegmentationDialogClosed() {
    ++segmentGeneration_;
    segmentPending_.reset();
    segmentCached_.reset();
    segmentCachedOptions_.reset();
    segmentCachedMask_ = {};
    segmentSnapshot_.reset();
    refreshDetailZonesOverlay();
    if (previewSegmentation_) {
        hideSegmentationPreview();
    }
}

} // namespace openstitch::desktop
