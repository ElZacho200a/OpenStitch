// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::formats {

// Import de dessin vectoriel SVG comme objets vectoriels éditables --
// permet de sauter entièrement l'image/la segmentation/la vectorisation
// quand l'utilisateur possède déjà le tracé (Illustrator/Inkscape/export
// CAO), demande explicite utilisateur (2026-09-11 : "évite la
// segmentation"). Portée "basique mais solide" (analyse préalable,
// jamais l'ensemble de la spécification SVG) :
//
//   - Éléments de forme : <path> (grammaire de commandes complète : M/L/H/V/
//     C/S/Q/T/A/Z, absolu/relatif, répétition implicite), <rect>, <circle>,
//     <ellipse>, <line>, <polyline>, <polygon>.
//   - <g> et transformations composées (translate/scale/rotate/matrix/
//     skewX/skewY), imbrication quelconque.
//   - Courbes cubiques/quadratiques converties EXACTEMENT en tan_in/tan_out
//     (`geometry::PathNode` supporte déjà nativement ce modèle -- aucune
//     perte). Arcs elliptiques (A) et cercles/ellipses aplatis en arcs de
//     Bézier cubiques standards (approximation visuelle usuelle, jamais un
//     arc paramétrique natif -- `geometry::Path` n'en a pas).
//   - Trous : les sous-chemins d'un même élément sont reconstruits en
//     hiérarchie extérieur/trous via `geometry::clean_to_path_sets`
//     (réutilisé tel quel, jamais réimplémenté) -- couvre le cas courant
//     (un <path> avec plusieurs sous-tracés, orientations opposées),
//     PAS la détection de trou entre éléments <path> distincts qui se
//     chevauchent (hors de portée "basique").
//   - Unités/taille physique : dérivée directement des attributs `width`/
//     `height`/`viewBox` de la racine <svg> (mm/cm/in/pt/pc explicites, ou
//     96 px/pouce par défaut si sans unité -- valeur normative CSS/SVG,
//     jamais une supposition arbitraire). Contenu centré sur l'origine du
//     canevas (cohérent avec `Vec2um` : "origine au centre du canevas").
//
// Diagnostiqué EXPLICITEMENT (jamais ignoré en silence, contrairement au
// décodeur DXF) mais non rendu : <text>, dégradés, filtres, `clip-path`,
// motifs (`pattern`), `use`/`symbol`, styles CSS externes/imbriqués au-delà
// de `fill`/`stroke` inline basique. Chaque diagnostic nomme l'élément et
// sa position dans le document.
struct SvgImportResult {
    // Un groupe par élément de forme top-level rencontré (après aplatissement
    // des <g>) -- peut contenir plusieurs PathSet si l'élément se sépare en
    // morceaux disjoints une fois nettoyé (`clean_to_path_sets`).
    std::vector<geometry::PathSet> objects;
    // Fonctionnalités rencontrées mais non prises en charge -- JAMAIS un
    // échec de l'import global (§ tolérance, même principe que le décodeur
    // DXF), mais toujours signalé explicitement à l'appelant.
    std::vector<std::string> warnings;
};

[[nodiscard]] Result<SvgImportResult> decode_svg(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<SvgImportResult> read_svg_file(const std::filesystem::path& path);

} // namespace openstitch::formats
