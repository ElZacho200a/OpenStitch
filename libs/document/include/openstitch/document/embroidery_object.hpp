// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::document {

// Style du point de contour (HP-STI-001). `Run` : comportement historique piloté par
// `repeats` (1 simple, 2 aller-retour, 3 triple). Les autres styles ignorent `repeats`.
enum class RunningStyle : std::uint8_t {
    Run,        // point droit, `repeats` passages
    Bean,       // point « bean » : chaque intervalle cousu 3 fois
    Backstitch, // point arrière : chaque point revient en arrière
    Stem,       // point tige : pénétrations décalées latéralement (aspect torsadé)
};

// Paramètres du point droit de contour (§5.1).
struct RunningStitchParams {
    Micrometers stitch_length{3'000}; // longueur cible (3 mm)
    Micrometers min_length{500};      // en dessous, les points sont fusionnés
    int repeats{1};                   // 1 simple, 2 aller-retour, 3 point triple
    RunningStyle style{RunningStyle::Run};
    Micrometers stem_offset{300}; // décalage latéral du point tige (0,3 mm)

    bool operator==(const RunningStitchParams&) const = default;
};

// Choix de la sous-couche (HP-ENG-002). `Manual` (défaut, comportement historique) :
// les réglages `underlay_*` de l'objet s'appliquent tels quels. `Auto` : le moteur
// choisit contour / rangées perpendiculaires / centre / zigzag selon le type, la
// taille et la largeur de la forme (`stitch_generation::resolve_*_underlay`), les
// réglages manuels étant alors ignorés. N'agit qu'à la génération.
enum class UnderlayMode : std::uint8_t { Manual, Auto };

// Choix de la compensation du tirage (HP-ENG-001.b). `Manual` (défaut, comportement
// historique) : `pull_compensation` s'applique tel quel. `Auto` : le moteur calcule la
// compensation selon la taille et la largeur de la forme (`stitch_generation::
// resolve_pull_compensation`), le réglage manuel étant alors ignoré. `Adaptive`
// (AdaptivePullCompensation, niveau 1 : géométrique) : la compensation est calculée
// LOCALEMENT (par station de satin, par rangée de remplissage) avec lissage et bornes de
// sécurité ; `Auto` reste la règle historique à largeur moyenne, inchangée. `Predicted`
// (niveau 3) : le déplacement du tissu est prédit par un CHAMP sur tous les segments cousus voisins
// (`stitch_generation::PullField`, anisotrope, avec confiance) et appliqué en passe finale de
// `effective_sequence`, EN PLUS de la compensation locale Adaptive. N'agit qu'à la
// génération. Valeurs persistées : ne pas réordonner.
enum class PullMode : std::uint8_t { Manual, Auto, Adaptive, Predicted };

// Paramètres du remplissage tatami (§5.4, §15).
struct TatamiParams {
    Angle angle{0.0};                 // orientation des rangées (radians)
    Micrometers row_spacing{400};     // écart entre rangées (densité) — 0,4 mm
    Micrometers stitch_length{3'000}; // longueur de point le long d'une rangée
    Micrometers inset{200};           // retrait du bord (compensation de contour)
    // Rangées avant répétition de la phase des pénétrations. 3 (et non 2) : à 2, les
    // pénétrations d'une rangée sur deux s'alignent en colonnes et la pièce brodée
    // paraît « pointillée » ; 3 répartit les trous en briques (décalage de 1/3).
    int stagger{3};
    // Tatami avancé (Lot 7) — désactivé par défaut, rétrocompatible.
    bool underlay_edge{false};           // sous-couche de contour (running rentré)
    bool underlay_parallel{false};       // sous-couche de rangées perpendiculaires espacées
    Micrometers underlay_inset{600};     // retrait de la sous-couche de contour
    Micrometers underlay_spacing{2'000}; // écart des rangées de sous-couche
    bool hidden_underpath{false}; // liaisons cousues cachées (au lieu de sauts) si trajet valide
    std::optional<Vec2um> entry_point; // démarre le remplissage près de ce point
    // HP-ENG-001 : compensation du tirage. Chaque rangée dépasse du contour de cette
    // longueur DANS L'AXE DU FIL (le fil tire dans sa direction : la forme cousue
    // rétrécit dans ce sens), 0 = aucune. Plage [0 ; 3] mm, 0,2 à 0,4 mm typique.
    Micrometers pull_compensation{0};
    PullMode pull_mode{PullMode::Manual};             // HP-ENG-001.b
    UnderlayMode underlay_mode{UnderlayMode::Manual}; // HP-ENG-002
    // HP-ENG-002 : sous-couche en zigzag (rangées diagonales à 45° des rangées de la couche
    // supérieure, espacées de `underlay_spacing`) ; croisée avec les rangées perpendiculaires,
    // elle stabilise un grand aplat. Faux = aucune (historique). Choisie par le mode Auto.
    bool underlay_zigzag{false};
    // Bords « effilochés » : sur un bord presque parallèle aux rangées, la fin de rangée saute
    // d'une rangée à l'autre et des pointes isolées dépassent. `row_end_clip` > 0 rabat toute
    // extrémité qui dépasse de plus de cette longueur les extrémités des deux rangées voisines
    // (écrêtage, dans l'axe du fil). 0 = aucun (historique). Plage [0 ; 5] mm, ~1 mm typique.
    Micrometers row_end_clip{0};
    // Passe de contour cousue PAR-DESSUS le remplissage (point droit le long du bord et des
    // trous) : masque les bouts de rangées. Faux = aucune (historique).
    bool edge_outline{false};

    bool operator==(const TatamiParams&) const = default;
};

// Profil de la rampe d'un `DensityGradient` : remappe la position t ∈ [0 ; 1] sur
// l'axe avant l'interpolation des écarts. `Linear` (défaut, comportement
// historique) : t. `Smooth` : 3t² − 2t³ (départ et arrivée adoucis). `Steep` :
// 6t⁵ − 15t⁴ + 10t³ (aplats plus longs, transition plus resserrée au milieu).
// Les trois valent 0, 0,5 et 1 en t = 0, 0,5 et 1. Valeurs persistées : ne pas réordonner.
enum class DensityCurve : std::uint8_t { Linear = 0, Smooth = 1, Steep = 2 };

// Dégradé de densité d'un remplissage directionnel : l'écart entre lignes de
// couture varie le long de l'axe `from` → `to` (repère du modèle,
// comme les guides), de `spacing_from` à `spacing_to`, et reste constant de
// part et d'autre des extrémités. `from == to` : axe dégénéré, espacement
// uniforme `spacing_from`. Écart borné à la génération (cf. directional_fill).
struct DensityGradient {
    Vec2um from{};
    Vec2um to{};
    Micrometers spacing_from{400}; // 0,4 mm
    Micrometers spacing_to{400};   // 0,4 mm
    // Faux : interpolation linéaire de l'écart (historique). Vrai : interpolation
    // linéaire de son inverse (densité), pour des rampes de couleurs complémentaires.
    bool interpolate_density{false};
    DensityCurve curve{DensityCurve::Linear};

    bool operator==(const DensityGradient&) const = default;
};

// Paramètres du remplissage DIRECTIONNEL (passé empiétant / peinture à
// l'aiguille, cf. docs/source/directional-fill.md). Contrairement au tatami
// (rangées droites à angle fixe), les points suivent un CHAMP de directions
// interpolé depuis des courbes guides : les lignes de couture épousent la
// forme. Comme le tatami, la région remplie est celle de l'objet vectoriel
// source ; les guides et lignes de rupture sont exprimés en µm dans le MÊME
// repère que ce contour (repère du modèle) et suivent la forme quand elle est
// déplacée ou redimensionnée (cf. TranslateVectorObjectCommand).
struct DirectionalFillParams {
    // Courbes guides (polylignes ou Béziers, ouvertes) : leurs tangentes
    // orientent le fil. Vide = repli sur l'axe principal de la forme.
    std::vector<geometry::Path> guides;
    // Lignes de rupture (Phase 2) : découpent la forme en SECTEURS dont les
    // champs sont calculés indépendamment (chevrons). Une rupture doit
    // traverser la forme (bord à bord ou jusqu'à une autre rupture).
    std::vector<geometry::Path> break_lines;
    Micrometers row_spacing{400}; // écart entre lignes de couture (densité) — 0,4 mm
    // Dégradé de densité : si présent, il remplace `row_spacing` pour le tracé
    // des lignes (`row_spacing` reste la référence des seuils de parcours).
    std::optional<DensityGradient> density_gradient;
    // Régularité de l'espacement, [0 ; 1] : 0 = tracé brut (comportement
    // historique, défaut) ; au-delà, les lignes sont déplacées par une
    // résolution quadratique qui égalise l'écart entre lignes voisines aux
    // dépens, au besoin, de leur direction d'origine (1 = espacement d'abord).
    double spacing_regularity{0.0};
    Micrometers stitch_length{3'000}; // longueur cible, bornée à [1 ; 7] mm à la génération
    double edge_weight{0.0};          // influence de la tangente du bord le plus proche, [0 ; 1]
    Micrometers inset{200};           // retrait du bord (compensation de contour)
    int stagger{2};                   // lignes avant répétition de la phase des pénétrations
    // Sous-couches (mêmes réglages et mêmes générateurs que le tatami).
    bool underlay_edge{false};       // contour rentré
    bool underlay_parallel{false};   // rangées droites perpendiculaires à la direction moyenne
    Micrometers underlay_inset{600}; // retrait de la sous-couche de contour
    Micrometers underlay_spacing{2'000}; // écart des rangées de sous-couche
    bool hidden_underpath{true}; // liaisons cousues cachées (au lieu de sauts) si trajet valide
    Micrometers sector_overlap{250}; // chevauchement le long des ruptures (Phase 2)
    // Aspect « fait main » (Phase 3) : longueurs irrégulières, pénétrations
    // imbriquées, légère ondulation de la direction. Pseudo-aléatoire à graine
    // FIXE (`seed`, stockée dans l'objet) : même projet, même résultat.
    bool handmade{false};
    int handmade_intensity{50}; // 0 à 100 %
    std::uint32_t seed{0};
    UnderlayMode underlay_mode{UnderlayMode::Manual}; // HP-ENG-002
    bool underlay_zigzag{false};                      // comme `TatamiParams::underlay_zigzag`
    // Passe de contour cousue PAR-DESSUS le remplissage (comme `TatamiParams::edge_outline`) :
    // masque les bouts de lignes de couture qui effilochent le bord. Faux = aucune (historique).
    bool edge_outline{false};
    // HP-ENG-001 : compensation du tirage. Chaque ligne de couture est prolongée à ses deux
    // extrémités de cette longueur, le long de sa tangente de bout (direction du fil). 0 =
    // aucune (historique). Plage [0 ; 3] mm.
    Micrometers pull_compensation{0};
    PullMode pull_mode{PullMode::Manual}; // HP-ENG-001.b

    bool operator==(const DirectionalFillParams&) const = default;
};

// Barreau (rung) : segment transversal reliant les deux rails, qui les découpe
// en intervalles correspondants. Produit par l'auto-satin (squelette) ; stocké
// dans le document (éditable, sérialisé), pas seulement utilisé à la génération.
struct SatinRung {
    Vec2um a{}; // côté rail A
    Vec2um b{}; // côté rail B
    // Identité optionnelle, locale au réseau `source_vector`, partagée par les
    // guides créés ensemble dans plusieurs sections. Absente pour les projets
    // historiques et les guides indépendants.
    std::optional<std::uint32_t> link_id;
    constexpr bool operator==(const SatinRung&) const = default;
};

// Identité d'une section dans un réseau satin décomposé. Les identifiants de
// jonction sont locaux au réseau porté par `source_vector` ; leur absence
// signifie une extrémité libre. Le bloc entier est optionnel afin que les
// colonnes historiques à deux rails restent strictement valides.
struct SatinSectionTopology {
    std::uint32_t section_index{}; // zéro-based, déterministe dans le réseau
    std::uint32_t section_count{1};
    std::optional<std::uint32_t> start_junction;
    std::optional<std::uint32_t> end_junction;

    constexpr bool operator==(const SatinSectionTopology&) const = default;
};

// Modes du satin (mêmes valeurs/ordre que `stitch_generation::*`, cf. generate).
enum class SatinShortStitch { Disabled, RemoveAndRedistribute, SingleInset, MultiLevelInset };
enum class SatinSplit { Disabled, Simple, Staggered, DeterministicJitter };
enum class SatinCap { Flat, Rounded, Tapered, Automatic };
enum class SatinLock { None, BackAndForth, Triangle, MicroZigzag };
// Coins vifs de la colonne (HP-ENG-005) : onglet (une traversée en bissectrice), chevauchement
// (les deux branches se croisent sans traversée intermédiaire) ou coupe (saut au coin).
enum class SatinCorner : std::uint8_t { None, Miter, Overlap, Cut };

// Origine « satin de bordure » (HP-STI-004) : un satin de largeur fixe suivant un
// contour. Les rails/barreaux de `SatinParams` restent la vérité de la génération ;
// cette spécification ne sert qu'à les REGÉNÉRER depuis le contour source quand
// l'utilisateur change la largeur, le côté ou les coins dans l'inspecteur.
enum class BorderSide : std::uint8_t { Centered, Inside, Outside };
enum class BorderCorner : std::uint8_t { Sharp, Round };
struct BorderSatinSpec {
    Micrometers width{3'000};              // largeur de la colonne (3 mm), plage [0,5 ; 20] mm
    BorderSide side{BorderSide::Centered}; // côté du contour où s'étend la colonne
    BorderCorner corner{BorderCorner::Sharp};
    // Anneau suivi dans l'objet vectoriel `source_vector` (pour régénérer les rails) :
    // `path_set` = indice du PathSet, `ring` = 0 pour le contour extérieur, k pour le
    // k-ième trou (1-based).
    std::uint32_t path_set{0};
    std::uint32_t ring{0};

    bool operator==(const BorderSatinSpec&) const = default;
};

// Paramètres d'une colonne satin (§5.3). Contrairement aux autres types, le
// satin porte sa propre géométrie (deux rails éditables), car il ne se déduit
// pas d'un simple contour — il naît d'un contour découpé ou de deux chemins.
struct SatinParams {
    geometry::Path rail_a;
    geometry::Path rail_b;
    std::vector<SatinRung> rungs;                 // barreaux (vide = satin manuel/legacy)
    std::optional<SatinSectionTopology> topology; // absent = colonne indépendante/legacy
    Micrometers density{400};
    Micrometers pull_compensation{0};
    PullMode pull_mode{PullMode::Manual}; // HP-ENG-001.b
    bool center_underlay{true};
    Micrometers max_width{9'000};       // au-delà, avertissement (satin trop large -> tatami)
    Micrometers max_width_hard{48'000}; // plafond géométrique avant refus du corridor

    // Lot 3 — finitions (défauts = comportement inchangé).
    SatinShortStitch short_stitch{SatinShortStitch::Disabled};
    SatinSplit split_stitch{SatinSplit::Disabled};
    SatinCap cap_start{SatinCap::Flat};
    SatinCap cap_end{SatinCap::Flat};
    Micrometers max_stitch_length{7'000};

    // Lot 4 — sous-couches et compensation (défauts = inchangé).
    bool underlay_edge{false};   // sous-couche de bord (deux chemins internes)
    bool underlay_zigzag{false}; // sous-couche zigzag légère
    Micrometers pull_left{0};    // compensation latérale fixe (côté A)
    Micrometers pull_right{0};   // (côté B)
    Micrometers push_start{0};   // extension longitudinale au départ
    Micrometers push_end{0};     // extension longitudinale à la fin

    // Lot 5 — entrée/sortie et points de fixation (défauts = inchangé).
    SatinLock lock_start{SatinLock::None};
    SatinLock lock_end{SatinLock::None};
    Micrometers lock_length{800};
    int lock_passes{2};
    std::optional<Vec2um> entry_point; // début de couture souhaité (projeté)
    std::optional<Vec2um> exit_point;  // fin de couture souhaitée

    UnderlayMode underlay_mode{UnderlayMode::Manual}; // HP-ENG-002
    std::optional<BorderSatinSpec> border;            // HP-STI-004 : satin de bordure
    bool auto_spacing{false};                         // HP-ENG-003 : pas selon la largeur locale
    SatinCorner corner_mode{SatinCorner::None};       // HP-ENG-005 : coins vifs

    bool operator==(const SatinParams&) const = default;
};

// Guide d'orientation d'un auto-satin. Ancré GÉOMÉTRIQUEMENT (point du repère
// du modèle, comme les guides du remplissage directionnel) : il est projeté sur
// l'axe de la forme à la génération et suit la forme quand elle est déplacée ou
// redimensionnée. Aucun identifiant de branche du squelette n'est persisté (ces
// identifiants changent à chaque édition du contour).
struct AutoSatinGuide {
    Vec2um anchor{};
    // Angle modulo π. Relatif (défaut) : écart à la perpendiculaire à l'axe, 0 =
    // perpendiculaire. Absolu : angle dans le repère du modèle.
    Angle angle{0.0};
    bool absolute{false};
    // Guide en courbe (polyligne, `anchor` compris comme premier point) : courbe de référence
    // le long de laquelle les fils sont PERPENDICULAIRES à la tangente (comme sur une colonne
    // satin), pour le BALAYAGE PARALLÈLE. Vide = guide droit (comportement historique : le trait
    // est la direction des fils) ; le squelette n'utilise que `anchor` et `angle`.
    std::vector<Vec2um> path;

    bool operator==(const AutoSatinGuide&) const = default;
};

// Paramètres de l'AUTO-SATIN par squelette et traversées orientées (spec
// specs/plans/satin-squelette-traversees.md). Comme le tatami et le directionnel,
// l'objet suit la région de son objet vectoriel source : les traversées sont
// recalculées à la demande (ADR-014), jamais stockées. Aucun rail ni barreau.
// Méthode de calcul des traversées d'un auto-satin (docs/auto-satin-research.md). Les valeurs
// sont persistées par NOM dans le `.osp` (jamais par entier) ; le squelette est le défaut.
enum class AutoSatinStrategy : std::uint8_t {
    Skeleton = 0,      // squelette raster Zhang-Suen (moteur historique)
    ParallelSweep = 1, // balayage parallèle à angle constant
    MedialAxis = 2,    // axe médian vectoriel (Voronoï des segments du contour)
};

// Identifiant stable (fichier `.osp`, CLI, API programmatique) et lecture inverse. `nullopt` pour
// un nom inconnu : l'appelant refuse explicitement, il ne substitue jamais une autre méthode.
[[nodiscard]] constexpr std::string_view auto_satin_strategy_id(AutoSatinStrategy strategy) {
    switch (strategy) {
    case AutoSatinStrategy::Skeleton:
        return "skeleton";
    case AutoSatinStrategy::ParallelSweep:
        return "parallel_sweep";
    case AutoSatinStrategy::MedialAxis:
        return "medial_axis";
    }
    return "skeleton";
}

[[nodiscard]] constexpr std::optional<AutoSatinStrategy>
parse_auto_satin_strategy(std::string_view id) {
    for (const auto s : {AutoSatinStrategy::Skeleton, AutoSatinStrategy::ParallelSweep,
                         AutoSatinStrategy::MedialAxis}) {
        if (id == auto_satin_strategy_id(s)) {
            return s;
        }
    }
    return std::nullopt;
}

// Méthode proposée aux NOUVEAUX auto-satins créés par l'utilisateur (bureau, CLI, API). Ce n'est
// pas le défaut d'`AutoSatinParams{}` : le squelette y reste (anciens `.osp` sans clé `strategy`,
// lettrage, auto-numérisation), pour que rien d'existant ne change de méthode à l'insu de
// l'utilisateur.
inline constexpr AutoSatinStrategy kDefaultAutoSatinStrategy = AutoSatinStrategy::ParallelSweep;

struct AutoSatinParams {
    // Choix de l'utilisateur pour cette région. Un changement régénère le satin : annulable.
    AutoSatinStrategy strategy{AutoSatinStrategy::Skeleton};
    // Balayage parallèle seulement : angle des traversées (modèle, Y vers le haut). Absent :
    // automatique (perpendiculaire à l'axe principal de la forme).
    std::optional<Angle> sweep_angle;
    std::vector<AutoSatinGuide> guides;
    Micrometers spacing{400}; // espacement cible entre traversées (au bord le plus écarté)
    // Fractionnement des traversées longues : au-delà de `split_threshold` (Lmax),
    // la traversée est découpée en segments d'au plus `split_length` (y).
    SatinSplit split_stitch{SatinSplit::Staggered};
    Micrometers split_threshold{7'000};
    Micrometers split_length{4'000};
    // Finitions (mêmes réglages et mêmes générateurs que le satin à rails).
    SatinShortStitch short_stitch{SatinShortStitch::SingleInset};
    Micrometers pull_compensation{0};
    PullMode pull_mode{PullMode::Manual}; // HP-ENG-001.b
    bool center_underlay{true};
    bool underlay_edge{false};
    bool underlay_zigzag{false};
    Micrometers pull_left{0};
    Micrometers pull_right{0};
    Micrometers push_start{0};
    Micrometers push_end{0};
    SatinCap cap_start{SatinCap::Flat};
    SatinCap cap_end{SatinCap::Flat};
    SatinLock lock_start{SatinLock::None};
    SatinLock lock_end{SatinLock::None};
    Micrometers lock_length{800};
    int lock_passes{2};
    std::optional<Vec2um> entry_point;                // début de couture souhaité
    std::optional<Vec2um> exit_point;                 // fin de couture souhaitée
    UnderlayMode underlay_mode{UnderlayMode::Manual}; // HP-ENG-002
    // Forme compacte (disque, pétale) : axe synthétique le long de l'axe principal
    // au lieu d'un refus (`SkeletonSatinParameters::compact_axis`). Faux par défaut.
    bool compact_axis{false};
    bool auto_spacing{false};                   // HP-ENG-003 : pas selon la largeur locale
    SatinCorner corner_mode{SatinCorner::None}; // HP-ENG-005 : coins vifs

    bool operator==(const AutoSatinParams&) const = default;
};

// Point de couverture « blanket / E-stitch » et zigzag simple le long d'un chemin (HP-STI-003).
// `Blanket` : une ligne de base, avec à chaque station une dent perpendiculaire (aller-retour).
// `Zigzag` : les points alternent entre deux bords de la bande de largeur `width`.
enum class BlanketStyle : std::uint8_t { Blanket, Zigzag };
// Côté de la bande par rapport au sens du chemin (Y vers le haut). `Center` : bande centrée
// sur le chemin (dents alternées gauche/droite pour le blanket).
enum class BlanketSide : std::uint8_t { Left, Right, Center };

struct BlanketStitchParams {
    BlanketStyle style{BlanketStyle::Blanket};
    Micrometers width{3'000};   // largeur de la bande / longueur des dents (3 mm)
    Micrometers spacing{2'000}; // écart entre stations le long du chemin (2 mm)
    BlanketSide side{BlanketSide::Left};

    bool operator==(const BlanketStitchParams&) const = default;
};

// Remplissage CONCENTRIQUE / contour (« echo », HP-STI-007) : anneaux successifs décalés vers
// l'intérieur du contour de la région (trous respectés : un anneau grandit autour de chaque trou),
// cousus de l'extérieur vers le centre. Les valeurs sont bornées à la génération.
struct ContourFillParams {
    Micrometers row_spacing{400};     // écart entre anneaux (densité) — 0,4 mm
    Micrometers stitch_length{3'000}; // longueur MAXIMALE d'un point, bornée à [1 ; 7] mm
    Micrometers min_length{500};      // longueur minimale d'un point (sauf coins), 0,5 mm
    Micrometers inset{200};           // retrait du bord (compensation de contour)
    // Vrai : les anneaux sont reliés en spirale continue (chaque tour glisse vers l'anneau
    // suivant, sans saut). Faux : anneaux fermés indépendants, reliés par un court pas.
    bool spiral{true};
    bool hidden_underpath{true}; // liaisons longues cousues cachées (au lieu de sauts)

    bool operator==(const ContourFillParams&) const = default;
};

// Motifs de base (HP-STI-002 / HP-STI-006) : petites séquences normalisées définies dans
// `stitch_generation::motif_pattern`. Ajouter en fin seulement (valeur persistée dans le .osp).
enum class MotifId : std::uint8_t { Wave, Zigzag, Leaf, Heart, Diamond, Scallop };

// Motif le long d'un chemin (« motif run », HP-STI-002) : le motif est répété par abscisse
// curviligne le long de chaque contour de la source, orienté selon la tangente.
struct MotifRunParams {
    MotifId motif{MotifId::Wave};
    Micrometers size{3'000};          // hauteur du motif (perpendiculaire au tracé), 3 mm
    Micrometers spacing{4'000};       // longueur d'une répétition le long du tracé, 4 mm
    Micrometers stitch_length{2'000}; // longueur MAXIMALE d'un point, bornée à [1 ; 7] mm
    Micrometers min_length{400};      // longueur minimale d'un point, 0,4 mm
    bool alternate{false};            // miroir d'un motif sur deux (feuille, vague en opposition)

    bool operator==(const MotifRunParams&) const = default;
};

// Remplissage à motif (« motif fill », HP-STI-006) : pavage de la région par un motif répété
// sur une grille tournée de `angle` ; les tracés sont coupés au contour (trous respectés).
struct MotifFillParams {
    MotifId motif{MotifId::Diamond};
    Micrometers size{4'000};          // taille du motif (carré), 4 mm
    Micrometers spacing{5'000};       // pas de la grille entre centres, 5 mm
    Angle angle{0.0};                 // orientation de la grille (radians)
    bool stagger{true};               // une rangée sur deux décalée d'un demi-pas
    Micrometers inset{200};           // retrait du bord
    Micrometers stitch_length{2'000}; // longueur MAXIMALE d'un point, bornée à [1 ; 7] mm
    Micrometers min_length{400};      // longueur minimale d'un point
    bool hidden_underpath{true};      // liaisons longues cousues cachées (au lieu de sauts)

    bool operator==(const MotifFillParams&) const = default;
};

// Remplissage RADIAL (HP-STI-008) : rayons depuis un centre (par défaut le centre de la boîte
// englobante de la région). La densité reste constante à la périphérie : les rayons sont plus
// nombreux vers l'extérieur (ils ne démarrent qu'à partir d'un rayon où l'écart reste supérieur
// ou égal à `row_spacing`), les points raccourcissent vers le centre. Bornes à la génération.
struct RadialFillParams {
    Micrometers row_spacing{400};     // écart (arc) entre rayons à la périphérie — 0,4 mm
    Micrometers stitch_length{3'000}; // longueur MAXIMALE d'un point, bornée à [1 ; 7] mm
    Micrometers min_length{500};      // longueur minimale d'un point (sauf bout de rayon)
    Micrometers inset{200};           // retrait du bord
    std::optional<Vec2um> center;     // nullopt = centre de la boîte englobante
    bool hidden_underpath{true};      // liaisons longues cousues cachées (au lieu de sauts)

    bool operator==(const RadialFillParams&) const = default;
};

// Remplissage en SPIRALE (HP-STI-009) : spirale d'Archimède continue depuis un centre, pas de
// `row_spacing` entre deux tours, rognée au contour de la région (sans coupe tant que la région
// contient la spirale ; les trous et sorties sont reliés par un saut caché si possible).
struct SpiralFillParams {
    Micrometers row_spacing{400};     // écart entre deux tours — 0,4 mm
    Micrometers stitch_length{3'000}; // longueur MAXIMALE d'un point, bornée à [1 ; 7] mm
    Micrometers min_length{500};      // longueur minimale d'un point (hors rognage au bord)
    Micrometers inset{200};           // retrait du bord
    std::optional<Vec2um> center;     // nullopt = centre de la boîte englobante
    bool clockwise{false};            // sens de rotation (faux = anti-horaire, Y vers le haut)
    bool hidden_underpath{true};

    bool operator==(const SpiralFillParams&) const = default;
};

// Remplissage CROISÉ / crosshatch (HP-STI-014) : 2 ou 3 tatamis très espacés à angles différents
// (répartis sur 180°) dans un seul objet.
struct CrosshatchFillParams {
    Angle angle{0.0};                 // orientation de la première couche (radians)
    int layers{2};                    // 2 (perpendiculaires) ou 3 (à 60°), borné à [2 ; 3]
    Micrometers row_spacing{1'500};   // écart entre rangées d'UNE couche — 1,5 mm
    Micrometers stitch_length{3'000}; // longueur de point le long d'une rangée
    Micrometers inset{200};           // retrait du bord
    bool hidden_underpath{true};

    bool operator==(const CrosshatchFillParams&) const = default;
};

// Un objet de broderie porte un TYPE de point sous forme de variant. Chaque
// type suit la géométrie d'un objet vectoriel source (contour pour running,
// région pleine pour tatami) ou porte la sienne (satin). La séparation
// intention/points (ADR-014) tient : les points sont régénérés à la demande.
// Les alternatives directionnelle puis auto-satin sont AJOUTÉES en fin de
// variant : les index des types historiques ne changent pas.
using StitchParams =
    std::variant<RunningStitchParams, TatamiParams, SatinParams, DirectionalFillParams,
                 AutoSatinParams, BlanketStitchParams, ContourFillParams, MotifRunParams,
                 MotifFillParams, RadialFillParams, SpiralFillParams, CrosshatchFillParams>;

// Type de point autorisé après retouche manuelle (Lot 8 MVP, §2 du cadrage) :
// seule transition permise, Stitch <-> Jump — pas de Trim/ColorChange/Stop,
// qui ont une sémantique machine distincte gérée par `trim_after` séparément.
enum class StitchPointType : std::uint8_t { Stitch, Jump };

// Retouche ponctuelle d'un point généré (ADR-014 : jamais stocké comme la
// séquence elle-même, seulement le delta). `base_index` désigne une position
// dans la vue brute de l'objet (`stitch_generation::raw_slice`), toutes passes
// confondues ; seules les entrées `pass == StitchPass::TopStitch` y sont
// éligibles en MVP, avec une restriction de type par champ (chacun validé
// indépendamment par `stitch_generation::apply_manual_overrides`, cf. cadrage
// Lot 8 §1-§2) : `moved_to` exige une cible `Stitch` ; `forced_type` et
// `trim_after` acceptent une cible `Stitch` OU `Jump` (Stitch<->Jump est
// bidirectionnel).
struct StitchOverride {
    std::size_t base_index{};
    std::optional<Vec2um> moved_to;             // nullopt = position générée
    std::optional<StitchPointType> forced_type; // nullopt = type généré
    bool trim_after{false};                     // insère un Trim juste après ce point
    // HP-EDIT-002 : suppression du point (Stitch ou Jump de passe TopStitch) et points Stitch
    // insérés juste après lui (dans l'ordre), en deltas comme le reste (ADR-014).
    bool deleted{false};
    std::vector<Vec2um> inserted_after;

    bool operator==(const StitchOverride&) const = default;
};

// §21/§24 du plan de refonte satin (2026-08-14) : distingue une
// classification AUTOMATIQUE (auto-numérisation, décision algorithmique
// sans utilisateur interactif à qui proposer un choix) d'un choix EXPLICITE
// de l'utilisateur (créer/convertir manuellement un objet) -- jusqu'ici
// implicite dans le CODE (quel chemin d'appel a créé l'objet), jamais un
// concept explicite du modèle de document. `AutoChoice` reste la valeur par
// défaut (comportement historique, aucune régression sur les projets
// existants qui n'avaient pas ce concept -- absent d'un .osp ancien ⇒
// `AutoChoice`, jamais une supposition différente).
enum class EmbroideryIntent : std::uint8_t { AutoChoice, ForcedUserChoice };

// Entrée/sortie automatiques (HP-ENG-010) au niveau de l'objet : `Inherit` suit le
// réglage du projet (`SequenceFinishing::auto_join`), `Auto` l'active pour cet
// objet, `Off` le désactive (le sens naturel du générateur est conservé).
enum class JoinMode : std::uint8_t { Inherit, Auto, Off };

// Connecteur APRÈS l'objet (HP-SEQ-005), vers l'objet suivant de même fil. `Inherit` suit les
// règles du projet (seuil de coupe, ponts cachés) ; `AlwaysTrim` force toujours point d'arrêt +
// coupe (jamais remplacée par un pont) ; `NeverTrim` supprime coupe et points d'arrêt à cet endroit
// : le fil reste tendu (saut), c'est à l'utilisateur de s'assurer qu'il sera caché. Sans effet
// avant un changement de fil, un arrêt ou une coupe posée à la main.
enum class ConnectorMode : std::uint8_t { Inherit, AlwaysTrim, NeverTrim };

// Relation explicite entre les deux couches d'un fondu rééditable. Pure métadonnée :
// seule la génération depuis `params` détermine les points, jamais ce lien.
struct ColorBlendLink {
    ObjectId partner;
    bool foreground{false};

    bool operator==(const ColorBlendLink&) const = default;
};

struct EmbroideryObject {
    ObjectId id;
    std::string name;
    ObjectId source_vector{};          // objet vectoriel suivi
    std::array<std::uint8_t, 3> rgb{}; // couleur de fil (palette réelle : Phase 10)
    StitchParams params{RunningStitchParams{}};
    bool visible{true};
    bool locked{false}; // l'optimisation d'ordre ne déplace pas un objet verrouillé
    EmbroideryIntent intent{EmbroideryIntent::AutoChoice};
    JoinMode join{JoinMode::Inherit};                // HP-ENG-010
    ConnectorMode connector{ConnectorMode::Inherit}; // HP-SEQ-005

    // Retouches manuelles (Lot 8 MVP) — deltas épars, jamais O(nombre de
    // points). Vide = comportement actuel inchangé (Clean, rétrocompatible).
    std::vector<StitchOverride> overrides;
    // Empreinte/compteur de `raw_slice` au moment de la dernière édition
    // réussie ; ne sont significatifs que si `overrides` n'est pas vide.
    std::uint64_t edited_fingerprint{0};
    std::uint32_t edited_point_count{0};

    // HP-THR-004 : fil de nuancier assigné (marque + référence). FACULTATIF :
    // vide = couleur libre (comportement historique). `rgb` reste la SEULE
    // source du rendu et des blocs de couleur ; assigner un fil copie la
    // couleur du fil dans `rgb` (SetObjectThreadCommand) et un fil absent de la
    // bibliothèque courante (nuancier utilisateur non chargé) n'altère pas
    // l'affichage. Écrit/lu dans le .osp (schéma v6, champ `thread`).
    std::optional<thread_palette::ThreadKey> thread;
    std::optional<ColorBlendLink> color_blend;

    [[nodiscard]] bool is_tatami() const { return std::holds_alternative<TatamiParams>(params); }
    [[nodiscard]] bool is_satin() const { return std::holds_alternative<SatinParams>(params); }
    [[nodiscard]] bool is_directional() const {
        return std::holds_alternative<DirectionalFillParams>(params);
    }
    [[nodiscard]] bool is_auto_satin() const {
        return std::holds_alternative<AutoSatinParams>(params);
    }
    [[nodiscard]] bool is_blanket() const {
        return std::holds_alternative<BlanketStitchParams>(params);
    }
    [[nodiscard]] bool is_contour_fill() const {
        return std::holds_alternative<ContourFillParams>(params);
    }
    [[nodiscard]] bool is_motif_run() const {
        return std::holds_alternative<MotifRunParams>(params);
    }
    [[nodiscard]] bool is_motif_fill() const {
        return std::holds_alternative<MotifFillParams>(params);
    }
    [[nodiscard]] bool is_radial_fill() const {
        return std::holds_alternative<RadialFillParams>(params);
    }
    [[nodiscard]] bool is_spiral_fill() const {
        return std::holds_alternative<SpiralFillParams>(params);
    }
    [[nodiscard]] bool is_crosshatch_fill() const {
        return std::holds_alternative<CrosshatchFillParams>(params);
    }
};

} // namespace openstitch::document
