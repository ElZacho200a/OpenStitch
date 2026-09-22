// SPDX-License-Identifier: Apache-2.0
#include "generation_options_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace openstitch::desktop {

namespace {

QDoubleSpinBox* mmSpin(QWidget* parent, const char* name, Micrometers value, double min, double max,
                       double step) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setObjectName(QString::fromLatin1(name));
    spin->setRange(min, max);
    spin->setDecimals(2);
    spin->setSingleStep(step);
    spin->setSuffix(QStringLiteral(" mm"));
    spin->setValue(static_cast<double>(value.value) / 1000.0);
    return spin;
}

Micrometers toUm(const QDoubleSpinBox* spin) {
    return Micrometers{static_cast<std::int32_t>(std::lround(spin->value() * 1000.0))};
}

} // namespace

std::optional<document::SequenceFinishing>
editSequenceFinishing(QWidget* parent, const document::SequenceFinishing& current) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Options de génération"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* enabled = new QCheckBox(
        QObject::tr("Finitions automatiques (coupes, points d'arrêt, points courts)"), &dialog);
    enabled->setObjectName(QStringLiteral("finishingEnabled"));
    enabled->setChecked(current.enabled);
    enabled->setToolTip(QObject::tr(
        "Désactivé pour un projet créé avant cette option : la séquence reste identique."));
    layout->addWidget(enabled);

    auto* form = new QFormLayout;
    auto* trim = mmSpin(&dialog, "trimThreshold", current.trim_threshold, 0.5, 100.0, 0.5);
    trim->setToolTip(QObject::tr(
        "Un déplacement plus long devient : point d'arrêt, coupe, déplacement, point d'arrêt. "
        "Plus court : simple saut."));
    form->addRow(QObject::tr("Couper au-delà de"), trim);
    auto* trimColor = new QCheckBox(QObject::tr("Couper avant chaque changement de fil"), &dialog);
    trimColor->setObjectName(QStringLiteral("trimBeforeColorChange"));
    trimColor->setChecked(current.trim_before_color_change);
    form->addRow(QString(), trimColor);

    auto* lockType = new QComboBox(&dialog);
    lockType->setObjectName(QStringLiteral("lockType"));
    lockType->addItem(QObject::tr("Aucun"), static_cast<int>(document::LockStitch::None));
    lockType->addItem(QObject::tr("Aller-retour"),
                      static_cast<int>(document::LockStitch::BackAndForth));
    lockType->addItem(QObject::tr("Triangle"), static_cast<int>(document::LockStitch::Triangle));
    lockType->addItem(QObject::tr("Micro-zigzag"),
                      static_cast<int>(document::LockStitch::MicroZigzag));
    lockType->setCurrentIndex(lockType->findData(static_cast<int>(current.lock_type)));
    form->addRow(QObject::tr("Point d'arrêt"), lockType);
    auto* lockLength = mmSpin(&dialog, "lockLength", current.lock_length, 0.2, 3.0, 0.1);
    form->addRow(QObject::tr("Longueur du point d'arrêt"), lockLength);
    auto* lockPasses = new QSpinBox(&dialog);
    lockPasses->setObjectName(QStringLiteral("lockPasses"));
    lockPasses->setRange(1, 4);
    lockPasses->setValue(current.lock_passes);
    form->addRow(QObject::tr("Répétitions du point d'arrêt"), lockPasses);

    auto* filter = new QCheckBox(QObject::tr("Fusionner les points trop courts"), &dialog);
    filter->setObjectName(QStringLiteral("filterShortStitches"));
    filter->setChecked(current.filter_short_stitches);
    form->addRow(QString(), filter);
    auto* minStitch = mmSpin(&dialog, "minStitchLength", current.min_stitch_length, 0.1, 2.0, 0.1);
    form->addRow(QObject::tr("Longueur minimale de point"), minStitch);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    document::SequenceFinishing out = current;
    out.enabled = enabled->isChecked();
    out.trim_threshold = toUm(trim);
    out.trim_before_color_change = trimColor->isChecked();
    out.lock_type = static_cast<document::LockStitch>(lockType->currentData().toInt());
    out.lock_length = toUm(lockLength);
    out.lock_passes = lockPasses->value();
    out.filter_short_stitches = filter->isChecked();
    out.min_stitch_length = toUm(minStitch);
    return out;
}

} // namespace openstitch::desktop
