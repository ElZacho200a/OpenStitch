// SPDX-License-Identifier: Apache-2.0
#include "diagnostics_panel.hpp"

#include <QBrush>
#include <QColor>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent)
    : QWidget(parent), summary_(new QLabel(this)), run_(new QPushButton(tr("Analyser"), this)),
      list_(new QListWidget(this)) {
    setObjectName(QStringLiteral("diagnosticsPanel"));
    summary_->setObjectName(QStringLiteral("diagnosticsSummary"));
    summary_->setWordWrap(true);
    run_->setObjectName(QStringLiteral("diagnosticsRun"));
    list_->setObjectName(QStringLiteral("diagnosticsList"));
    list_->setAccessibleName(tr("Constats de l'analyse"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(run_);
    layout->addWidget(summary_);
    layout->addWidget(list_, 1);
    connect(run_, &QPushButton::clicked, this, &DiagnosticsPanel::analyseRequested);
    list_->setWordWrap(true); // un constat long se lit en entier, sans défilement horizontal
    list_->setTextElideMode(Qt::ElideNone);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setResizeMode(QListView::Adjust);
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { activateRow(list_->currentRow()); });
    clear();
}

void DiagnosticsPanel::clear() {
    hasResult_ = false;
    stale_ = false;
    result_ = {};
    list_->clear();
    summary_->setText(tr("Aucune analyse. « Analyser » contrôle la brodabilité du document."));
}

void DiagnosticsPanel::setBusy(bool busy) {
    run_->setEnabled(!busy);
    run_->setText(busy ? tr("Analyse en cours…") : tr("Analyser"));
}

void DiagnosticsPanel::setResult(const DiagnosticsResult& result) {
    result_ = result;
    hasResult_ = true;
    stale_ = false;
    list_->clear();
    for (const auto& f : result.findings) {
        const bool error = f.severity == "error";
        const bool warning = f.severity == "warning";
        const QString tag = error ? tr("Erreur") : (warning ? tr("Avertissement") : tr("Info"));
        auto* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(tag, qs(f.message)), list_);
        item->setForeground(QBrush(error ? QColor(0xe5, 0x39, 0x35)
                                         : (warning ? QColor(0xf9, 0xa8, 0x25) : QColor())));
        QString tip = qs(f.suggestion);
        if (!f.objects.empty()) {
            tip += tr("\n%1 objet(s) concerné(s) — double-clic pour les sélectionner.")
                       .arg(f.objects.size());
        }
        item->setToolTip(tip);
    }
    setStale(false);
}

void DiagnosticsPanel::setStale(bool stale) {
    stale_ = stale;
    if (!hasResult_) {
        return;
    }
    QString text =
        tr("%1 erreur(s), %2 avertissement(s), %3 info(s) — %4 points, %5 sauts, %6 coupes, "
           "%7 changements de couleur, %8 m de fil, motif %9 × %10 mm")
            .arg(result_.errors)
            .arg(result_.warnings)
            .arg(result_.infos)
            .arg(result_.stitches)
            .arg(result_.jumps)
            .arg(result_.trims)
            .arg(result_.colorChanges)
            .arg(result_.threadM, 0, 'f', 1)
            .arg(result_.motifWidthMm, 0, 'f', 1)
            .arg(result_.motifHeightMm, 0, 'f', 1);
    if (stale) {
        text += tr("\nLe document a changé depuis cette analyse : relancez-la.");
    }
    summary_->setText(text);
}

int DiagnosticsPanel::findingCount() const {
    return list_->count();
}

void DiagnosticsPanel::activateRow(int row) {
    if (row < 0 || row >= static_cast<int>(result_.findings.size())) {
        return;
    }
    emit findingActivated(result_.findings[static_cast<std::size_t>(row)]);
}

} // namespace openstitch::desktop_v2
