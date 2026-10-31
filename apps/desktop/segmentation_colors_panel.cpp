// SPDX-License-Identifier: Apache-2.0
#include "segmentation_colors_panel.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace openstitch::desktop {

namespace {

// Couleur d'une ligne : rgb codé sur 24 bits dans le rôle utilisateur.
constexpr int kRgbRole = Qt::UserRole;
constexpr int kPreviewDelayMs = 150;

quint32 pack(const SegmentationColorsPanel::Rgb& rgb) {
    return (static_cast<quint32>(rgb[0]) << 16) | (static_cast<quint32>(rgb[1]) << 8) | rgb[2];
}

SegmentationColorsPanel::Rgb unpack(quint32 value) {
    return {static_cast<std::uint8_t>((value >> 16) & 0xFF),
            static_cast<std::uint8_t>((value >> 8) & 0xFF),
            static_cast<std::uint8_t>(value & 0xFF)};
}

QIcon swatch(const SegmentationColorsPanel::Rgb& rgb) {
    QPixmap pixmap(18, 18);
    pixmap.fill(QColor(rgb[0], rgb[1], rgb[2]));
    return QIcon(pixmap);
}

} // namespace

SegmentationColorsPanel::SegmentationColorsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("segmentationColorsPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("segColorsSummary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    // Les teintes techniques servent à définir les REGIONS. L'attribution des fils
    // et les références machine se font après numérisation dans le panneau Fils.
    auto* roleHint = new QLabel(
        tr("Couleurs de segmentation : elles définissent les zones de l'image, pas les fils "
           "à broder. Choisissez les fils dans le panneau Fils après la numérisation."),
        this);
    roleHint->setObjectName(QStringLiteral("segColorsRoleHint"));
    roleHint->setAccessibleName(tr("Rôle des couleurs de segmentation"));
    roleHint->setWordWrap(true);
    layout->addWidget(roleHint);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("segColorsList"));
    list_->setAccessibleName(tr("Couleurs des régions de l'image, sans référence de fil"));
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setToolTip(
        tr("Cliquez une teinte pour sélectionner toutes ses régions ; Ctrl ou Maj+clic "
           "pour en choisir plusieurs. La teinte active (dernière cliquée) sert de "
           "cible à la fusion."));
    layout->addWidget(list_, 1);

    mergeButton_ = new QPushButton(tr("Fusionner dans la teinte active"), this);
    mergeButton_->setObjectName(QStringLiteral("segColorsMergeButton"));
    mergeButton_->setToolTip(tr("Toutes les régions des teintes sélectionnées prennent la couleur "
                                "de la teinte active. Un seul pas d'annulation."));
    layout->addWidget(mergeButton_);

    // Réduction automatique, avec aperçu sur le canevas avant d'appliquer.
    auto* form = new QFormLayout;
    countSpin_ = new QSpinBox(this);
    countSpin_->setObjectName(QStringLiteral("segColorsCountSpin"));
    countSpin_->setToolTip(tr("Nombre de couleurs à garder : les plus proches sont fusionnées, le "
                              "plus gros aplat garde sa teinte exacte."));
    distanceSpin_ = new QDoubleSpinBox(this);
    distanceSpin_->setObjectName(QStringLiteral("segColorsDistanceSpin"));
    distanceSpin_->setRange(0.0, 30.0);
    distanceSpin_->setSingleStep(0.5);
    distanceSpin_->setDecimals(1);
    distanceSpin_->setSpecialValueText(tr("désactivé"));
    distanceSpin_->setToolTip(
        tr("Fusionne aussi les teintes dont l'écart de couleur (CIEDE2000) est "
           "inférieur à cette valeur : de 1 (quasi identiques) à 10 (voisines)."));
    form->addRow(tr("Réduire à :"), countSpin_);
    form->addRow(tr("Fusionner si écart ≤ :"), distanceSpin_);
    layout->addLayout(form);

    adjacentCheck_ =
        new QCheckBox(tr("Fusionner aussi les régions voisines de même couleur"), this);
    adjacentCheck_->setObjectName(QStringLiteral("segColorsAdjacentCheck"));
    adjacentCheck_->setChecked(true);
    adjacentCheck_->setToolTip(
        tr("Évite une frontière (et un objet de broderie) inutile entre deux "
           "régions devenues de même couleur."));
    layout->addWidget(adjacentCheck_);

    auto* buttons = new QHBoxLayout;
    applyButton_ = new QPushButton(tr("Appliquer"), this);
    applyButton_->setObjectName(QStringLiteral("segColorsApplyButton"));
    cancelButton_ = new QPushButton(tr("Annuler l'aperçu"), this);
    cancelButton_->setObjectName(QStringLiteral("segColorsCancelButton"));
    buttons->addWidget(applyButton_);
    buttons->addWidget(cancelButton_);
    layout->addLayout(buttons);

    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(kPreviewDelayMs);

    connect(list_, &QListWidget::itemSelectionChanged, this,
            &SegmentationColorsPanel::onSelectionChanged);
    connect(list_, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(mergeButton_, &QPushButton::clicked, this, [this] {
        const QListWidgetItem* current = list_->currentItem();
        const std::vector<Rgb> sources = selectedColors();
        if (current == nullptr || sources.size() < 2) {
            return;
        }
        emit mergeRequested(sources, unpack(current->data(kRgbRole).toUInt()), mergeAdjacent());
    });
    connect(countSpin_, &QSpinBox::valueChanged, this, [this] { scheduleReductionPreview(); });
    connect(distanceSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { scheduleReductionPreview(); });
    connect(adjacentCheck_, &QCheckBox::toggled, this, [this] { scheduleReductionPreview(); });
    connect(previewTimer_, &QTimer::timeout, this, &SegmentationColorsPanel::emitReductionPreview);
    connect(applyButton_, &QPushButton::clicked, this, [this] {
        if (reductionActive()) {
            emit reductionApplyRequested(maxColors(), distanceSpin_->value(), mergeAdjacent());
        }
    });
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        // Revient à « aucune réduction » : les curseurs reprennent leur valeur neutre.
        updating_ = true;
        countSpin_->setValue(classCount_);
        distanceSpin_->setValue(0.0);
        updating_ = false;
        previewTimer_->stop();
        updateButtons();
        emit reductionPreviewCleared();
    });
    setClasses({}, 0);
}

