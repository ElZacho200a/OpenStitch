// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/boundary_blend.hpp"

namespace openstitch::project_ops {

// PROPOSITION de fondus à la frontière depuis l'image source (docs/studio-fondu-frontiere-spec.md,
// lot L5). Pour chaque couple de zones voisines qui peuvent recevoir un fondu, l'image est lue
// en travers de leur frontière (profils perpendiculaires, un tous les `step` le long de la
// frontière) : quand la couleur passe de l'une à l'autre en douceur (largeur 10 %-90 % de la
// transition au moins `min_transition`), un fondu est proposé, ses demi-bandes reprenant la
// largeur mesurée de chaque côté. Une limite nette dans l'image ne donne rien.
struct BoundaryBlendProposalOptions {
    Micrometers min_transition{1'500}; // transition plus étroite : bord net, pas de fondu
    Micrometers step{1'000};           // espacement des profils le long de la frontière
    Micrometers reach{6'000};          // longueur lue de chaque côté de la frontière
    double min_contrast{30.0};         // écart de couleur minimal entre les deux côtés (RVB)
    double min_valid_share{0.5};       // part des profils exploitables pour conclure
};

struct BoundaryBlendProposal {
    ObjectId a;
    ObjectId b;
    BoundaryBlendOptions options;  // largeurs proposées (côté a, côté b), courbe linéaire
    double transition_mm{0.0};     // largeur médiane 10 %-90 % de la transition
    std::size_t profiles{0};       // profils exploitables
    std::size_t profiles_total{0}; // profils lus
};

// Propositions triées par transition décroissante puis identifiants (déterministe). Une zone
// n'apparaît que dans une proposition (la plus large). Couples déjà liés : ignorés. Erreur
// E_BLEND_NO_IMAGE si le projet n'a pas d'image source.
[[nodiscard]] Result<std::vector<BoundaryBlendProposal>>
propose_boundary_blends(const document::Project& project,
                        const BoundaryBlendProposalOptions& options = {});

// Applique toutes les propositions en UNE commande (un pas d'annulation). Erreur
// E_BLEND_NO_PROPOSAL si la liste est vide.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_apply_boundary_blend_proposals_command(const document::Project& project,
                                            const std::vector<BoundaryBlendProposal>& proposals);

} // namespace openstitch::project_ops
