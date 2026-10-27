// SPDX-License-Identifier: Apache-2.0
#include "segmentation_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kChangeDelayMs = 250;
constexpr int kThumbnailWidth = 176;
constexpr int kThumbnailHeight = 112;

// Redimensionnement directement dans la miniature : échantillonnage borné
// (au plus 19 712 pixels), même pour une photographie de plusieurs mégapixels.
QSize thumbnailSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    const double scale =
        std::min(static_cast<double>(kThumbnailWidth) / width,
                 static_cast<double>(kThumbnailHeight) / height);
    return QSize(std::clamp(static_cast<int>(std::lround(width * scale)), 1, kThumbnailWidth),
                 std::clamp(static_cast<int>(std::lround(height * scale)), 1, kThumbnailHeight));
}

QRgb checkerColor(int x, int y) {
    const int gray = (x / 12 + y / 12) % 2 == 0 ? 235 : 210;
    return qRgb(gray, gray, gray);
}

QImage sourceThumbnail(const image::Image& source) {
    const QSize size = thumbnailSize(source.width, source.height);
    if (size.isEmpty() ||
        source.rgba.size() !=
            static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4U) {
        return {};
    }
    QImage result(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        const int sy = static_cast<int>(static_cast<std::int64_t>(y) * source.height / size.height());
        for (int x = 0; x < size.width(); ++x) {
            const int sx =
                static_cast<int>(static_cast<std::int64_t>(x) * source.width / size.width());
            const std::size_t index =
                (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) +
                 static_cast<std::size_t>(sx)) *
                4U;
            const auto* p = source.rgba.data() + index;
            const int alpha = p[3];
            const QColor background(checkerColor(x, y));
            const int red = (p[0] * alpha + background.red() * (255 - alpha)) / 255;
            const int green = (p[1] * alpha + background.green() * (255 - alpha)) / 255;
            const int blue = (p[2] * alpha + background.blue() * (255 - alpha)) / 255;
            result.setPixel(x, y, qRgb(red, green, blue));
        }
    }
    return result;
}

QImage mapThumbnail(const segmentation::Segmentation& seg) {
    const QSize size = thumbnailSize(seg.width, seg.height);
    if (size.isEmpty() ||
        seg.labels.size() !=
            static_cast<std::size_t>(seg.width) * static_cast<std::size_t>(seg.height)) {
        return {};
    }
    QImage result(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        const int sy = static_cast<int>(static_cast<std::int64_t>(y) * seg.height / size.height());
        for (int x = 0; x < size.width(); ++x) {
            const int sx = static_cast<int>(static_cast<std::int64_t>(x) * seg.width / size.width());
            const auto label = seg.labels[static_cast<std::size_t>(sy) * seg.width + sx];
            if (label == 0U || static_cast<std::size_t>(label) > seg.region_slots.size() ||
                !seg.region_slots[label - 1U]) {
                result.setPixel(x, y, checkerColor(x, y));
                continue;
            }
            const auto& rgb = seg.region_slots[label - 1U]->rgb;
            result.setPixel(x, y, qRgb(rgb[0], rgb[1], rgb[2]));
        }
    }
    return result;
}

} // namespace

