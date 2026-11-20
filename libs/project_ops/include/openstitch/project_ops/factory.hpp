// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/autodigitize/stipple_objects.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::project_ops {

// Convention d'erreur de cette lib : `Error::message` = phrase montrable, `Error::detail` =
// piste de correction (vide = aucune), comme le « Piste : » de la CLI.

// --- Création de projets -------------------------------------------------------
struct NewProjectOptions {
    double width_mm{100.0};
    double height_mm{100.0};
    double margin_mm{0.0};
    double trim_threshold_mm{3.0};
    std::string lock{"backforth"}; // none | backforth | triangle | zigzag
};

// Projet vide (aucune image, aucun objet) : cadre et finitions des nouveaux projets.
// Refuse un cadre non positif ou une marge qui dépasse la moitié du cadre.
[[nodiscard]] Result<document::Project> create_empty_project(const NewProjectOptions& options);

// Applique `lock` / `trim_threshold_mm` aux finitions ; erreur nommée si `lock` est inconnu.
[[nodiscard]] Result<void> apply_basic_finishing(document::Project& project,
                                                 double trim_threshold_mm, std::string_view lock);

// « Niveaux » d'optimisation des trajets (`between,within,underlay,regions|all`).
[[nodiscard]] Result<void> parse_route_levels(std::string_view list, document::HiddenTravel& out);