std::vector<SegmentationColorsPanel::Rgb> SegmentationColorsPanel::selectedColors() const {
    std::vector<Rgb> out;
    for (int row = 0; row < list_->count(); ++row) {
        const QListWidgetItem* item = list_->item(row);
        if (item->isSelected()) {
            out.push_back(unpack(item->data(kRgbRole).toUInt()));
        }
    }
    return out;
}

bool SegmentationColorsPanel::mergeAdjacent() const {
    return adjacentCheck_->isChecked();
}

int SegmentationColorsPanel::maxColors() const {
    return countSpin_->value() < classCount_ ? countSpin_->value() : 0;
}

bool SegmentationColorsPanel::reductionActive() const {
    return countSpin_->value() < classCount_ || distanceSpin_->value() > 0.0;
}

void SegmentationColorsPanel::setClasses(std::vector<segmentation::ColorClass> classes,
                                         std::size_t total_pixels) {
    const std::vector<Rgb> keep = selectedColors();
    updating_ = true;
    previewTimer_->stop();
    list_->clear();
    for (const segmentation::ColorClass& cls : classes) {
        const double percent = total_pixels > 0 ? 100.0 * static_cast<double>(cls.pixel_count) /
                                                      static_cast<double>(total_pixels)
                                                : 0.0;
        auto* item = new QListWidgetItem(
            swatch(cls.rgb),
            tr("%1 %  ·  %n région(s)", "", static_cast<int>(cls.regions.size()))
                .arg(percent, 0, 'f', 1),
            list_);
        item->setData(kRgbRole, pack(cls.rgb));
        item->setToolTip(
            tr("Teinte de segmentation RGB(%1, %2, %3). Aucun fil machine n'est attribué ici.")
                .arg(cls.rgb[0])
                .arg(cls.rgb[1])
                .arg(cls.rgb[2]));
        if (std::find(keep.begin(), keep.end(), cls.rgb) != keep.end()) {
            item->setSelected(true);
        }
    }
    classCount_ = static_cast<int>(classes.size());
    countSpin_->setRange(1, std::max(1, classCount_));
    countSpin_->setValue(std::max(1, classCount_));
    distanceSpin_->setValue(0.0);
    summary_->setText(classCount_ == 0 ? tr("Aucune segmentation.")
                                       : tr("%n teinte(s) de segmentation.", "", classCount_));
    updating_ = false;
    updateButtons();
}

void SegmentationColorsPanel::onSelectionChanged() {
    updateButtons();
    if (!updating_) {
        emit colorsSelected(selectedColors());
    }
}

void SegmentationColorsPanel::scheduleReductionPreview() {
    if (updating_) {
        return;
    }
    updateButtons();
    previewTimer_->start();
}

void SegmentationColorsPanel::emitReductionPreview() {
    if (reductionActive()) {
        emit reductionPreviewRequested(maxColors(), distanceSpin_->value(), mergeAdjacent());
    } else {
        emit reductionPreviewCleared();
    }
}

void SegmentationColorsPanel::updateButtons() {
    mergeButton_->setEnabled(selectedColors().size() >= 2 && list_->currentItem() != nullptr);
    applyButton_->setEnabled(reductionActive());
    cancelButton_->setEnabled(reductionActive());
}

} // namespace openstitch::desktop
