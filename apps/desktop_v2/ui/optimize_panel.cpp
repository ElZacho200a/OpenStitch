// SPDX-License-Identifier: Apache-2.0
#include "optimize_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

OptimizePanel::OptimizePanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("optimizePanel"));
    auto* column = new QVBoxLayout(this);
    column->addWidget(new QLabel(tr("<b>Optimiser l'ordre de couture</b>"), this));

    strategy_ = new QComboBox(this);
    strategy_->setObjectName(QStringLiteral("optimizeStrategy"));
    for (const auto s : {PathStrategy::ColorThenProximity, PathStrategy::ByColor,
                         PathStrategy::ByProximity, PathStrategy::Layered}) {
        strategy_->addItem(fromUtf8(pathStrategyLabel(s)), static_cast<int>(s));
    }
    column->addWidget(strategy_);

    improve_ = new QCheckBox(tr("Affiner le trajet (2-opt)"), this);
    improve_->setObjectName(QStringLiteral("optimizeImprove"));
    improve_->setChecked(true);
    column->addWidget(improve_);

    propose_ = new QPushButton(tr("Calculer une proposition"), this);
    propose_->setObjectName(QStringLiteral("optimizePropose"));
    column->addWidget(propose_);

    verdict_ = new QLabel(tr("Aucune proposition. Le document n'est pas modifié."), this);
    verdict_->setObjectName(QStringLiteral("optimizeVerdict"));
    verdict_->setWordWrap(true);
    column->addWidget(verdict_);

    metrics_ = new QListWidget(this);
    metrics_->setObjectName(QStringLiteral("optimizeMetrics"));
    metrics_->setMaximumHeight(140);
    column->addWidget(metrics_);

    auto* row = new QHBoxLayout;
    accept_ = new QPushButton(tr("Accepter"), this);
    accept_->setObjectName(QStringLiteral("optimizeAccept"));
    refuse_ = new QPushButton(tr("Refuser"), this);
    refuse_->setObjectName(QStringLiteral("optimizeRefuse"));
    row->addWidget(accept_);
    row->addWidget(refuse_);
    column->addLayout(row);
    column->addStretch(1);

    connect(propose_, &QPushButton::clicked, this,
            [this] { emit proposeRequested(strategy(), improve()); });
    connect(accept_, &QPushButton::clicked, this, &OptimizePanel::acceptRequested);
    connect(refuse_, &QPushButton::clicked, this, &OptimizePanel::refuseRequested);
    updateButtons();
}

PathStrategy OptimizePanel::strategy() const {
    return static_cast<PathStrategy>(strategy_->currentData().toInt());
}

bool OptimizePanel::improve() const {
    return improve_->isChecked();
}

void OptimizePanel::chooseStrategy(PathStrategy strategy) {
    const int index = strategy_->findData(static_cast<int>(strategy));
    if (index >= 0) {
        strategy_->setCurrentIndex(index);
    }
}

void OptimizePanel::setBusy(bool busy) {
    busy_ = busy;
    if (busy) {
        verdict_->setText(tr("Calcul en cours…"));
    }
    updateButtons();
}

void OptimizePanel::setEnabledForDocument(bool hasEnoughObjects) {
    enabled_ = hasEnoughObjects;
    updateButtons();
}

void OptimizePanel::showProposal(const PathProposal& proposal) {
    hasProposal_ = true;
    changes_ = proposal.changes;
    stale_ = false;
    busy_ = false;
    metrics_->clear();
    for (const auto& line : proposal.summary()) {
        metrics_->addItem(fromUtf8(line));
    }
    if (!proposal.changes) {
        verdict_->setText(tr("<b>Cet ordre est déjà optimal</b> pour cette stratégie : rien à "
                             "changer."));
    } else if (proposal.improves()) {
        verdict_->setText(tr("<b>Amélioration mesurée</b> sur la séquence réelle. Acceptez pour "
                             "l'appliquer (annulable en une étape)."));
    } else {
        verdict_->setText(tr("<b>Compromis :</b> un indicateur s'améliore, un autre se dégrade. "
                             "Comparez les mesures avant de décider."));
    }
    updateButtons();
}

void OptimizePanel::showMessage(const QString& message) {
    hasProposal_ = false;
    changes_ = false;
    busy_ = false;
    metrics_->clear();
    verdict_->setText(message);
    updateButtons();
}

void OptimizePanel::setStale(bool stale) {
    if (!hasProposal_ || stale_ == stale) {
        return;
    }
    stale_ = stale;
    if (stale) {
        verdict_->setText(tr("<b>Proposition périmée :</b> le document a changé. Relancez le "
                             "calcul."));
    }
    updateButtons();
}

void OptimizePanel::clearProposal() {
    showMessage(tr("Aucune proposition. Le document n'est pas modifié."));
}

bool OptimizePanel::canAccept() const {
    return accept_->isEnabled();
}

int OptimizePanel::metricCount() const {
    return metrics_->count();
}

QString OptimizePanel::metricText(int row) const {
    return row >= 0 && row < metrics_->count() ? metrics_->item(row)->text() : QString();
}

QString OptimizePanel::verdict() const {
    return verdict_->text();
}

void OptimizePanel::updateButtons() {
    propose_->setEnabled(enabled_ && !busy_);
    accept_->setEnabled(hasProposal_ && changes_ && !stale_ && !busy_);
    refuse_->setEnabled(hasProposal_ && !busy_);
}

} // namespace openstitch::desktop_v2