SegmentationDialog::SegmentationDialog(double mmPerPx, QWidget* parent)
    : QDialog(parent), mmPerPx_(mmPerPx) {
    setObjectName(QStringLiteral("segmentDialog"));
    setWindowTitle(tr("Segmenter l'image"));
    setModal(false); // le canevas reste utilisable (zoom, déplacement) pour juger l'aperçu
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    colorsSpin_ = new QSpinBox(this);
    colorsSpin_->setObjectName(QStringLiteral("segmentColorsSpin"));
    colorsSpin_->setRange(2, 64);
    colorsSpin_->setValue(ui_memory::intValue(QStringLiteral("segment/colors"), 8, 2, 64));

    minSizeSpin_ = new QSpinBox(this);
    minSizeSpin_->setObjectName(QStringLiteral("segmentMinSizeSpin"));
    minSizeSpin_->setRange(1, 100'000);
    minSizeSpin_->setValue(ui_memory::intValue(QStringLiteral("segment/minSize"), 16, 1, 100'000));
    minSizeSpin_->setSuffix(tr(" px"));
    // Équivalent physique de la taille minimale (l'échelle de l'image est connue depuis l'import) :
    // l'IA raisonne en mm², la même unité ici évite de convertir de tête.
    minSizeMm_ = new QLabel(this);
    minSizeMm_->setObjectName(QStringLiteral("segmentMinSizeMm"));
    minSizeMm_->setEnabled(false);
    auto* minSizeRow = new QWidget(this);
    auto* minSizeLayout = new QHBoxLayout(minSizeRow);
    minSizeLayout->setContentsMargins(0, 0, 0, 0);
    minSizeLayout->addWidget(minSizeSpin_);
    minSizeLayout->addWidget(minSizeMm_);

    // L'affectation pixel-à-pixel au centre de couleur le plus proche est intrinsèquement bruitée
    // (effet « poivre et sel » sur les photos, dégradés, artefacts JPEG) : lissée par défaut par
    // vote local majoritaire (cf. segmentation::segment).
    smoothingSpin_ = new QSpinBox(this);
    smoothingSpin_->setObjectName(QStringLiteral("segmentSmoothingSpin"));
    smoothingSpin_->setRange(0, 20);
    smoothingSpin_->setValue(ui_memory::intValue(QStringLiteral("segment/smoothing"), 3, 0, 20));
    smoothingSpin_->setSuffix(tr(" px"));
    smoothingSpin_->setToolTip(
        tr("0 = désactivé. Arrondit les frontières et absorbe le bruit pixel à pixel ; "
           "une valeur trop élevée efface les détails plus fins qu'elle."));

    form->addRow(tr("Nombre maximal de couleurs :"), colorsSpin_);
    form->addRow(tr("Taille minimale de région :"), minSizeRow);
    form->addRow(tr("Lissage des formes :"), smoothingSpin_);
    layout->addLayout(form);

    auto* compareRow = new QHBoxLayout;
    compareRow->setSpacing(8);
    const auto makeThumbnail = [this, compareRow](const QString& title,
                                                   const QString& objectName,
                                                   const QString& accessibleName) {
        auto* column = new QVBoxLayout;
        auto* caption = new QLabel(title, this);
        caption->setWordWrap(true);
        column->addWidget(caption);
        auto* thumb = new QLabel(this);
        thumb->setObjectName(objectName);
        thumb->setAccessibleName(accessibleName);
        thumb->setFixedSize(kThumbnailWidth, kThumbnailHeight);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFrameShape(QFrame::StyledPanel);
        thumb->setText(tr("Aucun aperçu"));
        column->addWidget(thumb);
        compareRow->addLayout(column);
        return thumb;
    };
    sourceThumbnail_ =
        makeThumbnail(tr("Avant — image de travail"), QStringLiteral("segmentSourceThumbnail"),
                      tr("Image avant segmentation"));
    segmentationThumbnail_ =
        makeThumbnail(tr("Après — carte des régions"), QStringLiteral("segmentResultThumbnail"),
                      tr("Résultat de la segmentation"));
    layout->addLayout(compareRow);

    previewCheck_ = new QCheckBox(tr("Aperçu sur l'image"), this);
    previewCheck_->setObjectName(QStringLiteral("segmentPreviewCheck"));
    previewCheck_->setChecked(true);
    previewCheck_->setToolTip(tr("Affiche la carte des régions telle qu'elle sera créée, sans rien "
                                 "modifier tant que vous ne cliquez pas « Segmenter »."));
    layout->addWidget(previewCheck_);

    statsLabel_ = new QLabel(this);
    statsLabel_->setObjectName(QStringLiteral("segmentStatsLabel"));
    statsLabel_->setWordWrap(true);
    layout->addWidget(statsLabel_);
    stateLabel_ = new QLabel(this);
    stateLabel_->setObjectName(QStringLiteral("segmentStateLabel"));
    stateLabel_->setEnabled(false);
    layout->addWidget(stateLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    applyButton_ = buttons->addButton(tr("Segmenter"), QDialogButtonBox::AcceptRole);
    applyButton_->setObjectName(QStringLiteral("segmentApplyButton"));
    applyButton_->setDefault(true);
    layout->addWidget(buttons);

    changeTimer_ = new QTimer(this);
    changeTimer_->setSingleShot(true);
    changeTimer_->setInterval(kChangeDelayMs);

    connect(colorsSpin_, &QSpinBox::valueChanged, this, [this] { scheduleChange(); });
    connect(minSizeSpin_, &QSpinBox::valueChanged, this, [this] {
        refreshMinSizeMm();
        scheduleChange();
    });
    connect(smoothingSpin_, &QSpinBox::valueChanged, this, [this] { scheduleChange(); });
    connect(changeTimer_, &QTimer::timeout, this, [this] { emit optionsChanged(options()); });
    connect(previewCheck_, &QCheckBox::toggled, this, &SegmentationDialog::previewToggled);
    connect(applyButton_, &QPushButton::clicked, this, [this] { emit applyRequested(options()); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    refreshMinSizeMm();
}

segmentation::SegmentationOptions SegmentationDialog::options() const {
    return {.max_colors = colorsSpin_->value(),
            .min_region_px = minSizeSpin_->value(),
            .smoothing_radius_px = smoothingSpin_->value()};
}

bool SegmentationDialog::previewEnabled() const {
    return previewCheck_->isChecked();
}

void SegmentationDialog::setBusy(bool busy) {
    stateLabel_->setText(busy ? tr("Calcul de l'aperçu…") : QString());
    if (busy) {
        segmentationThumbnail_->clear();
        segmentationThumbnail_->setText(tr("Calcul en cours…"));
        statsLabel_->clear();
    }
}

void SegmentationDialog::setStats(const Stats& stats) {
    stateLabel_->setText(tr("Calculé en %1 s.").arg(stats.seconds, 0, 'f', 1));
    statsLabel_->setText(
        tr("%n région(s), %1 couleur(s) ; %2 région(s) trop petite(s) pour un remplissage "
           "(simple contour).",
           "", static_cast<int>(stats.regions))
            .arg(stats.colors)
            .arg(stats.smallRegions));
}

void SegmentationDialog::setFailed(const QString& message) {
    stateLabel_->setText(message);
    statsLabel_->clear();
    segmentationThumbnail_->clear();
    segmentationThumbnail_->setText(message);
}

void SegmentationDialog::setSourceThumbnail(const image::Image& source) {
    const QImage thumb = sourceThumbnail(source);
    if (thumb.isNull()) {
        sourceThumbnail_->clear();
        sourceThumbnail_->setText(tr("Image indisponible"));
    } else {
        sourceThumbnail_->setPixmap(QPixmap::fromImage(thumb));
    }
}

void SegmentationDialog::setSegmentationThumbnail(const segmentation::Segmentation& seg) {
    const QImage thumb = mapThumbnail(seg);
    if (thumb.isNull()) {
        segmentationThumbnail_->clear();
        segmentationThumbnail_->setText(tr("Aperçu indisponible"));
    } else {
        segmentationThumbnail_->setPixmap(QPixmap::fromImage(thumb));
    }
}

void SegmentationDialog::rememberSettings() const {
    ui_memory::setIntValue(QStringLiteral("segment/colors"), colorsSpin_->value());
    ui_memory::setIntValue(QStringLiteral("segment/minSize"), minSizeSpin_->value());
    ui_memory::setIntValue(QStringLiteral("segment/smoothing"), smoothingSpin_->value());
}

void SegmentationDialog::scheduleChange() {
    changeTimer_->start();
}

void SegmentationDialog::refreshMinSizeMm() {
    minSizeMm_->setText(tr("≈ %1 mm²").arg(minSizeSpin_->value() * mmPerPx_ * mmPerPx_, 0, 'f', 2));
}

} // namespace openstitch::desktop
