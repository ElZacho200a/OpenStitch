// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// FONDU À LA FRONTIÈRE entre deux zones voisines A et B (docs/studio-fondu-frontiere-spec.md).
//
// s(p) : distance signée de p à la frontière commune F, négative dans A, positive dans B.
// t = (s + w_A) / (w_A + w_B) borné à [0 ; 1], c = courbe(t), densité de A d_A = 1 - c,
// densité de B d_B = c. Chaque zone est prolongée dans la demi-bande de l'autre
// (R_A = A ∪ (B ∩ {s <= w_B})), et cousue par un masque de rangées (§6.1) : A garde les
// rangées de seuil < d_A, B celles de seuil >= d_A. Même angle et même écart : chaque morceau
// de rangée est cousu une seule fois. Sinon : superposition, couverture ≈ d_A + d_B = 1.
struct BoundaryBlendSpec {
    Micrometers width_a{2'500}; // demi-bande dans A, bornée à [0,5 ; 15] mm
    Micrometers width_b{2'500}; // demi-bande dans B, bornée à [0,5 ; 15] mm
    document::DensityCurve curve{document::DensityCurve::Linear};
    Micrometers tolerance{600};      // contact entre A et B (jours de vectorisation, inset)
    Micrometers min_frontier{1'000}; // morceau de frontière plus court : ignoré

    bool operator==(const BoundaryBlendSpec&) const = default;
};

// Champ de densité d'un fondu à la frontière. Immuable après construction, lecture
// concurrente sûre (aucun état mutable).
class BoundaryBlendField {
public:
    // `reach_margin` : prolongation des régions au-delà des demi-bandes (cf.
    // `plan_boundary_blend`) ; la boîte de `near_frontier` l'englobe.
    BoundaryBlendField(const geometry::PathSet& a, std::vector<geometry::Polyline> frontier,
                       const BoundaryBlendSpec& spec, Micrometers reach_margin = {});

    // Distance signée (µm) à la frontière : < 0 dans A, > 0 hors de A.
    [[nodiscard]] double signed_distance(Vec2um p) const;
    // Densité de A en p, dans [0 ; 1] ; celle de B vaut 1 - density_a(p).
    [[nodiscard]] double density_a(Vec2um p) const;
    // Vrai si p est assez près de la frontière pour être dans une région prolongée (boîte
    // englobante de la frontière élargie de la plus grande demi-bande et de la marge) ; hors de
    // cette boîte, chaque zone est un aplat (densité 1 dans sa zone, 0 dans l'autre).
    [[nodiscard]] bool near_frontier(Vec2um p) const;

    [[nodiscard]] const std::vector<geometry::Polyline>& frontier() const { return frontier_; }
    [[nodiscard]] double width_a() const { return widthA_; }
    [[nodiscard]] double width_b() const { return widthB_; }

private:
    [[nodiscard]] bool inside_a(Vec2um p) const;

    std::vector<std::vector<Vec2um>> polysA_; // A aplatie : extérieur puis trous
    std::vector<geometry::Polyline> frontier_;
    double widthA_{2'500.0};
    double widthB_{2'500.0};
    document::DensityCurve curve_{document::DensityCurve::Linear};
    double boxX0_{0.0};
    double boxY0_{0.0};
    double boxX1_{-1.0};
    double boxY1_{-1.0};
};

// Plan d'un fondu : frontière, régions prolongées et champ partagé par les deux couches.
struct BoundaryBlendPlan {
    std::vector<geometry::Polyline> frontier;
    std::vector<geometry::PathSet> region_a; // A ∪ (B ∩ bande de largeur w_B + marge)
    std::vector<geometry::PathSet> region_b; // B ∪ (A ∩ bande de largeur w_A + marge)
    std::shared_ptr<const BoundaryBlendField> field;
};

// Calcule le plan. Les régions reçues sont celles que la génération coud (retrait de bord déjà
// appliqué, d'où la tolérance de contact). `reach_margin` prolonge chaque zone AU-DELÀ de la
// demi-bande de l'autre (densité déjà saturée à 0 ou 1) : le masque décide par case de la
// grille des pénétrations, et une case à cheval sur le bord de bande doit exister dans les deux
// couches pour que la partition reste exacte (la génération passe la longueur de point).
// Erreurs : `E_BLEND_NO_FRONTIER` si A et B ne se touchent pas, `E_BLEND_WIDTH_RANGE` si une
// largeur sort de [0,5 ; 15] mm.
[[nodiscard]] Result<BoundaryBlendPlan> plan_boundary_blend(const geometry::PathSet& a,
                                                            const geometry::PathSet& b,
                                                            const BoundaryBlendSpec& spec,
                                                            Micrometers reach_margin = {});

// Points des deux couches d'un fondu tatami–tatami (chacune dans sa région prolongée, avec son
// angle et son écart). Plusieurs composantes : concaténées, chacune commençant par un saut.
struct BoundaryBlendFills {
    std::vector<FillStitch> a;
    std::vector<FillStitch> b;
};

[[nodiscard]] Result<BoundaryBlendFills>
fill_boundary_blend_tatami(const geometry::PathSet& a, const document::TatamiParams& params_a,
                           const geometry::PathSet& b, const document::TatamiParams& params_b,
                           const BoundaryBlendSpec& spec);

} // namespace openstitch::stitch_generation
