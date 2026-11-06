// SPDX-License-Identifier: Apache-2.0
#include "thread_palette_dialog.hpp"

#include <algorithm>

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "thread_palette_service.hpp"
#include "thread_plan.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
QIcon swatch(Rgb c) {
    QPixmap pix(16, 16);
    pix.fill(QColor(c[0], c[1], c[2]));
    return QIcon(pix);
}
} // namespace

ThreadPaletteDialog::ThreadPaletteDialog(DocumentSession& session, QWidget* parent)
    : QDialog(parent), session_(session) {
    setObjectName("threadPaletteDialog");
    setWindowTitle(tr("Nuanciers de fils"));
    resize(560, 640);
    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    chart_ = new QComboBox(this);
    chart_->setObjectName("paletteChart");
    for (const auto& info : chartInfos()) {
        chart_->addItem(fromUtf8(info.name), fromUtf8(info.id));
    }
    search_ = new QLineEdit(this);
    search_->setObjectName("paletteSearch");
    search_->setPlaceholderText(tr("Nom ou référence…"));
    top->addWidget(chart_, 1);
    top->addWidget(search_, 1);
    root->addLayout(top);

    threads_ = new QListWidget(this);
    threads_->setObjectName("paletteThreads");
    root->addWidget(threads_, 1);

    auto* assign = new QHBoxLayout;
    assign->addWidget(new QLabel(tr("Fil du motif :"), this));
    entry_ = new QComboBox(this);
    entry_->setObjectName("paletteEntry");
    assign->addWidget(entry_, 1);
    auto* assignButton = new QPushButton(tr("Assigner le fil choisi"), this);
    auto* nearestButton = new QPushButton(tr("Assigner le plus proche"), this);
    assign->addWidget(assignButton);
    assign->addWidget(nearestButton);
    root->addLayout(assign);

    nearest_ = new QLabel(this);
    nearest_->setObjectName("paletteNearest");
    nearest_->setWordWrap(true);
    blend_ = new QLabel(this);
    blend_->setObjectName("paletteBlend");
    blend_->setWordWrap(true);
    feedback_ = new QLabel(this);
    feedback_->setObjectName("paletteFeedback");
    feedback_->setWordWrap(true);
    root->addWidget(nearest_);
    root->addWidget(blend_);
    root->addWidget(feedback_);

    const auto entries = threadEntries(session_.project());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        entry_->addItem(swatch(e.rgb), fromUtf8(e.label) + QStringLiteral(" (") +
                                           QString::number(e.objects.size()) + QStringLiteral(")"));
    }

    connect(chart_, &QComboBox::currentIndexChanged, this, [this] {
        rebuildThreads();
        refreshSuggestions();
    });
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildThreads(); });
    connect(entry_, &QComboBox::currentIndexChanged, this, [this] { refreshSuggestions(); });
    connect(assignButton, &QPushButton::clicked, this, [this] {
        const QString refusal = assignSelected();
        feedback_->setText(refusal.isEmpty() ? tr("Fil assigné (Ctrl+Z pour annuler).") : refusal);
        refreshSuggestions();
    });
    connect(nearestButton, &QPushButton::clicked, this, [this] {
        const QString refusal = assignNearest();
        feedback_->setText(
            refusal.isEmpty() ? tr("Fil le plus proche assigné (Ctrl+Z pour annuler).") : refusal);
        refreshSuggestions();
    });
    rebuildThreads();
    refreshSuggestions();
}

void ThreadPaletteDialog::rebuildThreads() {
    threads_->clear();
    const auto chartId = chart_->currentData().toString().toStdString();
    for (const auto& t : searchThreads(chartId, search_->text().toStdString(), 400)) {
        auto* item = new QListWidgetItem(swatch(t.rgb), fromUtf8(t.code) + QStringLiteral("  ") +
                                                            fromUtf8(t.name));
        item->setData(Qt::UserRole, fromUtf8(t.code));
        threads_->addItem(item);
    }
}

void ThreadPaletteDialog::refreshSuggestions() {
    const auto entries = threadEntries(session_.project());
    const int index = entry_->currentIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size()) {
        nearest_->setText(tr("Aucun fil dans le motif."));
        blend_->clear();
        return;
    }
    const auto chartId = chart_->currentData().toString().toStdString();
    const auto rgb = entries[static_cast<std::size_t>(index)].rgb;
    QStringList lines;
    for (const auto& t : nearestThreads(rgb, chartId, 4)) {
        lines << QStringLiteral("%1 %2 (écart %3)")
                     .arg(fromUtf8(t.code), fromUtf8(t.name))
                     .arg(t.distance, 0, 'f', 1);
    }
    nearest_->setText(tr("Plus proches : ") + lines.join(QStringLiteral(" ; ")));
    if (const auto blend = suggestBlend(rgb, chartId)) {
        blend_->setText(tr("Mélange : ") + fromUtf8(blend->summary()));
    } else {
        blend_->setText(tr("Pas assez de fils dans ce nuancier pour proposer un mélange."));
    }
}

void ThreadPaletteDialog::setChart(const QString& chartId) {
    chart_->setCurrentIndex(std::max(0, chart_->findData(chartId)));
}

void ThreadPaletteDialog::setSearch(const QString& text) {
    search_->setText(text);
}

void ThreadPaletteDialog::selectEntry(int index) {
    entry_->setCurrentIndex(index);
}

void ThreadPaletteDialog::selectThreadRow(int row) {
    threads_->setCurrentRow(row);
}

QString ThreadPaletteDialog::assignSelected() {
    const auto* item = threads_->currentItem();
    if (item == nullptr) {
        return tr("Choisissez d'abord un fil dans la liste.");
    }
    const auto r = assignChartThread(session_, static_cast<std::size_t>(entry_->currentIndex()),
                                     chart_->currentData().toString().toStdString(),
                                     item->data(Qt::UserRole).toString().toStdString());
    return r.ok ? QString() : fromUtf8(r.message);
}

QString ThreadPaletteDialog::assignNearest() {
    const auto entries = threadEntries(session_.project());
    const int index = entry_->currentIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= entries.size()) {
        return tr("Aucun fil dans le motif.");
    }
    const auto chartId = chart_->currentData().toString().toStdString();
    const auto near = nearestThreads(entries[static_cast<std::size_t>(index)].rgb, chartId, 1);
    if (near.empty()) {
        return tr("Ce nuancier est vide.");
    }
    const auto r =
        assignChartThread(session_, static_cast<std::size_t>(index), chartId, near.front().code);
    return r.ok ? QString() : fromUtf8(r.message);
}

int ThreadPaletteDialog::chartCount() const {
    return chart_->count();
}

int ThreadPaletteDialog::threadRowCount() const {
    return threads_->count();
}

QString ThreadPaletteDialog::nearestText() const {
    return nearest_->text();
}

QString ThreadPaletteDialog::blendText() const {
    return blend_->text();
}

} // namespace openstitch::desktop_v2
