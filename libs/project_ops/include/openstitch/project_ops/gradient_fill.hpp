// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/document/multi_blend.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::project_ops {

// Remplissage dégradé LOGIQUE d'une forme : un seul objet pour l'utilisateur, deux
// couches de broderie pour la machine (fond + dessus, reliées par `ColorBlendLink`).
// Aucune donnée nouvelle n'est persistée : la description est relue des deux couches
// (couleurs, `DensityGradient` de la couche de dessus, profil complémentaire), donc les
// anciens fondus sont rééditables tels quels. Source UNIQUE de la construction des
// couches (aperçu, création, réédition, poignées du canevas) : `make_color_blend_layers`.
struct GradientFillSpec {
    using Rgb = std::array<std::uint8_t, 3>;
    Rgb background{};
    Rgb foreground{};
    std::optional<thread_palette::ThreadKey> background_thread;
    std::optional<thread_palette::ThreadKey> foreground_thread;
    // Gradient de la couche de DESSUS : axe `from` → `to` (repère modèle), écarts, courbe.
    document::DensityGradient gradient;
    // Vrai : rampes de densité opposées (fond ET dessus). Faux : fond uniforme.
    bool complementary{true};

    bool operator==(const GradientFillSpec&) const = default;
};

// Portion de l'étendue de la forme (mesurée le long de l'axe) où la rampe varie :
// 0 = bord de la forme côté `from`, 1 = bord opposé. Au-delà : aplats.
struct GradientZone {
    double start{0.0};
    double end{1.0};
};

// Une des deux couches reconnue comme dégradé valide (liens réciproques, même source).
[[nodiscard]] std::optional<document::ColorBlendLayers>
gradient_layers(const document::Project& project, ObjectId layer);

// Relit le dégradé complet depuis l'une ou l'autre des deux couches.
[[nodiscard]] std::optional<GradientFillSpec>
describe_gradient_fill(const document::Project& project, ObjectId layer);

// Zone de transition actuelle de `gradient` sur `shape`. nullopt : axe dégénéré/forme vide.
[[nodiscard]] std::optional<GradientZone> gradient_zone(const std::vector<geometry::PathSet>& shape,
                                                        const document::DensityGradient& gradient);

// `gradient` dont les extrémités sont replacées sur `zone` (l'axe garde sa direction et
// sa droite support). nullopt si la zone est invalide (start >= end, hors [0 ; 1]) ou
// l'axe dégénéré. Pure.
[[nodiscard]] std::optional<document::DensityGradient>
gradient_with_zone(const std::vector<geometry::PathSet>& shape,
                   const document::DensityGradient& gradient, GradientZone zone);

// Poignées d'édition de l'axe sur le canevas. `Start`/`End` : déplacent une extrémité de la
// transition le long de l'axe (Maj : l'autre bouge symétriquement autour du centre) ;
// `Move` : translate l'axe entier (la transition reste dans la forme) ; `Rotate` : tourne
// l'axe autour de son centre vers `pointer` (la poignée est à 90° de l'axe), en gardant la
// zone de transition. Résultat prêt pour `make_edit_gradient_fill_command` ; pur, sans
// document. nullopt pour un axe dégénéré ou une forme vide.
enum class GradientHandle { Start, End, Move, Rotate };

[[nodiscard]] std::optional<document::DensityGradient>
drag_gradient_handle(const std::vector<geometry::PathSet>& shape,
                     const document::DensityGradient& gradient, GradientHandle handle,
                     Vec2um pointer, bool symmetric = false);

// Dégradé de départ utilisable sans réglage : deux couleurs distinctes, transition sur
// toute la forme perpendiculairement aux rangées (axe horizontal), profil complémentaire.
// nullopt si la couche n'est pas un remplissage ou si sa forme est vide/dégénérée.
[[nodiscard]] std::optional<GradientFillSpec>
default_gradient_fill(const document::Project& project, ObjectId fill_layer);

// Raison courte et affichable pour laquelle `layer` ne peut pas devenir un dégradé
// (satin, contour, section multiple, déjà un dégradé…). Vide si compatible.
[[nodiscard]] std::string gradient_incompatibility(const document::Project& project,
                                                   ObjectId layer);

// Convertit un remplissage uni en dégradé : UN pas d'annulation. La couche existante
// devient le fond, la couche de dessus est insérée juste après elle. `second_fill`
// (fondre deux zones) : couche de l'autre zone, déjà supprimée par l'union que
// l'appelant place AVANT cette commande ; seul son modèle sert de gabarit au dessus.
// Alloue l'id de la couche de dessus dans `project`.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>> make_create_gradient_fill_command(
    document::Project& project, ObjectId fill_layer, const GradientFillSpec& spec,
    const std::vector<geometry::PathSet>& shape,
    const document::EmbroideryObject* second_fill = nullptr, ObjectId removed_layer = {});

