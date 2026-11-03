// SPDX-License-Identifier: Apache-2.0
#include "general_preferences_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace openstitch::desktop {

namespace {
constexpr double kGridMinMm = 0.1;
constexpr double kGridMaxMm = 100.0;
constexpr int kAutosaveMin = 1;
constexpr int kAutosaveMax = 30;
} // namespace

GeneralPreferences GeneralPreferences::clamped() const {
    GeneralPreferences out = *this;
    out.gridStepMm = std::clamp(gridStepMm, kGridMinMm, kGridMaxMm);
    out.autosaveMinutes = std::clamp(autosaveMinutes, kAutosaveMin, kAutosaveMax);
    return out;
}

GeneralPreferences loadGeneralPreferences() {
    QSettings settings;
    GeneralPreferences prefs;
    prefs.unit = length_units::current();
    // Clé historique du pas de grille (HP-VEC-009) : une seule source.
    prefs.gridStepMm = settings.value(QStringLiteral("edit/gridStepMm"), 1.0).toDouble();
    prefs.autosaveMinutes = settings.value(QStringLiteral("prefs/autosaveMinutes"), 2).toInt();
    prefs.workingDirectory = settings.value(QStringLiteral("prefs/workingDirectory")).toString();
    return prefs.clamped();
}

void saveGeneralPreferences(const GeneralPreferences& raw) {
    const GeneralPreferences prefs = raw.clamped();
    QSettings settings;
    length_units::setCurrent(prefs.unit);
    settings.setValue(QStringLiteral("edit/gridStepMm"), prefs.gridStepMm);
    settings.setValue(QStringLiteral("prefs/autosaveMinutes"), prefs.autosaveMinutes);
    settings.setValue(QStringLiteral("prefs/workingDirectory"), prefs.workingDirectory);
}

GeneralPreferencesDialog::GeneralPreferencesDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("generalPreferencesDialog"));
    setWindowTitle(tr("Préférences générales"));

    unit_ = new QComboBox;
    unit_->setObjectName(QStringLiteral("prefsUnit"));
    unit_->addItem(tr("Millimètres (mm)"), static_cast<int>(length_units::Unit::Millimeters));
    unit_->addItem(tr("Pouces (in)"), static_cast<int>(length_units::Unit::Inches));
    unit_->setToolTip(tr("Unité d'affichage et de saisie des longueurs. Le document reste "
                         "enregistré en micromètres : changer d'unité ne modifie aucun point."));

    gridStep_ = new QDoubleSpinBox;
    gridStep_->setObjectName(QStringLiteral("prefsGridStep"));

    autosave_ = new QSpinBox;
    autosave_->setObjectName(QStringLiteral("prefsAutosave"));
    autosave_->setRange(kAutosaveMin, kAutosaveMax);
    autosave_->setSuffix(tr(" min"));

    workDir_ = new QLineEdit;
    workDir_->setObjectName(QStringLiteral("prefsWorkDir"));
    workDir_->setPlaceholderText(tr("Automatique (dernier dossier utilisé)"));
    auto* browse = new QPushButton(tr("Parcourir…"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, tr("Dossier de travail"), workDir_->text());
        if (!dir.isEmpty()) {
            workDir_->setText(dir);
        }
    });
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(workDir_, 1);
    dirRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow(tr("Unités :"), unit_);
    form->addRow(tr("Pas de la grille :"), gridStep_);
    form->addRow(tr("Sauvegarde automatique toutes les :"), autosave_);
    form->addRow(tr("Dossier de travail :"), dirRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(unit_, &QComboBox::currentIndexChanged, this, [this] { onUnitChanged(); });
    connect(gridStep_, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { gridMm_ = length_units::toMm(value, shownUnit_); });
    setPreferences(GeneralPreferences{});
}

length_units::Unit GeneralPreferencesDialog::selectedUnit() const {
    return unit_->currentData().toInt() == 1 ? length_units::Unit::Inches
                                             : length_units::Unit::Millimeters;
}

void GeneralPreferencesDialog::onUnitChanged() {
    // Le pas de grille est conservé en mm : on le reconvertit dans la nouvelle unité affichée.
    const double mm = gridMm_;
    shownUnit_ = selectedUnit();
    const QSignalBlocker block(gridStep_);
    gridStep_->setDecimals(shownUnit_ == length_units::Unit::Inches ? 3 : 2);
    gridStep_->setRange(length_units::fromMm(kGridMinMm, shownUnit_),
                        length_units::fromMm(kGridMaxMm, shownUnit_));
    gridStep_->setSingleStep(length_units::fromMm(0.5, shownUnit_));
    gridStep_->setSuffix(QLatin1Char(' ') + length_units::suffix(shownUnit_));
    gridStep_->setValue(length_units::fromMm(mm, shownUnit_));
}

void GeneralPreferencesDialog::setPreferences(const GeneralPreferences& raw) {
    const GeneralPreferences prefs = raw.clamped();
    {
        const QSignalBlocker block(unit_);
        unit_->setCurrentIndex(unit_->findData(static_cast<int>(prefs.unit)));
    }
    gridMm_ = prefs.gridStepMm;
    onUnitChanged();
    autosave_->setValue(prefs.autosaveMinutes);
    workDir_->setText(prefs.workingDirectory);
}

GeneralPreferences GeneralPreferencesDialog::preferences() const {
    GeneralPreferences prefs;
    prefs.unit = selectedUnit();
    prefs.gridStepMm = gridMm_;
    prefs.autosaveMinutes = autosave_->value();
    prefs.workingDirectory = workDir_->text().trimmed();
    return prefs.clamped();
}

} // namespace openstitch::desktop
