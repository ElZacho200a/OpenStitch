// SPDX-License-Identifier: Apache-2.0
#include "thread_merge_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace openstitch::desktop {

namespace {

QIcon swatch(std::array<std::uint8_t, 3> rgb) {
    QPixmap pm(14, 14);
    pm.fill(QColor(rgb[0], rgb[1], rgb[2]));
    return QIcon(pm);
}

QString label(const stitch_analysis::ThreadUsage& u) {
    return QString::fromStdString(stitch_analysis::thread_label(u));
}

} // namespace

ThreadMergeDialog::ThreadMergeDialog(std::vector<stitch_analysis::ThreadUsage> usage,
                                     QWidget* parent)
    : QDialog(parent), usage_(std::move(usage)) {
    setObjectName(QStringLiteral("dialog_thread_merge"));
    setWindowTitle(tr("Réduire / fusionner les fils"));
    auto* root = new QVBoxLayout(this);

    reduceMode_ = new QRadioButton(tr("Réduire à un nombre de fils"), this);
    reduceMode_->setObjectName(QStringLiteral("radio_merge_reduce"));
    reduceMode_->setChecked(true);
    countSpin_ = new QSpinBox(this);
    countSpin_->setObjectName(QStringLiteral("spin_merge_count"));
    const int total = static_cast<int>(usage_.size());
    countSpin_->setRange(1, std::max(1, total));
    countSpin_->setValue(std::max(1, total - 1));
    manualMode_ = new QRadioButton(tr("Fusionner deux fils choisis"), this);
    manualMode_->setObjectName(QStringLiteral("radio_merge_manual"));
    fromCombo_ = new QComboBox(this);
    fromCombo_->setObjectName(QStringLiteral("combo_merge_from"));
    toCombo_ = new QComboBox(this);
    toCombo_->setObjectName(QStringLiteral("combo_merge_to"));
    for (const auto& u : usage_) {
        fromCombo_->addItem(swatch(u.identity.rgb), label(u));
        toCombo_->addItem(swatch(u.identity.rgb), label(u));
    }
    if (usage_.size() > 1) {
        toCombo_->setCurrentIndex(1);
    }

    auto* form = new QFormLayout();
    form->addRow(reduceMode_, countSpin_);
    form->addRow(manualMode_);
    form->addRow(tr("Remplacer le fil"), fromCombo_);
    form->addRow(tr("par le fil"), toCombo_);
    root->addLayout(form);

    root->addWidget(new QLabel(tr("Aperçu (rien n'est modifié avant « Appliquer ») :"), this));
    preview_ = new QTableWidget(this);
    preview_->setObjectName(QStringLiteral("table_merge_preview"));
    preview_->setColumnCount(3);
    preview_->setHorizontalHeaderLabels({tr("Fil"), tr("Points"), tr("Devient")});
    preview_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_->setSelectionMode(QAbstractItemView::NoSelection);
    preview_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(preview_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    applyButton_ = buttons->addButton(tr("Appliquer"), QDialogButtonBox::AcceptRole);
    applyButton_->setObjectName(QStringLiteral("btn_merge_apply"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto onChange = [this] { recompute(); };
    connect(reduceMode_, &QRadioButton::toggled, this, onChange);
    connect(countSpin_, &QSpinBox::valueChanged, this, onChange);
    connect(fromCombo_, &QComboBox::currentIndexChanged, this, onChange);
    connect(toCombo_, &QComboBox::currentIndexChanged, this, onChange);
    recompute();
}

void ThreadMergeDialog::recompute() {
    const bool reduce = reduceMode_->isChecked();
    countSpin_->setEnabled(reduce);
    fromCombo_->setEnabled(!reduce);
    toCombo_->setEnabled(!reduce);
    if (reduce) {
        plan_ = stitch_analysis::plan_reduce_threads(usage_,
                                                     static_cast<std::size_t>(countSpin_->value()));
    } else {
        plan_ = stitch_analysis::plan_merge_threads(
            usage_, static_cast<std::size_t>(std::max(0, fromCombo_->currentIndex())),
            static_cast<std::size_t>(std::max(0, toCombo_->currentIndex())));
    }
    applyButton_->setEnabled(!plan_.empty());
    fillPreview();
}

void ThreadMergeDialog::fillPreview() {
    preview_->setRowCount(static_cast<int>(usage_.size()));
    for (std::size_t i = 0; i < usage_.size(); ++i) {
        const int row = static_cast<int>(i);
        auto* name = new QTableWidgetItem(swatch(usage_[i].identity.rgb), label(usage_[i]));
        auto* stitches = new QTableWidgetItem(QString::number(usage_[i].stitch_count));
        const std::size_t to = i < plan_.target.size() ? plan_.target[i] : i;
        auto* becomes =
            to == i ? new QTableWidgetItem(tr("conservé"))
                    : new QTableWidgetItem(swatch(usage_[to].identity.rgb), label(usage_[to]));
        preview_->setItem(row, 0, name);
        preview_->setItem(row, 1, stitches);
        preview_->setItem(row, 2, becomes);
    }
    preview_->resizeColumnToContents(0);
}

} // namespace openstitch::desktop
