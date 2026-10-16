// SPDX-License-Identifier: Apache-2.0
#include "settings_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace openstitch::desktop {

namespace {

QDoubleSpinBox* mmSpin(double min, double max, double step, int decimals) {
    auto* box = new QDoubleSpinBox;
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    box->setSuffix(QStringLiteral(" mm"));
    return box;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Paramètres"));

    auto* tabs = new QTabWidget;

    auto* interfacePage = new QWidget;
    auto* interfaceForm = new QFormLayout(interfacePage);
    regionOpacityPercent_ = new QSpinBox;
    regionOpacityPercent_->setRange(20, 100);
    regionOpacityPercent_->setSuffix(QStringLiteral(" %"));
    regionOpacityPercent_->setToolTip(
        tr("Opacité de la carte des régions par-dessus l'image (100 % masque la photo)."));
    interfaceForm->addRow(tr("Opacité de la carte des régions"), regionOpacityPercent_);
    longPressMs_ = new QSpinBox;
    longPressMs_->setRange(300, 1000);
    longPressMs_->setSingleStep(50);
    longPressMs_->setSuffix(QStringLiteral(" ms"));
    longPressMs_->setToolTip(tr("Durée d'appui avant qu'un appui long soit reconnu."));
    interfaceForm->addRow(tr("Délai de l'appui long"), longPressMs_);
    tabs->addTab(interfacePage, tr("Interface"));

    auto* editPage = new QWidget;
    auto* editForm = new QFormLayout(editPage);
    snapNodes_ = new QCheckBox(tr("Accrocher les nœuds aux sommets des autres objets au glisser"));
    editForm->addRow(snapNodes_);
    unionGapMm_ = mmSpin(0.0, 0.5, 0.01, 3);
    unionGapMm_->setToolTip(tr("Unir soude deux formes séparées par un écart d'au plus cette "
                               "valeur (0 : union stricte). Les trous plus étroits sont comblés."));
    editForm->addRow(tr("Écart soudé par Unir"), unionGapMm_);
    knifeWidthMm_ = mmSpin(0.005, 0.2, 0.005, 3);
    knifeWidthMm_->setToolTip(tr("Largeur de la bande retirée par le Couteau."));
    editForm->addRow(tr("Largeur de la coupe du Couteau"), knifeWidthMm_);
    knifeMinLengthMm_ = mmSpin(0.1, 10.0, 0.1, 2);
    knifeMinLengthMm_->setToolTip(tr("Un trait de Couteau plus court que cette longueur est ignoré."));
    editForm->addRow(tr("Longueur minimale du trait de Couteau"), knifeMinLengthMm_);
    tabs->addTab(editPage, tr("Édition"));

    auto* note = new QLabel(tr("Ces réglages n'influencent pas les points de broderie générés. "
                               "Ils sont enregistrés dans :\n%1")
                                .arg(settingsFilePath()));
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::RestoreDefaults);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { setSettings(EditSettings::defaults()); });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(note);
    layout->addWidget(buttons);

    setSettings(EditSettings::defaults());
}

void SettingsDialog::setSettings(const EditSettings& settings) {
    const EditSettings v = settings.clamped();
    regionOpacityPercent_->setValue(static_cast<int>(std::lround(v.regionMapOpacity * 100.0)));
    longPressMs_->setValue(v.longPressMs);
    snapNodes_->setChecked(v.snapNodesOnDrag);
    unionGapMm_->setValue(v.unionGapMm);
    knifeWidthMm_->setValue(v.knifeWidthMm);
    knifeMinLengthMm_->setValue(v.knifeMinLengthMm);
}

EditSettings SettingsDialog::settings() const {
    EditSettings out;
    out.regionMapOpacity = regionOpacityPercent_->value() / 100.0;
    out.longPressMs = longPressMs_->value();
    out.snapNodesOnDrag = snapNodes_->isChecked();
    out.unionGapMm = unionGapMm_->value();
    out.knifeWidthMm = knifeWidthMm_->value();
    out.knifeMinLengthMm = knifeMinLengthMm_->value();
    return out.clamped();
}

} // namespace openstitch::desktop