// Remplace le dégradé existant : mêmes deux couches, mêmes ids et même rang de couture,
// aucune création. UN pas d'annulation. Erreur si `layer` n'est pas un dégradé valide ou
// si le spec est refusé par le moteur (même verdict que l'aperçu).
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_edit_gradient_fill_command(const document::Project& project, ObjectId layer,
                                const GradientFillSpec& spec);

// Retour à un remplissage uni (couleur du fond) : supprime la couche de dessus, délie et
// retire la rampe du fond. UN pas d'annulation.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_remove_gradient_fill_command(const document::Project& project, ObjectId layer);

// Camaïeu réellement multicolore : une forme logique, 2..8 fils cousus dans
// l'ordre de la liste. Les densités par couche sont des courbes en cloche
// piecewise-linéaires, dont la somme vaut 1 en mode complémentaire.
struct MultiGradientFillSpec {
    using Rgb = std::array<std::uint8_t, 3>;
    std::vector<Rgb> colors;
    document::DensityGradient gradient;
    // Profil de chaque fil indépendant (comme Multi Blend). Vide = profil
    // commun historique ; sinon un profil par couche physique.
    std::vector<document::DensityCurve> layer_curves;
    bool complementary{true};
};

// Relit N fils et leur axe depuis un membre quelconque du groupe.
[[nodiscard]] std::optional<MultiGradientFillSpec>
describe_multicolor_gradient_fill(const document::Project& project, ObjectId layer);

// Créer, rééditer (également convertir un ancien fondu à deux fils) et
// supprimer : chaque opération produit UNE commande annulable, pas une
// succession de fondus binaires. L'allocation d'IDs utilise le compteur
// documentaire habituel.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_create_multicolor_gradient_fill_command(document::Project& project, ObjectId fill,
                                             const MultiGradientFillSpec& spec);

[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_edit_multicolor_gradient_fill_command(document::Project& project, ObjectId member,
                                           const MultiGradientFillSpec& spec);

[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_remove_multicolor_gradient_fill_command(const document::Project& project, ObjectId member);

// Fondu multicolore AUTONOME (mission 2026-11-14) : spec complète d'un objet ancre
// (`document::MulticolorGradientParams`), méthode EXPLICITE -- contrairement à
// `MultiGradientFillSpec` qui la devine depuis `gradient.texture` pour la compatibilité des
// fonctions historiques ci-dessus (désormais de simples adaptateurs vers celles-ci).
struct MulticolorGradientSpec {
    document::GradientMethod method{document::GradientMethod::Directional};
    std::vector<document::GradientThread> palette; // 2..8, de l'arrière vers l'avant
    document::DensityGradient transition;
    bool complementary{true};
    Micrometers satin_density{400}; // SatinInterlaced uniquement
    int crosshatch_layers{2};       // CrossHatching uniquement
    Angle crosshatch_angle{0.0};    // CrossHatching uniquement

    bool operator==(const MulticolorGradientSpec&) const = default;
};

// Relit la spec complète depuis l'ancre OU n'importe quelle couche du groupe.
[[nodiscard]] std::optional<MulticolorGradientSpec>
describe_multicolor_gradient(const document::Project& project, ObjectId anchorOrMember);

// Raison courte et affichable pour laquelle `fill` ne peut pas devenir un fondu multicolore
// autonome (déjà un fondu, source introuvable, type incompatible…). Vide si compatible. Pour
// `SatinInterlaced` sans rails valides sur `fill`, compatible quand même : les rails sont dérivés
// automatiquement du contour source (`stitch_generation::rails_from_contour`) -- refusé seulement
// si CE contour est trop petit/dégénéré pour en tirer deux rails (forme compacte, pas allongée).
[[nodiscard]] std::string multicolor_gradient_incompatibility(const document::Project& project,
                                                              ObjectId fill,
                                                              document::GradientMethod method);

// Crée l'ancre et les `palette.size()` couches physiques, UN pas d'annulation. `fill` devient
// l'ancre (même id, conserve sa position) ; les couches sont de nouveaux objets insérés juste
// après. Changer `method` dans un `make_edit_...` ultérieur régénère les couches sans toucher
// `palette` ni `transition`.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_create_multicolor_gradient_command(document::Project& project, ObjectId fill,
                                        const MulticolorGradientSpec& spec);

[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_edit_multicolor_gradient_command(document::Project& project, ObjectId anchorOrMember,
                                      const MulticolorGradientSpec& spec);

[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_remove_multicolor_gradient_command(const document::Project& project, ObjectId anchorOrMember);

} // namespace openstitch::project_ops
