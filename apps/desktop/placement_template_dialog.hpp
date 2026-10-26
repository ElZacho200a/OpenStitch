// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QString>

#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/placement_template.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QPagedPaintDevice;
class QPrinter;

namespace openstitch::desktop {

// Dessine le gabarit sur un périphérique paginé (QPrinter ou QPdfWriter) à l'échelle 1:1 :
// 1 µm du gabarit = 1 µm de papier. Le périphérique doit avoir la taille de page du gabarit
// et des marges nulles (fixées ici).
void paintPlacementTemplate(QPagedPaintDevice* device,
                            const stitch_analysis::PlacementTemplate& tpl);

// HP-PROD-003 : Fichier > Gabarit de placement 1:1… Choix du papier / de l'orientation, nombre
// de pages, aperçu avant impression (HP-PROD-004), impression et export PDF vectoriel exact
// (généré par le cœur, sans Qt). Aucun calcul ici : le gabarit vient de
// `stitch_analysis::make_placement_template` sur la séquence EFFECTIVE.
class PlacementTemplateDialog : public QDialog {
    Q_OBJECT

public:
    PlacementTemplateDialog(const stitch::StitchSequence& sequence, const QString& projectName,
                            QWidget* parent = nullptr);

    [[nodiscard]] const stitch_analysis::PlacementTemplate& placementTemplate() const {
        return tpl_;
    }
    void refreshNow();
    // PDF vectoriel du cœur. Faux si le fichier n'a pas pu être écrit.
    bool exportPdf(const QString& path) const;
    // PDF produit par le chemin d'impression Qt (même dessin que l'impression papier).
    bool exportQtPdf(const QString& path) const;

    [[nodiscard]] QComboBox* paperCombo() const { return paper_; }
    [[nodiscard]] QCheckBox* landscapeCheck() const { return landscape_; }
    [[nodiscard]] QString summaryText() const;

private:
    void configurePrinter(QPrinter& printer) const;
    void preview();
    void print();
    void exportPdfInteractive();

    const stitch::StitchSequence& sequence_;
    QString name_;
    stitch_analysis::PlacementTemplate tpl_;
    QComboBox* paper_{nullptr};
    QCheckBox* landscape_{nullptr};
    QLabel* summary_{nullptr};
};

} // namespace openstitch::desktop
