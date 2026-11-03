// SPDX-License-Identifier: Apache-2.0
#pragma once

// Surimpression « Risque tissu » côté bureau : présentation uniquement. La grille de cases
// classées vient de libs/project_ops (`measure_fabric_risk`, la même analyse que le CLI et le
// serveur MCP) ; ici on ne fait que la rasteriser en image colorée, calculer la clé de cache et
// tenir la légende. Rien ici ne touche au document.

#include <QWidget>

#include <array>
#include <cstdint>
#include <vector>

#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::desktop {

// Image RGBA (une case de la grille = un pixel), ligne 0 = haut de l'écran (Y le plus grand).
struct FabricRiskRaster {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
};

// Palette accessible : vert bleuté, orange et rouge vif se distinguent aussi par leur opacité
// (croissante avec le risque) et par la légende textuelle. `None` : transparent.
[[nodiscard]] std::array<std::uint8_t, 4> fabricRiskColor(project_ops::FabricRiskLevel level);

[[nodiscard]] FabricRiskRaster rasterizeFabricRisk(const project_ops::FabricRiskGrid& grid);

// Nombre de cases par classe (None, Low, Moderate, High).
struct FabricRiskCounts {
    std::size_t none{0};
    std::size_t low{0};
    std::size_t moderate{0};
    std::size_t high{0};
};
[[nodiscard]] FabricRiskCounts countFabricRisk(const project_ops::FabricRiskGrid& grid);

// Profils de tissu proposés (indice -> identifiant de `FabricRiskOptions::fabric`).
[[nodiscard]] const char* fabricRiskProfileId(int profile);

// Clé de cache exacte : condensat de la séquence effective + profil de tissu.
[[nodiscard]] std::uint64_t fabricRiskKey(const stitch::StitchSequence& sequence, int profile);

// Légende (trois pastilles + libellés), posée en bas à droite de la vue.
class FabricRiskLegend : public QWidget {
    Q_OBJECT
public:
    explicit FabricRiskLegend(QWidget* parent);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void place();
};

} // namespace openstitch::desktop
