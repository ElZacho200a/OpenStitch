// SPDX-License-Identifier: Apache-2.0
#include "calque_panel.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "document_services.hpp"
#include "inspector_model.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

CalquePanel::CalquePanel(DocumentSession& session, QWidget* parent)
    : QWidget(parent), session_(session), list_(new QListWidget(this)) {
    setObjectName(QStringLiteral("calquePanel"));
    list_->setObjectName(QStringLiteral("calqueList"));
    list_->setAccessibleName(tr("Calques, du fond vers le dessus"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(list_, 1);
    auto* row1 = new QHBoxLayout;
    auto* row2 = new QHBoxLayout;
    const auto button = [this](const QString& text, const char* name, auto&& action) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        connect(b, &QPushButton::clicked, this, action);
        return b;
    };
    row1->addWidget(button(tr("Ajouter"), "calqueAdd", [this] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("Nouveau calque"), tr("Nom du calque :"),
                                  QLineEdit::Normal, tr("Calque"), &ok);
        if (ok) {
            addNamed(name);
        }
    }));
    row1->addWidget(button(tr("Renommer"), "calqueRename", [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Renommer le calque"), tr("Nom :"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok) {
            renameCurrent(name);
        }
    }));
    row1->addWidget(button(tr("Supprimer"), "calqueRemove", [this] { removeCurrent(); }));
    row2->addWidget(button(tr("▲"), "calqueUp", [this] { moveCurrent(+1); }));
    row2->addWidget(button(tr("▼"), "calqueDown", [this] { moveCurrent(-1); }));
    row2->addWidget(button(tr("Verrou"), "calqueLock", [this] { toggleLockCurrent(); }));
    row2->addWidget(button(tr("Sélection ici"), "calqueHere", [this] { moveSelectionHere(); }));
    layout->addLayout(row1);
    layout->addLayout(row2);

    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (refreshing_ || item == nullptr) {
            return;
        }
        const auto id = item->data(kIdRole).toULongLong();
        const bool visible = item->checkState() == Qt::Checked;
        const auto r = setCalqueVisible(session_, id, visible);
        if (!r.ok) {
            emit refused(qs(r.message));
        }
    });
    sessionToken_ = session_.subscribe([this](ChangeKind) { refresh(); });
    refresh();
}

CalquePanel::~CalquePanel() {
    session_.unsubscribe(sessionToken_);
}

void CalquePanel::refresh() {
    refreshing_ = true;
    const int keep = list_->currentRow();
    list_->clear();
    const auto all = calques(session_.project());
    // Du dessus vers le fond à l'écran (comme les palettes de calques), mais l'ordre du document
    // reste du fond au dessus : la rangée 0 est le DESSUS.
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        auto* item =
            new QListWidgetItem(qs(it->name) + (it->locked ? tr("  🔒") : QString()), list_);
        item->setData(kIdRole, static_cast<qulonglong>(it->id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(it->visible ? Qt::Checked : Qt::Unchecked);
    }
    if (keep >= 0 && keep < list_->count()) {
        list_->setCurrentRow(keep);
    }
    refreshing_ = false;
}

std::uint64_t CalquePanel::currentId() const {
    auto* item = list_->currentItem();
    return item == nullptr ? 0 : item->data(kIdRole).toULongLong();
}

bool CalquePanel::report(bool ok, const std::string& message) {
    if (!ok) {
        emit refused(qs(message));
    }
    return ok;
}

bool CalquePanel::addNamed(const QString& name) {
    const auto r = addCalque(session_, name.toStdString());
    return report(r.ok, r.message);
}
bool CalquePanel::renameCurrent(const QString& name) {
    const auto r = renameCalque(session_, currentId(), name.toStdString());
    return report(r.ok, r.message);
}
bool CalquePanel::removeCurrent() {
    const auto r = removeCalque(session_, currentId());
    return report(r.ok, r.message);
}
bool CalquePanel::toggleLockCurrent() {
    const auto id = currentId();
    for (const auto& c : calques(session_.project())) {
        if (c.id == id) {
            const auto r = setCalqueLocked(session_, id, !c.locked);
            return report(r.ok, r.message);
        }
    }
    return report(false, "Choisissez un calque.");
}
bool CalquePanel::moveCurrent(int direction) {
    const auto r = moveCalque(session_, currentId(), direction);
    return report(r.ok, r.message);
}
bool CalquePanel::moveSelectionHere() {
    const auto state = inspect(session_.project(), session_.selection().targets());
    const auto r = moveObjectsToCalque(session_, state.ids, currentId());
    return report(r.ok, r.message);
}
void CalquePanel::setCurrentRow(int row) {
    list_->setCurrentRow(row);
}
int CalquePanel::rowCount() const {
    return list_->count();
}
QString CalquePanel::rowText(int row) const {
    return list_->item(row) != nullptr ? list_->item(row)->text() : QString();
}
bool CalquePanel::rowVisible(int row) const {
    return list_->item(row) != nullptr && list_->item(row)->checkState() == Qt::Checked;
}
bool CalquePanel::setRowVisible(int row, bool visible) {
    auto* item = list_->item(row);
    if (item == nullptr) {
        return false;
    }
    const auto depth = session_.undoNames().size();
    item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    return session_.undoNames().size() > depth;
}

} // namespace openstitch::desktop_v2
