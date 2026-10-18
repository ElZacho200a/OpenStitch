// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// Poches sans fil d'un « bloc » uni (cœur, Qt-free, déterministe).
//
// Un bloc est un ensemble d'objets vectoriels qui doivent former une surface continue (zones
// de couleurs ou de types de points différents côte à côte). La VÉRITÉ de ce qui est brodé
// vient des points cousus de la séquence effective, pas des contours vectoriels : un remplissage
// ne couvre pas toujours toute sa région (retrait, satin plus étroit, retouches).
//
// Méthode : (1) enveloppe du bloc = union des emprises nominales de ses objets, fermée sur
// `closing_gap` (fentes entre formes voisines) et sans trous ; (2) masque des cellules à moins de
// `coverage_width / 2` d'un segment visible cousu (`TopStitch` / `Manual`, tous objets confondus
// -- un objet hors bloc peut couvrir la poche) ; (3) poches = cellules de l'enveloppe non
// couvertes ; (4) ouverture morphologique de largeur `min_width` (écarte les liserés le long des
// bords et les interstices entre rangées espacées) ; (5) tri par aire décroissante.
struct UncoveredPocketOptions {
    Micrometers coverage_width{500}; // même règle que `object_coverage`
    Micrometers cell{100};           // pas du masque (discrétisation)
    // Écart maximal entre deux formes du bloc pour le considérer comme une fente à combler.
    Micrometers closing_gap{1'000};
    // Largeur minimale d'une poche : en dessous, ce n'est pas un vide visible.
    Micrometers min_width{300};
    // Aires retenues (µm²) : [min_area ; max_area], max_area <= 0 = sans plafond.
    double min_area_um2{250'000.0}; // 0,25 mm²
    double max_area_um2{0.0};
    // Débordement des poches retournées sur leurs voisines, pour que le comblement (retrait de
    // contour compris) recouvre le bord des points existants.
    Micrometers overlap{300};
    Micrometers flatten_tolerance{20};
    std::size_t max_cells{4'000'000}; // au-delà, le pas double (déterministe)
};

// `vector_ids` : objets vectoriels du bloc (les broderies de surface qui les suivent, et les
// satins à rails qui en sont issus, définissent l'emprise). Erreur si le bloc n'a aucune surface.
// Résultat : poches triées (aire décroissante, puis position), éventuellement vide.
[[nodiscard]] Result<std::vector<geometry::PathSet>>
find_uncovered_pockets(const document::Project& project, const stitch::StitchSequence& sequence,
                       const std::vector<ObjectId>& vector_ids,
                       const UncoveredPocketOptions& options = {});

} // namespace openstitch::stitch_analysis
