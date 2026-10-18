// SPDX-License-Identifier: Apache-2.0
#pragma once

// Carte de densité (HP-VIEW-005) côté bureau : présentation uniquement. Le calcul (grille,
// zones sur-denses) vient de libs/stitch_analysis/density_map.hpp ; ici on ne fait que
// rasteriser la grille en image colorée (palette froide -> chaude), clé de cache, réglages
// persistants et légende. Rien ici ne touche au document.

#include <QWidget>

#include <array>
#include <cstdint>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/density_map.hpp"

namespace openstitch::desktop {

// Image RGBA (une case de la grille = un pixel), ligne 0 = haut de l'écran (Y le plus grand).
struct DensityRaster {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
    double scale_max{1.0}; // densité (pén./mm²) au sommet de la palette
};

// Résultat complet d'un calcul, produit hors du fil UI sur un instantané de la séquence.
struct DensityResult {
    DensityRaster raster;
    Vec2um origin{};     // coin bas-gauche de la case (0, 0)
    Micrometers cell{0}; // côté de case effectivement utilisé
    double max_density{0.0};
    std::vector<stitch_analysis::DensityHotspot> hotspots;
};

// Palette froide -> chaude : t = 0 bleu, 0,5 vert, 1 rouge (t borné à [0, 1]).
[[nodiscard]] std::array<std::uint8_t, 3> densityColor(double t);

// Rasterise la grille. Les cases vides sont transparentes ; celles au-delà de `threshold`
// (> 0) sont plus opaques. `scale_max` = max(densité maximale, seuil, 1).
[[nodiscard]] DensityRaster rasterizeDensity(const stitch_analysis::DensityMap& map,
                                             double threshold);

// Calcul pur et déterministe (sans Qt-GUI), sûr hors du fil UI.
[[nodiscard]] DensityResult computeDensity(const stitch::StitchSequence& sequence,
                                           double threshold);

// Clé de cache : condensat de la séquence + seuil. Deux séquences identiques -> même clé.
[[nodiscard]] std::uint64_t densityKey(const stitch::StitchSequence& sequence, double threshold);

// Réglages des deux règles d'analyse désactivées par défaut (HP-PROD-007), mémorisés d'une
// session à l'autre. Valeurs initiales : seuil de densité du cœur ; coupes : 50.
struct ExtraRuleSettings {
    bool trimsEnabled{false};
    int maxTrims{50};
    bool densityEnabled{false};
    double maxDensity{stitch_analysis::DensityOptions{}.max_penetrations_per_mm2};

    [[nodiscard]] static ExtraRuleSettings load();
    void save() const;
};

// Seuil de la carte de densité (zones sur-denses), mémorisé ; défaut = valeur du cœur.
[[nodiscard]] double loadDensityThreshold();
void saveDensityThreshold(double value);

// Légende de la carte (dégradé + valeurs en pén./mm²), posée en bas à gauche de la vue.
// S'attache au widget parent (viewport) et se repositionne à son redimensionnement.
class DensityLegend : public QWidget {
    Q_OBJECT
public:
    explicit DensityLegend(QWidget* parent);
    void setRange(double scaleMax, double threshold);
    [[nodiscard]] double scaleMax() const { return scaleMax_; }
    [[nodiscard]] double threshold() const { return threshold_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void place();
    double scaleMax_{1.0};
    double threshold_{0.0};
};

} // namespace openstitch::desktop
