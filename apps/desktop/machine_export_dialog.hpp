// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QString>

#include <functional>

#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/project_io/machine_file.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace openstitch::desktop {

// HP-FMT-002..005 : choix du format machine (DST, PES, JEF, EXP -- tout ce que le registre sait
// écrire) et des options machine (coupes, arrêts, changements de couleur) avant l'export, avec le
// résumé pré-export recalculé à chaque changement. Aucune logique métier : le résumé est fourni
// par la fenêtre principale via `summarize`, l'écriture est faite par `project_io`.
class MachineExportDialog : public QDialog {
    Q_OBJECT

public:
    struct Summary {
        QString text;                    // texte multi-lignes affiché
        bool has_analysis_errors{false}; // erreurs d'analyse : « Voir les problèmes » proposé
        bool blocking{false};            // limite du format dépassée : l'export serait refusé
    };
    using SummaryFn =
        std::function<Summary(const formats::FormatInfo&, const formats::MachineExportOptions&,
                              const project_io::ExportOptimizations&)>;

    MachineExportDialog(SummaryFn summarize, const QString& initialFormatId, QWidget* parent);

    [[nodiscard]] const formats::FormatInfo* format() const;
    [[nodiscard]] formats::MachineExportOptions options() const;
    // Optimisations demandées pour cet export (HP-SEQ-006) : appliquées à la toute fin, sur la
    // séquence exportée seulement -- le projet n'est pas modifié.
    [[nodiscard]] project_io::ExportOptimizations optimizations() const;
    [[nodiscard]] bool viewProblemsRequested() const { return viewProblems_; }

private:
    void refresh();

    SummaryFn summarize_;
    QComboBox* formatBox_{nullptr};
    QCheckBox* trims_{nullptr};
    QComboBox* stops_{nullptr};
    QCheckBox* colorChanges_{nullptr};
    QComboBox* startMode_{nullptr};
    QDoubleSpinBox* startX_{nullptr};
    QDoubleSpinBox* startY_{nullptr};
    QCheckBox* reduceTrims_{nullptr};
    QCheckBox* between_{nullptr};
    QCheckBox* within_{nullptr};
    QCheckBox* underlay_{nullptr};
    QCheckBox* regions_{nullptr};
    QCheckBox* sameColour_{nullptr};
    QDoubleSpinBox* maxBridge_{nullptr};
    QLabel* summary_{nullptr};
    QPushButton* chooseBtn_{nullptr};
    QPushButton* problemsBtn_{nullptr};
    bool viewProblems_{false};
};

} // namespace openstitch::desktop
