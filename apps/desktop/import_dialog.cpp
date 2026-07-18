// SPDX-License-Identifier: Apache-2.0
#include "import_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>

namespace openstitch::desktop {

ImportDialog::ImportDialog(int widthPx, int heightPx, QWidget* parent)
    : QDialog(parent), widthPx_(widthPx), heightPx_(heightPx) {
    setWindowTitle(tr("Taille physique de l'image"));

    auto* layout = new QFormLayout(this);
    layout->addRow(new QLabel(tr("Image : %1 × %2 pixels").arg(widthPx_).arg(heightPx_)));

    const auto makeSpin = [this] {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(1.0, 1000.0);
        spin->setDecimals(1);
        spin->setSuffix(tr(" mm"));
        return spin;
    };
    widthMm_ = makeSpin();
    heightMm_ = makeSpin();
    keepRatio_ = new QCheckBox(tr("Conserver les proportions"), this);
    keepRatio_->setChecked(true);

    // Valeur par défaut : 96 dpi (métier : libs/document).
    if (const auto def = document::placement_from_dpi(widthPx_, heightPx_, 96.0)) {
        widthMm_->setValue(to_millimeters(def->width).value);
        heightMm_->setValue(to_millimeters(def->height).value);
    }

    layout->addRow(tr("Largeur :"), widthMm_);
    layout->addRow(tr("Hauteur :"), heightMm_);
    layout->addRow(keepRatio_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    connect(widthMm_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::syncFromWidth);
    connect(heightMm_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::syncFromHeight);
    connect(keepRatio_, &QCheckBox::toggled, this, &ImportDialog::syncFromWidth);
}

void ImportDialog::syncFromWidth() {
    if (syncing_ || !keepRatio_->isChecked()) {
        return;
    }
    syncing_ = true;
    if (const auto p = document::placement_from_width(widthPx_, heightPx_,
                                                      Millimeters{widthMm_->value()})) {
        heightMm_->setValue(to_millimeters(p->height).value);
    }
    syncing_ = false;
}

void ImportDialog::syncFromHeight() {
    if (syncing_ || !keepRatio_->isChecked()) {
        return;
    }
    syncing_ = true;
    // Ratio conservé depuis la hauteur : symétrique du cas largeur.
    if (const auto p = document::placement_from_width(heightPx_, widthPx_,
                                                      Millimeters{heightMm_->value()})) {
        widthMm_->setValue(to_millimeters(p->height).value);
    }
    syncing_ = false;
}

std::optional<document::ImagePlacement> ImportDialog::placement() const {
    const auto p = document::placement_from_size(widthPx_, heightPx_,
                                                 Millimeters{widthMm_->value()},
                                                 Millimeters{heightMm_->value()});
    if (!p) {
        return std::nullopt;
    }
    return *p;
}

}  // namespace openstitch::desktop
