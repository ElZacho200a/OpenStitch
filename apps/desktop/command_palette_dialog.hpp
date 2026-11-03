// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>

class QAction;
class QEvent;
class QLineEdit;
class QListWidget;
class QMenuBar;
class QWidget;
class QLabel;

namespace openstitch::desktop {

// Commandes existantes uniquement : jamais de chemin métier alternatif.
class CommandPaletteDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CommandPaletteDialog(QMenuBar* menus, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry {
        QPointer<QAction> action;
        QString path;
        QString keywords;
    };

    void rebuildResults();
    void activateCurrent();

    QList<Entry> entries_;
    QPointer<QWidget> previousFocus_;
    QLineEdit* search_{nullptr};
    QListWidget* results_{nullptr};
    QLabel* feedback_{nullptr};
};

} // namespace openstitch::desktop