// --- Numérisation automatique image -> objets du document ----------------------
// Mêmes réglages et mêmes valeurs par défaut que `openstitch-cli digitize` et que le
// dialogue du bureau. Aucun algorithme ici : appel de `segmentation`, `autodigitize`.
struct DigitizeSettings {
    double dpi{96.0};
    int max_colors{8};
    int min_region_px{16};
    int smoothing_px{3};
    // -1 auto (segmentation::background_candidate), 0 non, 1 oui (toute région de la couleur du
    // fond), 2 extérieur seulement (les régions de cette couleur qui touchent le pourtour de
    // l'image ; un blanc enfermé dans le motif reste cousu)
    int skip_background{-1};
    std::optional<double> min_feature_mm;
    std::optional<double> min_region_mm2;
    double trim_threshold_mm{3.0};
    std::string lock{"backforth"};
    // Ponts cachés (HP-SEQ-006.a), activés par défaut depuis le 2026-11-20.
    bool hidden_travel{true};
    bool hidden_same_color{false};
    std::string hidden_levels{"all"};
    double hidden_max_mm{50.0};
    std::string mode{"shapes"}; // shapes | contours | stipple | blend
    // Mode « blend » (zones de fondu, 2026-11-17, docs/source/directional-fill.md) : l'image est
    // découpée en zones à bord net (`segmentation::segment_blend_zones`) et chaque zone devient
    // un fondu à parts lues dans l'image, ses fils pris dans une palette globale de mélange.
    int blend_threads{12};     // fils de la palette globale (2 à 16)
    int blend_max_per_zone{3}; // fils mélangés au plus par zone (1 à 4)
    double blend_edge{2.5};    // force de bord qui sépare deux zones (ΔE par 0,5 mm)
    double blend_cell_mm{3.0}; // pas de la grille de parts de chaque zone
    double blend_min_zone_mm2{25.0};
    bool blend_continuous{true};    // rangées continues par fil (faux : grille partagée)
    double detail{0.5};             // contours
    std::string technique{"auto"};  // contours : auto | running | legacy-satin
    double curve_tolerance_mm{0.0}; // shapes
    bool satin_bands{false};        // shapes
    bool single_path{false};        // contours
    bool coherent_lines{false};     // contours : FDoG avant segmentation
    // Traitements d'image AVANT la segmentation (non destructifs : stockés dans
    // `project.imageSource().ops`, `original` reste la source). Opérations séparées par `+`,
    // chacune `nom[:arg[:arg…]]` : median[:1|2], bilateral[:1..3], flatten[:1..3], clahe[:C],
    // sharpen[:A], quantize[:N], contrast:B:C, gray, crop:x:y:w:h, flip:h|v, rotate:1..3. Exemple :
    // « bilateral:2+median:1 ». Vide = aucun.
    std::string pre;
    // HP-AUTO-005 : traits sombres fins cousus par-dessus les remplissages (mode formes).
    bool dark_lines{false};
    double dark_lines_width_mm{1.6};
    double dark_lines_lightness{25.0};
    double dark_lines_bars_mm{
        0.0}; // barres sombres jusqu'à cette largeur : bandes (satin), 0 = non
    double dark_lines_min_length_mm{0.0}; // fragments de trait isolés plus courts : non cousus
    bool dark_lines_objects_only{false};  // ne coudre que les vrais objets, pas les séparations
    // Nettoyage des îlots (voisine de couleur la plus proche) ; 0 = désactivé.
    double island_cleanup_mm2{0.0};
    // Retrait du bord des tatami (mm) ; absent = défaut du document (0,2 mm).
    std::optional<double> fill_inset_mm;
    // Débord de chaque tatami sur ses voisins brodés (mm) ; 0 = désactivé (défaut du moteur).
    double fill_overlap_mm{0.0};
    // Liaisons cousues entre les morceaux de rangées d'un tatami
    // (`AutoOptions::fill_hidden_underpath`).
    bool fill_hidden_underpath{true};
    double island_max_delta_e{0.0}; // 0 = sans limite
    // Mode formes : bloc uni sans tissu visible entre zones voisines (voir
    // `autodigitize::apply_solid_block`). `solid_block_overlap_mm` : débord sous les voisins.
    bool solid_block{false};
    double solid_block_overlap_mm{0.5};
    // « Suivre le mouvement » : les grandes formes allongées (tentacules, rubans, nuages, fumée)
    // passent en remplissage directionnel dont les guides suivent l'axe médian de la forme, au
    // lieu d'un tatami à angle fixe. La couleur la plus sombre (traits) reste en tatami.
    // Cercles et ellipses épaissis (hublots, ventouses, bulles, yeux) : détectés sur le masque des
    // pixels sombres (trou enclos entouré d'une bande sombre d'épaisseur régulière), cousus en
    // auto-satin avec un contour exact. Nécessite `dark_lines`.
    bool rings_satin{false};
    bool flow_fill{false};
    double flow_min_area_mm2{10.0};
    double flow_min_elongation{2.5}; // (plus long guide)² / aire : un disque ~0, une bande >= 5
    // Diagnostic : non vide, dossier où écrire les masques de décision (régions, traits, barres,
    // séparations), la couverture réelle des fils, la carte d'erreurs et `decisions.json`.
    std::string debug_dir;
    // Palette de fils : fusionne les couleurs les plus proches jusqu'à n'en garder que ce nombre
    // (`AutoOptions::max_threads`) ; 0 = pas de limite.
    std::size_t max_threads{0};
    // Tolérance de simplification des contours de formes (µm) ; absent = défaut du moteur.
    std::optional<int> simplify_tolerance_um;
    int stipple_points{2000};
    int stipple_seed{1};
    bool stipple_invert{false};
    // HP-FILE-014 : chemin du profil de réglages (`--settings`) cité dans le rapport ; vide =
    // aucun. Sans effet sur la numérisation.
    std::string profile_path;
};

// Valide la grammaire de `DigitizeSettings::pre` sans toucher d'image.
[[nodiscard]] Result<void> validate_pre_spec(const std::string& spec);

