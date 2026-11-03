// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;

namespace openstitch::desktop {

// HP-HOOP-002 : choix du profil machine du projet (liste de `document::machine_profiles()`),
// avec le détail du profil et, au choix, l'un des cadres de la machine à appliquer. Aucune
// logique métier : la liste, les valeurs et la mutation (SetMachineCommand) viennent du cœur.
class MachineDialog : public QDialog {
    Q_OBJECT
public:
    explicit MachineDialog(const QString& currentMachineId, QWidget* parent = nullptr);

    // Identifiant du profil choisi ; vide = « Aucune machine ».
    [[nodiscard]] QString machineId() const;
    // Cadre à appliquer (identifiant de `document::hoop_library()`) ; vide = garder le cadre.
    [[nodiscard]] QString hoopId() const;

    // Texte descriptif d'un profil (champ, aiguilles, formats, coupe-fil, vitesse).
    [[nodiscard]] static QString describe(const QString& machineId);

private:
    void refresh();

    QComboBox* machineCombo_{nullptr};
    QComboBox* hoopCombo_{nullptr};
    QLabel* details_{nullptr};
};

} // namespace openstitch::desktop
