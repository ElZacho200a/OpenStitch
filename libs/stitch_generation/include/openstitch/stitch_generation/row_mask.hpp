// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>

#include "openstitch/core/units.hpp"

namespace openstitch::stitch_generation {

// MASQUE DE RANGÉES d'un fondu (docs/studio-fondu-frontiere-spec.md §6.1).
//
// Un remplissage masqué garde une grille de rangées FIXE et n'en coud qu'une partie : la rangée
// d'indice absolu `k` porte le seuil `row_threshold(k)` ∈ [0 ; 1), et chaque morceau de rangée
// (entre deux pénétrations) n'est cousu que si `keeps(milieu, seuil)` est vrai. À densité `d`,
// une proportion ≈ `d` des rangées est cousue, régulièrement répartie. Deux couches dont les
// masques se partagent [0 ; 1) (l'une garde `seuil < d`, l'autre `seuil >= d`) cousent chaque
// morceau de rangée exactement une fois.
//
// Le masque est une donnée dérivée à la génération, jamais stockée dans le document. Il est lu
// en lecture seule ; une implémentation doit être sans état mutable (appels concurrents
// possibles depuis `parallel_for`).
class RowMask {
public:
    RowMask() = default;
    RowMask(const RowMask&) = default;
    RowMask& operator=(const RowMask&) = default;
    RowMask(RowMask&&) = default;
    RowMask& operator=(RowMask&&) = default;
    virtual ~RowMask() = default;

    // La couche coud-elle, au point `p` (repère du modèle), la rangée de seuil `threshold` ?
    [[nodiscard]] virtual bool keeps(Vec2um p, double threshold) const = 0;
    // Proportion des rangées cousues en `p`, dans [0 ; 1] (largeur de la tranche gardée). Sert
    // aux liaisons : une liaison ne traverse pas une zone où le fil est presque absent.
    [[nodiscard]] virtual double coverage(Vec2um p) const = 0;
};

// Seuil de la rangée d'indice ABSOLU `k` : suite de Weyl du nombre d'or, frac(k × 0,618…),
// comme `row_thresholds` du remplissage directionnel. Fonction de `k` seul : deux régions
// différentes qui partagent une rangée lui donnent le même seuil. Valeur dans [0 ; 1).
[[nodiscard]] double row_threshold(std::int64_t k);

// Masque défini par une densité `d(p)` ∈ [0 ; 1] (bornée à la lecture).
// - `Slice::Lower` : garde les rangées de seuil < d(p) (la couche « de départ ») ;
// - `Slice::Upper` : garde les rangées de seuil >= 1 - d(p) (la couche complémentaire).
// Avec `d_B = 1 - d_A`, une couche Lower de densité d_A et une couche Upper de densité d_B se
// partagent exactement chaque rangée.
class DensityRowMask final : public RowMask {
public:
    enum class Slice : std::uint8_t { Lower, Upper };
    using Field = std::function<double(Vec2um)>;

    DensityRowMask(Field density, Slice slice);

    [[nodiscard]] bool keeps(Vec2um p, double threshold) const override;
    [[nodiscard]] double coverage(Vec2um p) const override;

private:
    Field density_;
    Slice slice_;
};

} // namespace openstitch::stitch_generation
