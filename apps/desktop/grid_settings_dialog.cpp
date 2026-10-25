// SPDX-License-Identifier: Apache-2.0
#include "grid_settings_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace openstitch::desktop {

GridSettingsDialog::GridSettingsDialog(const stitch_render::GridSettings& initial, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("dialog_gridSettings"));
    setWindowTitle(tr("Réglages de la grille"));
    const stitch_render::GridSettings s = stitch_render::sanitized(initial);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    root->addLayout(form);

    visible_ = new QCheckBox(tr("Afficher la grille"), this);
    visible_->setObjectName(QStringLiteral("check_gridVisible"));
    visible_->setChecked(s.visible);
    form->addRow(visible_);

    auto_ = new QCheckBox(tr("Pas automatique (adapté au zoom)"), this);
    auto_->setObjectName(QStringLiteral("check_gridAuto"));
    auto_->setChecked(s.step_mm <= 0.0);
    form->addRow(auto_);

    step_ = new QDoubleSpinBox(this);
    step_->setObjectName(QStringLiteral("spin_gridStep"));
    step_->setRange(stitch_render::kGridStepMinMm, stitch_render::kGridStepMaxMm);
    step_->setDecimals(2);
    step_->setSingleStep(0.5);
    step_->setSuffix(tr(" mm"));
    step_->setValue(s.step_mm > 0.0 ? s.step_mm : 1.0);
    form->addRow(tr("Pas de la grille :"), step_);

    major_ = new QSpinBox(this);
    major_->setObjectName(QStringLiteral("spin_gridMajor"));
    major_->setRange(1, 50);
    major_->setValue(s.major_every);
    major_->setToolTip(tr("Une ligne plus marquée tous les N pas (1 = pas de distinction)."));
    form->addRow(tr("Ligne majeure tous les :"), major_);

    themeColor_ = new QCheckBox(tr("Couleur du thème"), this);
    themeColor_->setObjectName(QStringLiteral("check_gridThemeColor"));
    themeColor_->setChecked(s.rgba[3] == 0);
    form->addRow(themeColor_);

    color_ =
        s.rgba[3] == 0 ? QColor(0, 0, 0, 80) : QColor(s.rgba[0], s.rgba[1], s.rgba[2], s.rgba[3]);
    colorButton_ = new QPushButton(this);
    colorButton_->setObjectName(QStringLiteral("button_gridColor"));
    form->addRow(tr("Couleur :"), colorButton_);
    connect(colorButton_, &QPushButton::clicked, this, [this] {
        const QColor picked = QColorDialog::getColor(color_, this, tr("Couleur de la grille"),
                                                     QColorDialog::ShowAlphaChannel);
        if (picked.isValid()) {
            setColor(picked);
        }
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(auto_, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    connect(themeColor_, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    updateEnabled();
    updateColorButton();
}

void GridSettingsDialog::updateEnabled() {
    step_->setEnabled(!auto_->isChecked());
    major_->setEnabled(!auto_->isChecked());
    colorButton_->setEnabled(!themeColor_->isChecked());
}

void GridSettingsDialog::updateColorButton() {
    colorButton_->setText(color_.name(QColor::HexArgb));
    colorButton_->setStyleSheet(
        QStringLiteral("QPushButton { border-left: 14px solid %1; }").arg(color_.name()));
}

void GridSettingsDialog::setColor(const QColor& color) {
    if (!color.isValid()) {
        return;
    }
    color_ = color;
    themeColor_->setChecked(false);
    updateColorButton();
}

stitch_render::GridSettings GridSettingsDialog::settings() const {
    stitch_render::GridSettings out;
    out.visible = visible_->isChecked();
    out.step_mm = auto_->isChecked() ? 0.0 : step_->value();
    out.major_every = auto_->isChecked() ? 5 : major_->value();
    if (!themeColor_->isChecked()) {
        const int alpha = std::max(1, color_.alpha());
        out.rgba = {static_cast<std::uint8_t>(color_.red()),
                    static_cast<std::uint8_t>(color_.green()),
                    static_cast<std::uint8_t>(color_.blue()), static_cast<std::uint8_t>(alpha)};
    }
    return stitch_render::sanitized(out);
}

} // namespace openstitch::desktop
