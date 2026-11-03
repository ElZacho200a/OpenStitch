// SPDX-License-Identifier: Apache-2.0
#include "machine_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

#include "openstitch/document/machine_profile.hpp"

namespace openstitch::desktop {

namespace {

QString fromView(std::string_view v) {
    return QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()));
}

} // namespace

MachineDialog::MachineDialog(const QString& currentMachineId, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Machine de broderie"));
    setObjectName(QStringLiteral("machineDialog"));
    auto* layout = new QFormLayout(this);
    layout->addRow(new QLabel(tr("Profil générique de la machine : sert à avertir avant l'export "
                                 "(champ, cadres, aiguilles, formats, points trop longs). Le "
                                 "motif n'est jamais modifié."),
                              this));

    machineCombo_ = new QComboBox(this);
    machineCombo_->setObjectName(QStringLiteral("machineCombo"));
    machineCombo_->addItem(tr("Aucune machine"), QString());
    for (const auto& m : document::machine_profiles()) {
        machineCombo_->addItem(fromView(m.name), fromView(m.id));
    }
    const int index = machineCombo_->findData(currentMachineId);
    machineCombo_->setCurrentIndex(index >= 0 ? index : 0);
    layout->addRow(tr("Machine :"), machineCombo_);

    details_ = new QLabel(this);
    details_->setObjectName(QStringLiteral("machineDetailsLabel"));
    details_->setWordWrap(true);
    layout->addRow(details_);

    hoopCombo_ = new QComboBox(this);
    hoopCombo_->setObjectName(QStringLiteral("machineHoopCombo"));
    hoopCombo_->setToolTip(tr("Cadres disponibles pour cette machine : le choisir remplace le "
                              "cadre du projet (taille et forme ; la marge est conservée)."));
    layout->addRow(tr("Cadre :"), hoopCombo_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    connect(machineCombo_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    refresh();
}

QString MachineDialog::machineId() const {
    return machineCombo_->currentData().toString();
}

QString MachineDialog::hoopId() const {
    return hoopCombo_->currentData().toString();
}

QString MachineDialog::describe(const QString& machineId) {
    const auto* m = document::find_machine(machineId.toStdString());
    if (m == nullptr) {
        return tr("Aucune vérification liée à une machine.");
    }
    QStringList formats;
    for (const std::string_view f : m->format_ids()) {
        formats << fromView(f).toUpper();
    }
    return tr("Champ maximal : %1 × %2 mm\nAiguilles : %3\nFormats : %4 (préféré : %5)\n"
              "Point maximal : %6 mm\nCoupe-fil automatique : %7\nVitesse : %8 points/min")
        .arg(m->max_field_width_mm)
        .arg(m->max_field_height_mm)
        .arg(m->needles)
        .arg(formats.join(QStringLiteral(", ")))
        .arg(fromView(m->preferred_format).toUpper())
        .arg(m->max_stitch_length_mm)
        .arg(m->auto_trim ? tr("oui") : tr("non"))
        .arg(m->max_speed_spm);
}

void MachineDialog::refresh() {
    const QString id = machineId();
    details_->setText(describe(id));
    hoopCombo_->clear();
    hoopCombo_->addItem(tr("Garder le cadre actuel"), QString());
    if (const auto* m = document::find_machine(id.toStdString())) {
        for (const std::string_view hoopId : m->hoops()) {
            if (const auto* h = document::find_hoop(hoopId)) {
                hoopCombo_->addItem(fromView(h->name), fromView(h->id));
            }
        }
    }
    hoopCombo_->setEnabled(hoopCombo_->count() > 1);
}

} // namespace openstitch::desktop