struct DigitizeReport {
    std::size_t image_width_px{0};
    std::size_t image_height_px{0};
    bool image_had_alpha{false};
    std::size_t regions{0};
    bool background_skipped{false};
    bool background_auto{true}; // décision automatique (sinon option explicite)
    std::optional<segmentation::BackgroundCandidate> background_candidate;
    std::size_t vector_objects{0};
    std::size_t embroidery_objects{0};
    std::size_t satin{0};
    std::size_t tatami{0};
    std::size_t rings_satin{0}; // anneaux cousus en satin (--rings-satin)
    std::size_t flow_fill{0};   // tatami passés en directionnel (--flow-fill)
    std::size_t running{0};
    std::size_t blend_fondus{0}; // mode blend : zones cousues en fondu
    std::size_t blend_flats{0};  // mode blend : zones d'un seul fil (aplat)
    std::vector<std::array<std::uint8_t, 3>> blend_palette;   // mode blend : palette globale
    std::optional<autodigitize::ContourMetrics> contour;      // mode contours
    std::optional<autodigitize::StippleMetrics> stipple;      // mode stipple
    std::optional<autodigitize::LineLayerMetrics> dark_lines; // couche de traits sombres
    std::vector<std::string> warnings; // avertissements non bloquants du moteur
};

// Journal d'état optionnel (une ligne lisible par étape) : la CLI le branche sur stderr/stdout.
using StatusLog = std::function<void(std::string_view)>;

// Entrée de `digitize_objects` : segmentation (nulle en mode stipple) et image de travail (celle
// que la segmentation a lue ; traits sombres et stipple s'y appuient).
struct DigitizeSource {
    const segmentation::Segmentation* segmentation{nullptr};
    const image::Image* image{nullptr};
    Millimeters mm_per_px{25.4 / 96.0};
    // Zones de détail du projet (facultatif) : vectorisation plus fine dans ces zones.
    const segmentation::DetailMask* detail_mask{nullptr};
};

struct DigitizedObjects {
    autodigitize::AutoResult objects;
    std::optional<autodigitize::ContourMetrics> contour;
    std::optional<autodigitize::StippleMetrics> stipple;
    std::optional<autodigitize::LineLayerMetrics> dark_lines;
    std::size_t flow_fill{0}; // tatami passés en directionnel (`flow_fill`)
};

// `DigitizeSettings` -> `AutoOptions` du mode formes (bloc uni compris).
[[nodiscard]] autodigitize::AutoOptions make_auto_options(const DigitizeSettings& settings,
                                                          Millimeters mm_per_px,
                                                          bool skip_largest_region);

// Cœur partagé de la CLI et du bureau : choisit le mode (`settings.mode`), numérise et applique le
// remplissage directionnel. Ne touche à aucun document : l'appelant insère les objets (commande
// annulable côté bureau, ajout direct côté CLI). `ids` alloue les identifiants.
[[nodiscard]] Result<DigitizedObjects>
digitize_objects(const DigitizeSource& source, IdGenerator<ObjectId>& ids,
                 const DigitizeSettings& settings, bool skip_largest_region,
                 const StatusLog& log = {}, autodigitize::LineLayerMasks* debug_masks = nullptr);

// Mode blend (expérimental) : à appeler après `digitize_objects` (mode « blend ») sur une
// segmentation de `segmentation::segment_blend_zones`, quand `project` ne contient que les objets
// issus de l'image active. Palette globale de mélange (`settings.blend_threads`) sur `working`,
// puis chaque tatami (une zone) reçoit 1 à `blend_max_per_zone` fils : un seul -> aplat recoloré,
// sinon fondu à parts lues dans l'image (`make_image_blend_command`, sans historique). Ordre de
// couture : fil par fil sur toutes les zones. Partagé par la CLI et Studio.
[[nodiscard]] Result<void> apply_blend_zones(document::Project& project,
                                             const image::Image& working,
                                             const DigitizeSettings& settings,
                                             DigitizeReport& report, const StatusLog& log = {});

// Charge l'image, la place dans `project` (original, résolution), segmente et numérise :
// `project` reçoit image, segmentation, objets vectoriels et de broderie dans l'ordre de
// couture. `project` doit être vide d'objets (sinon E_REFUSED). Les objets sont ajoutés
// directement (création du projet, pas une édition : pas d'historique d'annulation).
[[nodiscard]] Result<DigitizeReport> digitize_image_into(document::Project& project,
                                                         const std::filesystem::path& image,
                                                         const DigitizeSettings& settings,
                                                         const StatusLog& log = {});

} // namespace openstitch::project_ops
