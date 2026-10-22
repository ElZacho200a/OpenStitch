// SPDX-License-Identifier: Apache-2.0
#include "command_palette_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#include "responsive.hpp"

namespace openstitch::desktop {

namespace {

QString plainLabel(QString value) {
    value.remove(QLatin1Char('&')); // mnémoniques des menus, pas du texte à rechercher
    return value.simplified();
}

QString synonyms(const QString& label) {
    QString result;
    if (label.contains(QStringLiteral("ouvrir"), Qt::CaseInsensitive) ||
        label.contains(QStringLiteral("import"), Qt::CaseInsensitive)) {
        result += QStringLiteral(" ouvrir charger importer fichier ");
    }
    if (label.contains(QStringLiteral("broder"), Qt::CaseInsensitive) ||
        label.contains(QStringLiteral("point"), Qt::CaseInsensitive)) {
        result += QStringLiteral(" broderie couture point stitch ");
    }
    if (label.contains(QStringLiteral("export"), Qt::CaseInsensitive) ||
        label.contains(QStringLiteral("enregistrer"), Qt::CaseInsensitive)) {
        result += QStringLiteral(" sortie sauvegarder export ");
    }
    if (label.contains(QStringLiteral("vect"), Qt::CaseInsensitive) ||
        label.contains(QStringLiteral("forme"), Qt::CaseInsensitive)) {
        result += QStringLiteral(" contour chemin dessin geometrie ");
    }
    return result;
}

} // namespace

CommandPaletteDialog::CommandPaletteDialog(QMenuBar* menus, QWidget* parent)
    : QDialog(parent), previousFocus_(QApplication::focusWidget()) {
    setObjectName(QStringLiteral("commandPaletteDialog"));
    setWindowTitle(tr("Rechercher une commande"));
    setModal(false);
    setMinimumWidth(320);
    resize(600, 410);
    setAccessibleName(tr("Palette de commandes"));

    auto* layout = new QVBoxLayout(this);
    auto* caption = new QLabel(tr("Rechercher dans les commandes des menus"), this);
    layout->addWidget(caption);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("commandPaletteSearch"));
    search_->setPlaceholderText(tr("Nom, mot-clé ou catégorie…"));
    search_->setAccessibleName(tr("Rechercher une commande"));
    search_->installEventFilter(this);
    layout->addWidget(search_);

    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("commandPaletteResults"));
    results_->setAccessibleName(tr("Commandes correspondantes"));
    results_->setUniformItemSizes(true);
    layout->addWidget(results_, 1);

    feedback_ = new QLabel(this);
    feedback_->setObjectName(QStringLiteral("commandPaletteFeedback"));
    feedback_->setWordWrap(true);
    layout->addWidget(feedback_);

    // Parcourir les menus et leurs sous-menus évite de doubler leur logique.
    // Une QAction présente deux fois n'est indexée qu'une seule fois.
    QSet<QAction*> seen;
    std::function<void(QMenu*, const QString&)> collect;
    collect = [&](QMenu* menu, const QString& prefix) {
        for (QAction* action : menu->actions()) {
            if (action == nullptr || action->isSeparator() || !action->isVisible()) {
                continue;
            }
            const QString label = plainLabel(action->text());
            if (label.isEmpty()) {
                continue;
            }
            const QString path = prefix.isEmpty() ? label : prefix + QStringLiteral(" › ") + label;
            if (QMenu* sub = action->menu()) {
                collect(sub, path);
            } else if (!seen.contains(action) &&
                       action->objectName() != QLatin1String("action_commandPalette")) {
                seen.insert(action);
                entries_.append({action, path, synonyms(label)});
            }
        }
    };
    if (menus != nullptr) {
        for (QAction* root : menus->actions()) {
            if (root != nullptr && root->menu() != nullptr) {
                collect(root->menu(), plainLabel(root->text()));
            }
        }
    }

    connect(search_, &QLineEdit::textChanged, this, &CommandPaletteDialog::rebuildResults);
    connect(search_, &QLineEdit::returnPressed, this, &CommandPaletteDialog::activateCurrent);
    connect(results_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { activateCurrent(); });
    fitDialogToScreen(this);
    rebuildResults();
    search_->setFocus(Qt::PopupFocusReason);
}

bool CommandPaletteDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            if (results_->count() == 0) {
                return true;
            }
            const int delta = key->key() == Qt::Key_Down ? 1 : -1;
            const int next = qBound(0, results_->currentRow() + delta, results_->count() - 1);
            results_->setCurrentRow(next);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPaletteDialog::rebuildResults() {
    results_->clear();
    feedback_->clear();
    const QStringList terms =
        search_->text().simplified().toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_.at(i);
        if (!entry.action) {
            continue;
        }
        QString searchable = entry.path + QLatin1Char(' ') + entry.keywords;
        searchable += QLatin1Char(' ');
        searchable += entry.action->toolTip();
        searchable += QLatin1Char(' ');
        searchable += entry.action->statusTip();
        searchable = searchable.toCaseFolded();
        bool matches = true;
        for (const QString& term : terms) {
            if (!searchable.contains(term)) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            continue;
        }
        const QString shortcut = entry.action->shortcut().toString(QKeySequence::NativeText);
        QString label = entry.path;
        if (!shortcut.isEmpty()) {
            label += QStringLiteral("    (") + shortcut + QLatin1Char(')');
        }
        auto* item = new QListWidgetItem(label, results_);
        item->setData(Qt::UserRole, static_cast<int>(i));
        if (!entry.action->isEnabled()) {
            item->setText(label + tr(" — indisponible"));
            item->setToolTip(entry.action->statusTip().isEmpty()
                                 ? tr("Action indisponible dans le contexte actuel.")
                                 : entry.action->statusTip());
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        } else {
            item->setToolTip(entry.action->toolTip());
        }
        if (results_->count() >= 80) {
            break;
        }
    }
    if (results_->count() > 0) {
        results_->setCurrentRow(0);
    } else {
        feedback_->setText(tr("Aucune commande correspondante."));
    }
}

void CommandPaletteDialog::activateCurrent() {
    QListWidgetItem* selected = results_->currentItem();
    if (selected == nullptr) {
        return;
    }
    const int index = selected->data(Qt::UserRole).toInt();
    if (index < 0 || index >= entries_.size() || !entries_.at(index).action) {
        return;
    }
    const QPointer<QAction> chosen = entries_.at(index).action;
    if (!chosen->isEnabled()) {
        feedback_->setText(chosen->statusTip().isEmpty()
                               ? tr("Action indisponible dans le contexte actuel.")
                               : chosen->statusTip());
        return;
    }
    const QPointer<QWidget> previousFocus = previousFocus_;
    QWidget* host = parentWidget();
    accept();
    // Lancer APRES la fermeture permet aux actions ouvrant une modale
    // de retrouver normalement la fenêtre principale et son focus.
    if (host != nullptr) {
        QTimer::singleShot(0, host, [chosen, previousFocus] {
            if (previousFocus != nullptr && previousFocus->isVisible()) {
                previousFocus->setFocus(Qt::ShortcutFocusReason);
            }
            if (chosen != nullptr && chosen->isEnabled()) {
                chosen->trigger();
            }
        });
    }
}

} // namespace openstitch::desktop
