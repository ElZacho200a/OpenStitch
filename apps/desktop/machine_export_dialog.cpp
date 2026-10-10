// SPDX-License-Identifier: Apache-2.0
#include "machine_export_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace openstitch::desktop {

namespace {

constexpr const char* kSettingsFormat = "export/format";
constexpr const char* kSettingsTrims = "export/trims";
constexpr const char* kSettingsStops = "export/stops";
constexpr const char* kSettingsColors = "export/colorChanges";

} // namespace

MachineExportDialog::MachineExportDialog(SummaryFn summarize, const QString& initialFormatId,
                                         QWidget* parent)
    : QDialog(parent), summarize_(std::move(summarize)) {
    setWindowTitle(tr("Exporter une broderie machine"));
    setObjectName(QStringLiteral("dialog_machineExport"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    formatBox_ = new QComboBox(this);
    formatBox_->setObjectName(QStringLiteral("combo_exportFormat"));
    for (const auto& f : formats::registered_formats()) {
        if (!f.can_write || f.encode_ex == nullptr) {
            continue;
        }
        QString exts;
        for (const auto& e : f.extensions) {
            exts += (exts.isEmpty() ? QString() : QStringLiteral(", ")) + QStringLiteral(".") +
                    QString::fromStdString(e);
        }
        formatBox_->addItem(tr("%1 (%2)").arg(QString::fromStdString(f.display_name), exts),
                            QString::fromStdString(f.id));
    }
    form->addRow(tr("Format :"), formatBox_);

    trims_ = new QCheckBox(tr("Écrire les coupes de fil"), this);
    trims_->setObjectName(QStringLiteral("check_exportTrims"));
    stops_ = new QComboBox(this);
    stops_->setObjectName(QStringLiteral("combo_exportStops"));
    stops_->addItem(tr("Natif du format"), static_cast<int>(formats::StopMode::Native));
    stops_->addItem(tr("Comme un changement de couleur"),
                    static_cast<int>(formats::StopMode::AsColorChange));
    stops_->addItem(tr("Ignorés"), static_cast<int>(formats::StopMode::Drop));
    colorChanges_ = new QCheckBox(tr("Écrire les changements de couleur"), this);
    colorChanges_->setObjectName(QStringLiteral("check_exportColorChanges"));
    form->addRow(QString(), trims_);
    form->addRow(tr("Arrêts machine :"), stops_);
    form->addRow(QString(), colorChanges_);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("label_exportSummary"));
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(this);
    chooseBtn_ = buttons->addButton(tr("Choisir le fichier…"), QDialogButtonBox::AcceptRole);
    chooseBtn_->setDefault(true);
    problemsBtn_ = buttons->addButton(tr("Voir les problèmes"), QDialogButtonBox::ActionRole);
    problemsBtn_->setObjectName(QStringLiteral("action_exportViewProblems"));
    buttons->addButton(tr("Annuler"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(problemsBtn_, &QPushButton::clicked, this, [this] {
        viewProblems_ = true;
        reject();
    });

    // Réglages mémorisés (par poste).
    QSettings settings;
    const QString wanted = initialFormatId.isEmpty()
                               ? settings.value(kSettingsFormat, "dst").toString()
                               : initialFormatId;
    if (const int idx = formatBox_->findData(wanted); idx >= 0) {
        formatBox_->setCurrentIndex(idx);
    }
    trims_->setChecked(settings.value(kSettingsTrims, true).toBool());
    colorChanges_->setChecked(settings.value(kSettingsColors, true).toBool());
    if (const int idx = stops_->findData(settings.value(kSettingsStops, 0).toInt()); idx >= 0) {
        stops_->setCurrentIndex(idx);
    }

    const auto onChange = [this] { refresh(); };
    connect(formatBox_, &QComboBox::currentIndexChanged, this, onChange);
    connect(trims_, &QCheckBox::toggled, this, onChange);
    connect(colorChanges_, &QCheckBox::toggled, this, onChange);
    connect(stops_, &QComboBox::currentIndexChanged, this, onChange);
    connect(this, &QDialog::accepted, this, [this] {
        QSettings s;
        s.setValue(kSettingsFormat, formatBox_->currentData().toString());
        s.setValue(kSettingsTrims, trims_->isChecked());
        s.setValue(kSettingsColors, colorChanges_->isChecked());
        s.setValue(kSettingsStops, stops_->currentData().toInt());
    });
    refresh();
}

const formats::FormatInfo* MachineExportDialog::format() const {
    return formats::find_format(formatBox_->currentData().toString().toStdString());
}

formats::MachineExportOptions MachineExportDialog::options() const {
    formats::MachineExportOptions o;
    o.trims = trims_->isChecked() ? formats::TrimMode::Native : formats::TrimMode::Drop;
    o.color_changes = colorChanges_->isChecked() ? formats::ColorChangeMode::Emit
                                                 : formats::ColorChangeMode::Drop;
    o.stops = static_cast<formats::StopMode>(stops_->currentData().toInt());
    return o;
}

void MachineExportDialog::refresh() {
    const auto* f = format();
    if (f == nullptr) {
        return;
    }
    // Le DST garde son propre codage des coupes/arrêts : les options ne s'y appliquent pas.
    const bool dst = f->id == "dst";
    trims_->setEnabled(!dst);
    stops_->setEnabled(!dst);
    colorChanges_->setEnabled(!dst);
    const auto summary = summarize_(*f, options());
    summary_->setText(summary.text);
    problemsBtn_->setVisible(summary.has_analysis_errors);
    chooseBtn_->setEnabled(!summary.blocking);
}

} // namespace openstitch::desktop
