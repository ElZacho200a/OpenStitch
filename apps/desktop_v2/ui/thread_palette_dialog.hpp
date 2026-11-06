// SPDX-License-Identifier: Apache-2.0
#pragma once

// Nuanciers de fils : parcourir un nuancier, chercher par nom ou référence, voir les fils les plus
// proches d'un fil du plan, proposer un mélange de deux fils et assigner un fil de nuancier.
// Aucune logique métier : tout passe par `thread_palette_service` (cœur).

#include <QDialog>
#include <QString>

#include "document_session.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace openstitch::desktop_v2 {

class ThreadPaletteDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ThreadPaletteDialog(DocumentSession& session, QWidget* parent = nullptr);

    void setChart(const QString& chartId);
    void setSearch(const QString& text);
    void selectEntry(int index);   // fil du plan de fils à qui assigner
    void selectThreadRow(int row); // ligne de la liste des fils
    QString assignSelected();      // refus en clair, ou chaîne vide
    QString assignNearest();       // assigne le fil le plus proche du fil du plan

    [[nodiscard]] int chartCount() const;
    [[nodiscard]] int threadRowCount() const;
    [[nodiscard]] QString nearestText() const;
    [[nodiscard]] QString blendText() const;

private:
    void rebuildThreads();
    void refreshSuggestions();

    DocumentSession& session_;
    QComboBox* chart_{nullptr};
    QLineEdit* search_{nullptr};
    QListWidget* threads_{nullptr};
    QComboBox* entry_{nullptr};
    QLabel* nearest_{nullptr};
    QLabel* blend_{nullptr};
    QLabel* feedback_{nullptr};
};

} // namespace openstitch::desktop_v2
