// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/satin.hpp"

namespace openstitch::satin_coverage {

// Entrée minimale décrivant une colonne satin pour l'analyseur : uniquement
// ce qui définit sa géométrie STRUCTURELLE (rails + barreaux), jamais un type
// interne d'`auto_satin` -- l'analyseur reste un observateur indépendant du
// générateur, utilisable aussi bien depuis `autodigitize` que depuis un test
// synthétique. L'appelant adapte trivialement `SatinColumnGeometry`/
// `ParametricSatinObject` vers cette entrée (copie de champs, aucun calcul).
struct SatinColumnInput {
    geometry::Path rail_a;
    geometry::Path rail_b;
    std::vector<stitch_generation::SatinRungSeg> rungs;
    Micrometers density{400};  // pas d'échantillonnage des stations (0,4 mm par défaut)
};

// Une composante connexe de la zone NON couverte par les colonnes -- vient
// directement d'un élément du `vector<PathSet>` renvoyé par la différence
// booléenne (chaque élément EST déjà une composante connexe, cf.
// `geometry::difference_polygons`) : aucun calcul de composantes connexes
// séparé n'est nécessaire ici.
struct MissingRegion {
    geometry::PathSet region;
    double area_mm2{0.0};
    double area_ratio{0.0};  // fraction de la surface cible totale
    Vec2um centroid{};       // barycentre des sommets du contour extérieur (approximatif)
    Vec2um bbox_min{};
    Vec2um bbox_max{};
    // Rayon (mm) du plus grand disque inscrit dans cette région, obtenu par
    // recherche binaire d'érosions successives (`geometry::inset_path_set`,
    // déjà Clipper2, déjà testé) -- caractérise la PROFONDEUR du trou, pas
    // seulement sa surface : une frange fine de plusieurs mm² le long d'un
    // bord n'est pas aussi grave qu'une poche ronde de même aire en plein
    // milieu. Diagnostic vectoriel continu, jamais un raster.
    double max_gap_radius_mm{0.0};
};

// Seuils explicites, centralisés, documentés -- aucun coefficient magique
// dans `analyze_satin_coverage` lui-même. Valeurs par défaut raisonnables
// pour démarrer, à ajuster après expérimentation (§13 : ne jamais considérer
// 99,5 % comme une constante universelle du métier).
struct SatinCoverageConfig {
    double min_raw_coverage{0.90};
    double min_core_coverage{0.995};
    double max_largest_missing_area_mm2{1.0};
    double max_gap_radius_mm{0.5};
    double max_outside_ratio{0.05};

    // Érosion de la cible avant calcul de `core_coverage_ratio` : absorbe les
    // approximations numériques/de discrétisation le long du bord (une frange
    // de quelques dizaines de µm n'est pas un vrai défaut). Sur une forme
    // trop fine pour survivre à cette érosion (`Pcore` vide),
    // `core_coverage_ratio` retombe sur `raw_coverage_ratio` plutôt que de
    // produire une division par zéro / un résultat non défini.
    double boundary_tolerance_mm{0.1};

    // Résolution de la recherche binaire par érosions successives utilisée
    // pour `max_gap_radius_mm` (précision du diagnostic, pas un pas de
    // raster -- cette bibliothèque n'a aucune dépendance OpenCV).
    double gap_radius_resolution_mm{0.02};
};

struct SatinCoverageReport {
    double target_area_mm2{0.0};

    double covered_area_mm2{0.0};  // aire(P ∩ C)
    double missing_area_mm2{0.0};  // aire(P \ C)
    double outside_area_mm2{0.0};  // aire(C \ P)

    // Géométrie exacte des trois régions (P ∩ C, C \ P ; la géométrie de
    // `P \ C` est déjà portée par `missing_regions[i].region`) -- conservée
    // pour la visualisation de debug (`coverage_to_svg`) et tout diagnostic
    // ultérieur, jamais recalculée par l'appelant.
    std::vector<geometry::PathSet> covered_regions;
    std::vector<geometry::PathSet> outside_regions;

    double raw_coverage_ratio{0.0};   // covered / target
    double core_coverage_ratio{0.0};  // 1 - missing(Pcore) / area(Pcore)
    double outside_ratio{0.0};        // outside / target

    std::vector<MissingRegion> missing_regions;  // triées par aire décroissante
    double largest_missing_area_mm2{0.0};
    double largest_missing_ratio{0.0};
    double max_gap_radius_mm{0.0};  // maximum sur toutes les régions manquantes

    // Intervalles entre deux stations consécutives d'une même colonne rejetés
    // car géométriquement dégénérés (rail A et rail B qui se croisent) --
    // l'appariement ladder amont (`stitch_generation::satin_stations`)
    // garantit déjà l'absence de croisement de ce type, mais cet analyseur
    // reste un observateur INDÉPENDANT : il ne fait jamais confiance
    // aveuglément à cette garantie amont, et compte plutôt que de masquer
    // silencieusement un intervalle suspect dans l'union.
    std::size_t degenerate_interval_count{0};

    bool passed{false};

    // Message texte prêt à afficher/logger (diagnostic exploitable, jamais un
    // simple "Couverture insuffisante.") -- toujours rempli, y compris quand
    // `passed` est vrai.
    std::string diagnostic;
};

// Calcule la couverture géométrique d'une région cible par un ensemble de
// colonnes satin : compare la SURFACE balayée par les quadrilatères
// consécutifs de chaque colonne (jamais une heuristique sur le squelette, le
// nombre de branches/stations ou la longueur des rails) à la surface de la
// région cible. N'utilise que la géométrie STRUCTURELLE des colonnes (rails +
// barreaux, via `stitch_generation::satin_stations`) : jamais les points de
// couture finaux (compensation, split-stitch, terminaisons), qui répondent à
// un besoin de couture, pas à la question structurelle « les rails
// couvrent-ils la forme ? ». N'altère ni `target`, ni `columns`. Chaque
// colonne contribue ses propres quadrilatères (décomposés en deux triangles
// chacun, pour rester robuste à un quadrilatère non convexe) ; deux colonnes
// distinctes ne sont jamais reliées par un quadrilatère artificiel, et une
// rupture de ruban (`SatinStation::jump_before`, cf. audit lettres) coupe la
// colonne en deux segments de couverture indépendants plutôt que d'être
// pontée.
[[nodiscard]] Result<SatinCoverageReport> analyze_satin_coverage(
    const geometry::PathSet& target, const std::vector<SatinColumnInput>& columns,
    const SatinCoverageConfig& config = {});

// SVG de diagnostic (coordonnées en millimètres, même convention que
// `auto_satin::debug_export`) superposant : forme cible (contour gris),
// couverture (remplissage vert), zones manquantes (remplissage rouge, une
// composante à la fois -- `fill-rule="evenodd"` pour respecter les trous de
// chaque région), zones hors forme (remplissage orange), rails de chaque
// colonne (lignes bleu/orange fines) et leurs stations structurelles
// (points). Un commentaire d'en-tête résume `report` en texte, lisible sans
// rendu graphique. Ne modifie ni `target`, ni `columns`, ni `report`.
[[nodiscard]] std::string coverage_to_svg(const geometry::PathSet& target,
                                          const std::vector<SatinColumnInput>& columns,
                                          const SatinCoverageReport& report);

}  // namespace openstitch::satin_coverage
