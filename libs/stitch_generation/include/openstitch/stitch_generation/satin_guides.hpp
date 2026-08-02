// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "openstitch/document/project.hpp"

namespace openstitch::stitch_generation {

enum class SatinGuideSide { RailA, RailB };

struct SatinGuideInsertion {
    document::SatinRung guide{};
    std::size_t index{};
};

struct SatinJunctionGuideRef {
    ObjectId embroidery_id{};
    std::size_t guide_index{};
    std::uint32_t section_index{};

    constexpr bool operator==(const SatinJunctionGuideRef&) const = default;
};

// Classe tous les guides en une seule projection des rails et renvoie, pour
// chacun, la jonction structurelle éventuellement portée par le guide terminal.
// La détection repose sur les stations projetées, pas sur l'index du vecteur :
// un import aux rungs inversés/désordonnés reste donc interprété correctement.
[[nodiscard]] std::vector<std::optional<std::uint32_t>> satin_guide_junctions(
    const document::SatinParams& satin,
    Micrometers flatten_tolerance = Micrometers{100});

// Variante ponctuelle utilisée par les commandes et les actions contextuelles.
[[nodiscard]] std::optional<std::uint32_t> satin_guide_junction(
    const document::SatinParams& satin, std::size_t guide_index,
    Micrometers flatten_tolerance = Micrometers{100});

// Retrouve toutes les extrémités structurelles d'une jonction dans le réseau
// identifié par son objet vectoriel source. Le résultat est trié par section,
// identifiant d'objet puis index de guide afin que l'édition groupée soit
// déterministe indépendamment de l'ordre des objets dans le document.
[[nodiscard]] std::vector<SatinJunctionGuideRef> satin_junction_guides(
    const document::Project& project, ObjectId source_vector, std::uint32_t junction_id,
    Micrometers flatten_tolerance = Micrometers{100});

// Alloue déterministement l'identité suivante des guides liés dans un réseau.
// Aucun identifiant n'est produit sans source valide ou après saturation uint32.
[[nodiscard]] std::optional<std::uint32_t> next_satin_guide_link_id(
    const document::Project& project, ObjectId source_vector);

// Retrouve tous les guides internes partageant `link_id` dans le réseau
// identifié par `source_vector`. Comme satin_junction_guides, le résultat est
// trié (section, objet, index) pour une édition groupée déterministe et vide
// dès qu'une section porte plusieurs guides du même link_id (donnée corrompue)
// ou que la topologie est incohérente. Contrairement aux guides de jonction
// (verrouillés, un par extrémité de section), un groupe lié n'a pas besoin de
// couvrir toutes les sections du réseau — seulement celles qui touchaient la
// jonction où le groupe a été créé — donc au moins deux membres suffisent.
[[nodiscard]] std::vector<SatinJunctionGuideRef>
satin_linked_guides(const document::Project& project, ObjectId source_vector,
                    std::uint32_t link_id);

// Édition atomique d'un guide au sein d'un groupe lié (id d'objet + index +
// nouveau barreau), produite par move_satin_guide_group pour une seule section
// du groupe.
struct SatinGuideGroupEdit {
    ObjectId embroidery_id{};
    std::size_t guide_index{};
    document::SatinRung guide{};
};

// Déplace longitudinalement tout un groupe de guides liés en préservant, pour
// chaque section, sa position normalisée entre le guide de jonction voisin et
// son propre voisin suivant (distance normalisée à la jonction partagée). Le
// point `desired` est projeté UNIQUEMENT sur le rail `dragged_side` de la
// section `dragged_id` : c'est la seule donnée géométrique brute qui traverse
// une frontière de section, convertie immédiatement en un delta normalisé
// partagé — jamais une coordonnée ou un angle recopié d'une section à l'autre.
// Tout ou rien : nullopt si une section n'est plus adjacente à sa jonction
// (topologie modifiée entretemps), si le delta ferait sortir un guide de son
// intervalle local, ou si l'espacement minimal (densité) ne serait plus
// respecté sur une section — jamais de mutation partielle.
[[nodiscard]] std::optional<std::vector<SatinGuideGroupEdit>>
move_satin_guide_group(const document::Project& doc, ObjectId source_vector, std::uint32_t link_id,
                       ObjectId dragged_id, SatinGuideSide dragged_side, Vec2um desired,
                       Micrometers flatten_tolerance = Micrometers{100});

// Projette l'extrémité déplacée sur son rail et refuse une disposition qui ne
// progresse plus strictement sur les deux rails. Le même demi-pas de densité
// que fill_satin_columns est utilisé : une poignée acceptée ne pourra donc pas
// être fusionnée/rejetée silencieusement lors de la génération. Un guide
// terminal attaché à une jonction est structurel et ne peut pas être déplacé.
[[nodiscard]] std::optional<document::SatinRung> move_satin_guide_endpoint(
    const document::SatinParams& satin, std::size_t guide_index, SatinGuideSide side,
    Vec2um desired, Micrometers flatten_tolerance = Micrometers{100});

// Construit un guide au milieu du plus grand intervalle médian entre guides
// existants. L'index retourné est celui d'insertion dans SatinParams::rungs.
[[nodiscard]] std::optional<SatinGuideInsertion> make_satin_guide_in_largest_gap(
    const document::SatinParams& satin,
    Micrometers flatten_tolerance = Micrometers{100});

// Construit un guide dans l'intervalle directement adjacent à un guide
// structurel de jonction. L'intervalle doit progresser strictement sur les deux
// rails et conserver le même demi-pas minimal que le générateur.
[[nodiscard]] std::optional<SatinGuideInsertion> make_satin_guide_next_to_junction(
    const document::SatinParams& satin, std::size_t junction_guide_index,
    Micrometers flatten_tolerance = Micrometers{100});

}  // namespace openstitch::stitch_generation
