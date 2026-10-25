// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace openstitch::desktop {

// Coloris multiples (HP-THR-006) et tissu de fond (HP-THR-008). Dialogue non modal, SANS logique
// métier : il affiche l'état que MainWindow lui donne (`setState`) et émet des demandes ; c'est
// MainWindow qui les exécute par des ICommand annulables puis rafraîchit le dialogue.
class ColorwayDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ColorwayDialog(QWidget* parent = nullptr);

    // `names` : un coloris par ligne ; `active` : indice du coloris actif (-1 = aucun).
    void setState(const QStringList& names, int active, const QColor& fabric,
                  const QString& fabricName);

signals:
    void addRequested(const QString& suggestedName);
    void activateRequested(int index);
    void removeRequested(int index);
    void renameRequested(int index, const QString& name);
    void fabricChangeRequested(const QColor& color, const QString& name);

private:
    void updateButtons();
    void pickFabricColor();
    void applyPreset(int presetIndex);
    void emitFabric();
    void paintSwatch();

    QListWidget* list_{nullptr};
    QPushButton* addButton_{nullptr};
    QPushButton* activateButton_{nullptr};
    QPushButton* removeButton_{nullptr};
    QPushButton* fabricButton_{nullptr};
    QComboBox* presetCombo_{nullptr};
    QLineEdit* fabricName_{nullptr};
    QColor fabric_{Qt::white};
    int active_{-1};
    bool populating_{false};
};

} // namespace openstitch::desktop
