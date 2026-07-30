// SPDX-License-Identifier: Apache-2.0
#include "import_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

#include "app_theme.hpp"

namespace openstitch::desktop {

ImportDialog::ImportDialog(int widthPx, int heightPx, const QImage& preview, QSizeF hoopMm,
                           QWidget* parent)
    : QDialog(parent), widthPx_(widthPx), heightPx_(heightPx), hoopMm_(hoopMm) {
    setWindowTitle(tr("Importer une image"));

    auto* root = new QVBoxLayout(this);

    // Aperçu + informations pixel.
    auto* preview_label = new QLabel(this);
    preview_label->setAlignment(Qt::AlignCenter);
    if (!preview.isNull()) {
        preview_label->setPixmap(QPixmap::fromImage(preview).scaled(
            220, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    root->addWidget(preview_label);

    QString info = tr("%1 × %2 pixels").arg(widthPx_).arg(heightPx_);
    if (preview.hasAlphaChannel()) {
        info += tr("  ·  transparence");
    }
    auto* infoLabel = new QLabel(info, this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setEnabled(false);
    root->addWidget(infoLabel);

    auto* form = new QFormLayout();
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

    form->addRow(tr("Largeur :"), widthMm_);
    form->addRow(tr("Hauteur :"), heightMm_);
    form->addRow(keepRatio_);
    root->addLayout(form);

    resolutionLabel_ = new QLabel(this);
    resolutionLabel_->setEnabled(false);
    root->addWidget(resolutionLabel_);

    warningLabel_ = new QLabel(this);
    warningLabel_->setWordWrap(true);
    warningLabel_->setStyleSheet(
        QStringLiteral("color:%1;").arg(AppTheme::instance().tokens().warning.name()));
    root->addWidget(warningLabel_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(widthMm_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::syncFromWidth);
    connect(heightMm_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::syncFromHeight);
    connect(keepRatio_, &QCheckBox::toggled, this, &ImportDialog::syncFromWidth);
    recompute();
}

void ImportDialog::syncFromWidth() {
    if (!syncing_ && keepRatio_->isChecked()) {
        syncing_ = true;
        if (const auto p = document::placement_from_width(widthPx_, heightPx_,
                                                          Millimeters{widthMm_->value()})) {
            heightMm_->setValue(to_millimeters(p->height).value);
        }
        syncing_ = false;
    }
    recompute();
}

void ImportDialog::syncFromHeight() {
    if (!syncing_ && keepRatio_->isChecked()) {
        syncing_ = true;
        // Ratio conservé depuis la hauteur : symétrique du cas largeur.
        if (const auto p = document::placement_from_width(heightPx_, widthPx_,
                                                          Millimeters{heightMm_->value()})) {
            widthMm_->setValue(to_millimeters(p->height).value);
        }
        syncing_ = false;
    }
    recompute();
}

void ImportDialog::recompute() {
    const double wMm = widthMm_->value();
    const double hMm = heightMm_->value();
    const double mmPerPx = widthPx_ > 0 ? wMm / widthPx_ : 0.0;
    resolutionLabel_->setText(
        tr("Résolution : %1 mm/pixel   ·   cadre %2 × %3 mm")
            .arg(mmPerPx, 0, 'f', 3)
            .arg(hoopMm_.width(), 0, 'f', 0)
            .arg(hoopMm_.height(), 0, 'f', 0));

    if (wMm > hoopMm_.width() + 1e-6 || hMm > hoopMm_.height() + 1e-6) {
        warningLabel_->setText(
            tr("L'image dépasse le cadre (%1 × %2 mm > %3 × %4 mm). Vous pourrez la "
               "recadrer ou agrandir le cadre.")
                .arg(wMm, 0, 'f', 1)
                .arg(hMm, 0, 'f', 1)
                .arg(hoopMm_.width(), 0, 'f', 0)
                .arg(hoopMm_.height(), 0, 'f', 0));
        warningLabel_->show();
    } else {
        warningLabel_->clear();
        warningLabel_->hide();
    }
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
