// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QString>

#include "length_units.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace openstitch::desktop {

// HP-UX-007 : préférences générales de l'application (unités, grille, sauvegarde automatique,
// dossier de travail). Jamais écrites dans le document ; persistées via QSettings.
struct GeneralPreferences {
    length_units::Unit unit{length_units::Unit::Millimeters};
    double gridStepMm{1.0};   // pas de la grille (stocké en mm quelle que soit l'unité affichée)
    int autosaveMinutes{2};   // intervalle de la sauvegarde automatique
    QString workingDirectory; // dossier proposé par défaut aux dialogues de fichiers (vide = auto)

    [[nodiscard]] GeneralPreferences clamped() const;
    bool operator==(const GeneralPreferences&) const = default;
};

[[nodiscard]] GeneralPreferences loadGeneralPreferences();
void saveGeneralPreferences(const GeneralPreferences& prefs);

class GeneralPreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit GeneralPreferencesDialog(QWidget* parent = nullptr);

    void setPreferences(const GeneralPreferences& prefs);
    [[nodiscard]] GeneralPreferences preferences() const;

private:
    void onUnitChanged();
    [[nodiscard]] length_units::Unit selectedUnit() const;

    QComboBox* unit_{nullptr};
    QDoubleSpinBox* gridStep_{nullptr};
    QSpinBox* autosave_{nullptr};
    QLineEdit* workDir_{nullptr};
    length_units::Unit shownUnit_{length_units::Unit::Millimeters};
    double gridMm_{1.0}; // vérité du pas de grille (la boîte n'en est que l'affichage arrondi)
};

} // namespace openstitch::desktop
