// SPDX-License-Identifier: Apache-2.0
#include "segmentation_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

constexpr int kChangeDelayMs = 250;

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
