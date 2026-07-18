// SPDX-License-Identifier: Apache-2.0
#include "brightness_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>

namespace openstitch::desktop {

BrightnessDialog::BrightnessDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Luminosité et contraste"));

    const auto makeSlider = [this] {
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setMinimumWidth(240);
        return slider;
    };
    brightness_ = makeSlider();
    contrast_ = makeSlider();

    auto* layout = new QFormLayout(this);
    layout->addRow(tr("Luminosité :"), brightness_);
    layout->addRow(tr("Contraste :"), contrast_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    const auto emitPreview = [this] {
        emit previewRequested(brightness(), contrast());
    };
    connect(brightness_, &QSlider::valueChanged, this, emitPreview);
    connect(contrast_, &QSlider::valueChanged, this, emitPreview);
}

double BrightnessDialog::brightness() const {
    return static_cast<double>(brightness_->value());
}

double BrightnessDialog::contrast() const {
    return static_cast<double>(contrast_->value());
}

}  // namespace openstitch::desktop
