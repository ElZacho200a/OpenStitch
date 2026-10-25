// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <vector>

#include "openstitch/stitch_analysis/thread_reduction.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"

class QComboBox;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;

namespace openstitch::desktop {

// Réduire / fusionner les fils du design (HP-THR-011) avec APERÇU avant application. Le plan vient
// de stitch_analysis (plan_reduce_threads / plan_merge_threads) : le dialogue ne fait qu'afficher
// le plan et le rendre à l'appelant, qui l'exécute en une seule commande annulable.
class ThreadMergeDialog final : public QDialog {
    Q_OBJECT

public:
    ThreadMergeDialog(std::vector<stitch_analysis::ThreadUsage> usage, QWidget* parent = nullptr);

    // Plan courant (selon le mode et les réglages affichés).
    [[nodiscard]] const stitch_analysis::ThreadMergePlan& plan() const { return plan_; }

private:
    void recompute();
    void fillPreview();

    std::vector<stitch_analysis::ThreadUsage> usage_;
    stitch_analysis::ThreadMergePlan plan_;
    QRadioButton* reduceMode_{nullptr};
    QRadioButton* manualMode_{nullptr};
    QSpinBox* countSpin_{nullptr};
    QComboBox* fromCombo_{nullptr};
    QComboBox* toCombo_{nullptr};
    QTableWidget* preview_{nullptr};
    QPushButton* applyButton_{nullptr};
};

} // namespace openstitch::desktop
