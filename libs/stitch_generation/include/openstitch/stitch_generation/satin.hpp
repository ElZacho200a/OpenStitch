// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Barreau : segment transversal reliant un point du rail A à un point du rail B.
// Il définit une correspondance EXACTE entre les deux rails à cette station.
using SatinRungSeg = std::pair<Vec2um, Vec2um>;

// Points courts dans les virages : le rail intérieur reçoit des pénétrations
// rentrées (inset) pour ne pas s'entasser sur le bord (§7).
enum class ShortStitchMode { Disabled, RemoveAndRedistribute, SingleInset, MultiLevelInset };

// Points de fractionnement pour les traversées trop longues (§8). Le point
// intermédiaire est décalé (staggered/jitter) pour éviter une ligne centrale.
enum class SplitStitchMode { Disabled, Simple, Staggered, DeterministicJitter };

// Terminaisons de colonne (§9).
enum class SatinCapType { Flat, Rounded, Tapered, Automatic };

// Paramètres d'une colonne satin (§5.3).
struct SatinConfig {
    Micrometers density{400};            // écart entre pénétrations le long d'un rail (0,4 mm)
    Micrometers pull_compensation{0};    // élargit la colonne pour compenser la traction du fil
    bool center_underlay{false};         // sous-couche : point droit sur l'axe central
    Micrometers underlay_spacing{2'000}; // longueur de point de la sous-couche centrale

    // --- Points courts (virages) ---
    ShortStitchMode short_stitch{ShortStitchMode::Disabled};
    double short_stitch_curvature{0.55};    // ratio avance intérieure/extérieure sous lequel c'est serré
    Micrometers short_stitch_min_gap{250};  // avance intérieure mini avant d'agir
    double short_stitch_inset{0.35};        // profondeur d'inset (fraction de demi-largeur)
    int short_stitch_levels{2};             // nombre de niveaux (mode MultiLevel)

    // --- Split (traversées longues) ---
    SplitStitchMode split_stitch{SplitStitchMode::Disabled};
    Micrometers max_stitch_length{7'000};   // au-delà, on fractionne la traversée
    std::uint64_t split_seed{1};            // graine déterministe (jitter)

    // --- Terminaisons ---
    SatinCapType cap_start{SatinCapType::Flat};
    SatinCapType cap_end{SatinCapType::Flat};
    int cap_length{4};                      // nombre de fils effilés/arrondis au bout

    // --- Sous-couches (Lot 4) : passes distinctes, ordre center -> edge -> zigzag ---
    bool underlay_edge{false};              // deux chemins internes près des rails
    bool underlay_zigzag{false};            // satin léger (largeur réduite, pas plus grand)
    Micrometers underlay_edge_inset{600};   // retrait des rails
    Micrometers underlay_end_retract{600};  // retrait aux extrémités (center/edge)
    Micrometers underlay_zigzag_spacing{1'500};
    double underlay_zigzag_width{0.65};     // fraction de la largeur

    // --- Compensation (Lot 4) : pull latérale asymétrique + push longitudinale ---
    Micrometers pull_left{0};
    Micrometers pull_right{0};
    double pull_left_prop{0.0};              // fraction de la largeur locale
    double pull_right_prop{0.0};
    Micrometers pull_max{5'000};             // borne de l'offset par côté
    Micrometers push_start{0};               // >0 étend la colonne au départ
    Micrometers push_end{0};                 // >0 étend la colonne à la fin
};

// Une passe de couture (polyligne) — sous-couche ou trajet.
struct SatinPass {
    std::vector<Vec2um> points;
};

// Résultat d'une génération satin : sous-couches (dans l'ordre) + couche
// supérieure. Chaque passe est distincte (affichable/analysable séparément).
struct SatinResult {
    std::vector<SatinPass> underlays;  // center, edge A, edge B, zigzag (selon config)
    std::vector<Vec2um> satin;         // points du zigzag principal
    double max_width_um{0.0};          // largeur maximale (pour l'avertissement)

    // Indices dans `satin` où le fil doit être LEVÉ (saut) plutôt qu'enchaîné
    // par un point continu depuis le point précédent -- la couture reprend
    // normalement à partir de ce point. Rempli par `fill_satin_columns`
    // quand deux barreaux consécutifs cessent localement de se comporter
    // comme un ruban (viennent de deux bords qui ne sont plus globalement
    // parallèles, ex. le coude intérieur d'une lettre en L/T) : plutôt que
    // de forcer un point continu disproportionné qui traverse visuellement
    // la forme en diagonale, le fil saute -- pratique standard en broderie
    // (cf. les logiciels commerciaux du métier) plutôt qu'un artefact à
    // masquer après coup. Vide dans le cas courant (ruban continu du début
    // à la fin, la grande majorité des colonnes).
    std::vector<std::size_t> jump_before;
};

// Génère les points d'une colonne satin à partir de deux rails (polylignes
// ouvertes, censées être orientées dans le même sens : bout 0 <-> bout 0,
// bout N <-> bout N). Fournies tête-bêche, elles sont détectées et l'une des
// deux est ré-orientée en interne (cf. audit rails 2026-08-01) — l'appelant
// n'a pas à s'en soucier, mais orienter correctement en amont reste la voie
// normale (l'heuristique de détection n'est pas une garantie géométrique
// générale). Les rails sont appariés par une correspondance locale monotone,
// puis ré-échantillonnés selon l'avance de la ligne médiane : à chaque station,
// un point sur chaque rail et le fil zigzague d'un bord à l'autre. Densité =
// pas le long de la colonne.
[[nodiscard]] SatinResult fill_satin(const geometry::Path& rail_a, const geometry::Path& rail_b,
                                     const SatinConfig& config);

// Génère une colonne satin en respectant des BARREAUX (correspondance par
// sections, cf. auto-satin). Chaque paire de barreaux consécutifs découpe les
// rails en intervalles correspondants, interpolés selon LEUR propre abscisse
// curviligne (rails de longueurs/courbures différentes autorisés). L'espacement
// est mesuré PERPENDICULAIREMENT (avance de la ligne médiane), pas le long d'un
// rail. Les barreaux sont traversés exactement. Séquence L0,R0,L1,R1,…
// déterministe. `rungs` n'a pas besoin d'être trié par l'appelant (chaque
// barreau est une station transversale, pas un élément de séquence — trié en
// interne par position projetée) ; deux barreaux dont la projection sur les
// deux rails est distante de moins de la moitié de `density` sont fusionnés
// (le premier après tri gagne), pour éviter un intervalle dégénéré entre eux.
// Si `rungs` a moins de 2 éléments (ou moins de 2 après fusion), retombe sur
// `fill_satin`.
[[nodiscard]] SatinResult fill_satin_columns(const geometry::Path& rail_a,
                                             const geometry::Path& rail_b,
                                             const std::vector<SatinRungSeg>& rungs,
                                             const SatinConfig& config);

// Génère des barreaux par défaut (correspondance ladder), à l'espacement
// donné, entre deux rails sans barreaux explicites — pour amener un satin créé
// manuellement (deux rails seuls, cf. `rails_from_contour`) sur le MÊME chemin
// de génération que le satin auto (barreaux) : seul `fill_satin_columns`
// implémente l'intégralité de `SatinConfig` (sous-couches de bord/zigzag,
// terminaisons, split, compensation push/pull asymétrique) ; `fill_satin`
// (utilisé quand `rungs` a moins de 2 éléments) n'en implémente qu'un
// sous-ensemble (densité, compensation symétrique, sous-couche centrale) —
// défaut trouvé par revue : l'inspecteur exposait ces réglages pour TOUT
// satin, y compris manuel, sans avertissement ni effet réel sur le résultat
// cousu dans ce cas. Retourne un vecteur vide si les rails sont trop
// courts/dégénérés (l'appelant retombe alors sur `fill_satin`, comportement
// inchangé).
[[nodiscard]] std::vector<SatinRungSeg> default_rungs(const geometry::Path& rail_a,
                                                       const geometry::Path& rail_b,
                                                       Micrometers spacing);

// Découpe un contour fermé en deux rails, coupé aux deux sommets les plus
// éloignés (les « bouts » de la colonne). Convient aux formes allongées.
// Renvoie nullopt si le contour est trop petit.
[[nodiscard]] std::optional<std::pair<geometry::Path, geometry::Path>> rails_from_contour(
    const geometry::Path& contour);

}  // namespace openstitch::stitch_generation
