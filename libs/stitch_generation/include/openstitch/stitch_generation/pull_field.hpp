// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "openstitch/core/units.hpp"

namespace openstitch::stitch_generation {

// AdaptivePullCompensation, NIVEAU 3 (prédiction) : champ de déplacement du tissu.
//
// Le niveau 1 (`adaptive_pull.hpp`) compense chaque objet isolément, selon sa propre géométrie.
// Le niveau 3 estime au contraire un CHAMP : en tout point du plan, de combien le tissu se
// déplace sous la traction de TOUS les fils voisins, et avec quelle confiance. Chaque segment
// cousu contracte le tissu le long de son axe ; la réponse est anisotrope (le sens des fils du
// tissu, chaîne / trame, rend le déplacement plus facile dans un sens que dans l'autre).
//
// Fonctions pures et déterministes (même entrée -> même sortie, octet pour octet : sources
// parcourues par indice croissant, cellules par clé ordonnée). Le champ n'est jamais stocké dans
// le document ; il se recalcule à la génération. LOT A : cœur seul, non branché sur
// `PullMode` / `effective_sequence` (lots suivants).
//
// AUCUNE MESURE PHYSIQUE n'est incorporée. Les coefficients par défaut sont des règles
// empiriques non validées sur machine : tant que `FabricResponse::calibrated` est faux, la
// confiance est plafonnée à `uncalibrated_confidence`. Le recalage viendra de la pièce-test
// (HP-ENG-001.a). Le champ est une fonction CONTINUE de la géométrie cousue ; il ne reproduit
// aucune table « largeur de bout × tissu » (voir roadmap-rd-brevets.md).

// Réponse du tissu. Tous les coefficients sont empiriques jusqu'à calibration.
struct FabricResponse {
    double gain{0.020};          // contraction relative le long d'un segment (2 %)
    double poisson{0.30};        // poussée latérale, fraction de la contraction axiale
    double sigma_along_mm{1.2};  // portée au-delà du bout d'un segment, dans son axe
    double sigma_across_mm{0.8}; // portée perpendiculaire à un segment
    double grain_angle_rad{0.0}; // direction des fils de chaîne (0 = axe X, Y vers le haut)
    double grain_anisotropy{
        1.0}; // souplesse trame / chaîne (1 = isotrope ; > 1 : trame plus souple)
    // Tassement du tissu : le déplacement cumulé est divisé par 1 + compression · (somme des poids
    // de recouvrement), de sorte que la superposition de nombreux segments sature au lieu de
    // croître linéairement. 0 = ancien comportement linéaire. Empirique, à calibrer.
    double compression{0.06};
    // Plafond doux : la norme tend vers ce maximum (tanh) sans jamais le dépasser, au lieu d'un
    // écrêtage brutal qui aplatit toute la zone dense au même module.
    double max_displacement_mm{0.8};     // plafond absolu de la norme du déplacement
    bool calibrated{false};              // vrai seulement si les coefficients viennent de mesures
    double calibrated_confidence{0.8};   // confiance de base d'un tissu calibré
    double uncalibrated_confidence{0.3}; // confiance de base sans calibration
    // Intensité de la correction inverse, distincte de la confiance du modèle.
    // 3× par défaut pour rendre le mode Predicted sensible ; la correction finale
    // reste bornée par max_displacement_mm. Empirique, à calibrer sur tissu.
    double compensation_strength{3.0};
    // Passes de la compensation prédite : le décalage d'une zone change la géométrie réellement
    // cousue, donc le tirage qu'elle impose aux zones suivantes. Chaque passe recalcule le champ
    // depuis la géométrie compensée de la précédente (point fixe) ; 1 = ancien comportement.
    int compensation_passes{3};
};

// Un segment cousu : source de traction. `weight` module sa traction (densité de points, 1 =
// nominal).
// `order` : rang de couture du segment (indice de la commande dans la séquence). Un point cousu au
// rang r est déplacé par le tirage des segments de rang >= r seulement : le tissu a déjà bougé sous
// les segments cousus avant lui quand l'aiguille tombe, et son point suit ensuite le tissu.
struct PullSource {
    Vec2um a{};
    Vec2um b{};
    double weight{1.0};
    std::uint32_t order{0};
};

// Résultat d'un échantillon du champ.
struct PullSample {
    Vec2um displacement{};  // déplacement prédit du tissu en ce point
    double confidence{1.0}; // [0 ; 1] ; 1 hors de toute influence (déplacement nul, certain)
    std::uint32_t contributors{0}; // nombre de segments ayant contribué
};

class PullField {
public:
    PullField(std::vector<PullSource> sources, const FabricResponse& fabric);

    // `min_order` : seuls les segments de rang >= min_order tirent (0 = tous, champ final).
    [[nodiscard]] PullSample sample(Vec2um p, std::uint32_t min_order = 0) const;

    // Décalage à appliquer à un point cousu : opposé du déplacement, pondéré par
    // la confiance puis amplifié par compensation_strength. La norme finale reste
    // inférieure ou égale à max_displacement_mm (indépendamment de l'intensité).
    // `min_order` : rang de couture du point (voir PullSource::order).
    [[nodiscard]] Vec2um compensation_at(Vec2um p, std::uint32_t min_order = 0) const;

    [[nodiscard]] std::size_t source_count() const { return sources_.size(); }

private:
    std::vector<PullSource> sources_;
    FabricResponse fabric_;
    double cell_um_{1.0};
    double cutoff_um_{1.0};
    std::map<std::int64_t, std::vector<std::uint32_t>> grid_;

    [[nodiscard]] std::int64_t key_of(std::int64_t cx, std::int64_t cy) const;
};

} // namespace openstitch::stitch_generation
