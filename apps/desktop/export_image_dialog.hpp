// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include "export_image.hpp"
#include "openstitch/stitch_render/params.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace openstitch::desktop {

// Fichier > Exporter l'image… (HP-FMT-011) : réglages de l'image de présentation du motif.
// Aucun calcul métier : le dialogue ne fait que lire / afficher des réglages ; la taille
// annoncée vient de `stitch_render::plan_view`, le rendu de `render_export_image`.
class ExportImageDialog : public QDialog {
    Q_OBJECT

public:
    // `widthMm` x `heightMm` : boîte englobante des points cousus (pour annoncer la taille en
    // pixels). `baseParams` : tissu, fil et relief de départ (ceux du rendu réaliste du bureau).
    ExportImageDialog(double widthMm, double heightMm,
                      const stitch_render::RenderParams& baseParams, QWidget* parent = nullptr);

    [[nodiscard]] ExportImageSettings settings() const;

private:
    void refresh();

    double widthMm_{0.0};
    double heightMm_{0.0};
    stitch_render::RenderParams base_;
    QDoubleSpinBox* resolution_{nullptr};
    QDoubleSpinBox* margin_{nullptr};
    QCheckBox* opaque_{nullptr};
    QComboBox* quality_{nullptr};
    QComboBox* format_{nullptr};
    QSpinBox* jpegQuality_{nullptr};
    QLabel* summary_{nullptr};
};

} // namespace openstitch::desktop
