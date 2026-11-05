// SPDX-License-Identifier: Apache-2.0
#include "command_palette.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

CommandPalette::CommandPalette(QWidget* parent)
    : QDialog(parent), query_(new QLineEdit(this)), hint_(new QLabel(this)),
      list_(new QListWidget(this)) {
    setObjectName(QStringLiteral("commandPalette"));
    setWindowTitle(tr("Palette de commandes"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    resize(520, 360);
    query_->setObjectName(QStringLiteral("paletteQuery"));
    query_->setPlaceholderText(tr("Tapez une commande, un outil, un espace…"));
    query_->setAccessibleName(tr("Rechercher une commande"));
    list_->setObjectName(QStringLiteral("paletteList"));
    list_->setAccessibleName(tr("Commandes"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(query_);
    hint_->setObjectName(QStringLiteral("paletteHint"));
    hint_->setWordWrap(true);
    hint_->setAccessibleName(tr("État de la recherche"));
    hint_->hide();
    layout->addWidget(hint_);
    layout->addWidget(list_, 1);
    setFocusProxy(query_);
    query_->installEventFilter(this); // flèches / Entrée / Échap depuis le champ de saisie

    connect(query_, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(list_, &QListWidget::itemActivated, this, [this] { activateCurrent(); });
}

void CommandPalette::setEntries(std::vector<CommandEntry> entries) {
    entries_ = std::move(entries);
    refill();
}

void CommandPalette::setQuery(const QString& query) {
    query_->setText(query); // textChanged -> refill
    refill();
}

void CommandPalette::refill() {
    list_->clear();
    for (const auto* e : filterCommands(entries_, query_->text().toStdString())) {
        QString text = qs(e->label);
        if (!e->shortcut.empty()) {
            text += QStringLiteral("    [%1]").arg(qs(e->shortcut));
        }
        auto* item = new QListWidgetItem(text, list_);
        item->setData(kIdRole, qs(e->id));
        if (!e->enabled) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setText(text + tr(" — indisponible"));
            item->setToolTip(tr("%1 — commande indisponible dans ce contexte").arg(qs(e->group)));
        } else {
            item->setToolTip(qs(e->group));
        }
    }
    int firstEnabled = -1;
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->flags() & Qt::ItemIsEnabled) {
            firstEnabled = row;
            break;
        }
    }
    list_->setCurrentRow(firstEnabled);
    hint_->setVisible(firstEnabled < 0);
    if (list_->count() == 0) {
        hint_->setText(tr("Aucune commande trouvée. Essayez un autre terme."));
    } else if (firstEnabled < 0) {
        hint_->setText(tr("Les commandes trouvées sont indisponibles dans ce contexte."));
    }
}

QStringList CommandPalette::visibleIds() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) {
        out << list_->item(i)->data(kIdRole).toString();
    }
    return out;
}

bool CommandPalette::activateCurrent() {
    auto* item = list_->currentItem();
    if (item == nullptr || !(item->flags() & Qt::ItemIsEnabled)) {
        return false;
    }
    const QString id = item->data(kIdRole).toString();
    accept();
    emit chosen(id);
    return true;
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
    if (watched == query_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up: {
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            if (list_->count() == 0) {
                return true;
            }
            int row = list_->currentRow();
            // Lignes désactivées ignorées ; si aucune commande n'est exécutable,
            // la sélection reste vide et Entrée ne fait rien.
            for (int n = 0; n < list_->count(); ++n) {
                row = (row + step + list_->count()) % list_->count();
                if (list_->item(row)->flags() & Qt::ItemIsEnabled) {
                    list_->setCurrentRow(row);
                    break;
                }
            }
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateCurrent();
            return true;
        case Qt::Key_Escape:
            reject();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    query_->setFocus();
    query_->selectAll();
}

} // namespace openstitch::desktop_v2
