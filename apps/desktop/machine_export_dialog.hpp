// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/project_io/machine_file.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QTimer;

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
        bool has_warnings{false};        // avertissements (analyse, cadre, limites du format)
        QString problems;                // liste des problèmes, montrée sans fermer le dialogue
        // HP-FMT-022 : blocs de couleur dans l'ordre où ils seront cousus (pastille + texte).
        struct BlockRow {
            unsigned rgb{0}; // 0xRRGGBB
            QString text;
        };
        std::vector<BlockRow> blocks;
    };
    using SummaryFn =
        std::function<Summary(const formats::FormatInfo&, const formats::MachineExportOptions&,
                              const project_io::ExportOptimizations&)>;

    // Derniers réglages d'export enregistrés par poste (HP-UX-032 : « Exporter avec les derniers
    // réglages »). Mêmes clés que le dialogue : un export rapide écrit ce que le dialogue aurait
    // proposé, sans l'afficher.
    struct SavedExport {
        QString formatId;
        formats::MachineExportOptions options;
        project_io::ExportOptimizations optimizations;
        project_io::ColorCompanionExport companion;
    };
    [[nodiscard]] static SavedExport loadSavedExport();

    MachineExportDialog(SummaryFn summarize, const QString& initialFormatId, QWidget* parent);

    // Délai d'inactivité avant de recalculer le résumé (analyse comprise) : cocher plusieurs
    // options d'affilée ne déclenche qu'un calcul (HP-UX-032).
    static constexpr int kPreviewDelayMs = 300;
    // Calcule tout de suite un aperçu en attente (appelé avant d'accepter ; point d'entrée de
    // test).
    void flushPendingRefresh();
    void accept() override;

    [[nodiscard]] const formats::FormatInfo* format() const;
    [[nodiscard]] formats::MachineExportOptions options() const;
    // Optimisations demandées pour cet export (HP-SEQ-006) : appliquées à la toute fin, sur la
    // séquence exportée seulement -- le projet n'est pas modifié.
    [[nodiscard]] project_io::ExportOptimizations optimizations() const;
    [[nodiscard]] bool viewProblemsRequested() const { return viewProblems_; }
    // HP-FMT-009 / HP-FMT-005 : « écrire le fichier de couleurs à côté » (DST, EXP). Le champ
    // `write` n'est vrai que si la case est cochée ET que le format n'emporte pas ses couleurs ;
    // `written` / `error` sont remplis par `project_io::export_machine_file`.
    [[nodiscard]] project_io::ColorCompanionExport companion() const;

signals:
    // « Enregistrer le projet d'abord » : l'appelant enregistre le .osp, le dialogue reste ouvert.
    void saveProjectRequested();

private:
    void refresh();         // immédiat : états des cases puis résumé
    void scheduleRefresh(); // états des cases tout de suite, résumé après kPreviewDelayMs
    void updateEnabledStates();
    void updateDefaultButton(bool risky, bool hasProblems);

    SummaryFn summarize_;
    QComboBox* formatBox_{nullptr};
    QCheckBox* trims_{nullptr};
    QComboBox* stops_{nullptr};
    QCheckBox* colorChanges_{nullptr};
    QCheckBox* companion_{nullptr};
    QComboBox* companionKind_{nullptr};
    QComboBox* startMode_{nullptr};
    QDoubleSpinBox* startX_{nullptr};
    QDoubleSpinBox* startY_{nullptr};
    QToolButton* optimizationsToggle_{nullptr};
    QCheckBox* reduceTrims_{nullptr};
    QCheckBox* between_{nullptr};
    QCheckBox* within_{nullptr};
    QCheckBox* underlay_{nullptr};
    QCheckBox* regions_{nullptr};
    QCheckBox* sameColour_{nullptr};
    QDoubleSpinBox* maxBridge_{nullptr};
    QLabel* summary_{nullptr};
    QLabel* blocksTitle_{nullptr};
    QListWidget* blocks_{nullptr};
    QPushButton* chooseBtn_{nullptr};
    QPushButton* problemsBtn_{nullptr};
    QPushButton* openAnalysisBtn_{nullptr};
    QPushButton* saveBtn_{nullptr};
    QPushButton* cancelBtn_{nullptr};
    QPlainTextEdit* problemsView_{nullptr};
    QTimer* refreshTimer_{nullptr};
    bool blocking_{false};
    bool viewProblems_{false};
};

// Une demande d'écriture de fichier machine : tout ce qu'il faut après le choix des réglages,
// que ceux-ci viennent du dialogue (Ctrl+E) ou des derniers réglages mémorisés (Ctrl+Maj+E).
struct MachineExportRequest {
    const formats::FormatInfo* format{nullptr};
    formats::MachineExportOptions options;
    project_io::ExportOptimizations optimizations;
    project_io::ColorCompanionExport companion;
    // Nombre de points du fichier écrit, s'il est connu d'avance (séquence optimisée en cache).
    std::optional<std::size_t> writtenStitches;
    // Fichier cible ; vide : la fenêtre le demande (boîte « Enregistrer sous »).
    QString file;
};

} // namespace openstitch::desktop
