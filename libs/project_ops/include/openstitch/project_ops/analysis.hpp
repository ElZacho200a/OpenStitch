// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

// Outils d'analyse de la qualité d'un résultat, sur la séquence EFFECTIVE d'un projet, dans le
// repère EXACT de l'image source (aucun calage heuristique) : fidélité à la source, épaisseurs de
// fil, « qu'est-ce qui a cousu ce point ? » et détecteurs de défauts connus. Qt-free,
// déterministes.
namespace openstitch::project_ops {

inline constexpr const char* kFidelitySchema = "openstitch.fidelity/1";
inline constexpr const char* kDensitySchema = "openstitch.density/1";
inline constexpr const char* kFabricRiskSchema = "openstitch.fabric-risk/1";
inline constexpr const char* kLocateSchema = "openstitch.locate/1";
inline constexpr const char* kDiagnoseSchema = "openstitch.diagnose/1";

// --- Fidélité à l'image source ---------------------------------------------------------------
struct FidelityOptions {
    double thread_mm{0.55};   // diamètre de fil utilisé pour décider qu'un pixel est couvert
    double hole_zone_mm{1.6}; // un manque à moins de cette distance du fond est un « bord »
    double background_tolerance{6.0}; // écart CIELAB au fond en dessous duquel un pixel est du fond
    double near_palette{18.0};        // pixel « proche » d'une couleur de fil (détail attendu)
    double wrong_colour{30.0}; // écart au fil posé à partir duquel la couleur est mauvaise
    std::size_t largest{10};   // nombre de plus grands trous listés
    bool error_map{false};     // produit aussi une carte d'erreurs PNG
};

struct FidelityReport {
    std::string json; // schéma `openstitch.fidelity/1`
    std::string text; // résumé lisible
    std::vector<std::uint8_t> error_map_png;
    double motif_mm2{0.0};
    double missing_mm2{0.0};       // à broder mais sans fil (bords compris)
    double interior_hole_mm2{0.0}; // idem, à l'INTÉRIEUR du motif : tissu visible
    double lost_detail_mm2{0.0};   // détail dont la couleur est absorbée par une voisine
    double interior_hole_ratio{0.0};
    double lost_detail_ratio{0.0};
};

// Compare chaque pixel de l'image source à ce que les fils couvrent réellement. Le fond ignoré est
// la plus grande région de la segmentation dont la couleur n'est celle d'aucun fil.
[[nodiscard]] Result<FidelityReport> measure_fidelity(const document::Project& project,
                                                      const FidelityOptions& options = {});

// --- Épaisseurs de fil ------------------------------------------------------------------------
struct DensityOptions {
    double cell_mm{1.0};
    // Une « épaisseur » = la longueur de fil par mm² d'un remplissage à cet écart de rangées.
    double reference_spacing_mm{0.4};
    double hotspot_layers{
        4.0}; // au-delà : surépaisseur (risque de casse d'aiguille, de plissement)
    double heatmap_px_per_cell{8.0};
    bool heatmap{false};
};

struct DensityReport {
    std::string json; // schéma `openstitch.density/1`
    std::string text;
    std::vector<std::uint8_t> heatmap_png;
    double layers_max{0.0};
    double layers_p99{0.0};
    double hotspot_area_mm2{0.0};
};

[[nodiscard]] Result<DensityReport> measure_density(const document::Project& project,
                                                    const DensityOptions& options = {});

// Estimation relative (0..100) : pas une tension en N, ni une simulation de deformations.
struct FabricRiskOptions {
    // Profils indicatifs non calibres ; aucun changement des reglages de couture.
    std::string fabric{"woven"}; // woven | jersey | felt
    double cell_mm{1.0};
    double reference_spacing_mm{0.4};
    double heatmap_px_per_cell{4.0};
    bool heatmap{false};
};

struct FabricRiskReport {
    std::string json; // openstitch.fabric-risk/1
    std::string text;
    std::vector<std::uint8_t> heatmap_png;
    double max_score{0.0};
    double p99_score{0.0};
    double hotspot_area_mm2{0.0}; // score >= 70
};

[[nodiscard]] Result<FabricRiskReport> measure_fabric_risk(
    const document::Project& project, const FabricRiskOptions& options = {});

// Dimensions (px) de l'image de travail (source passée par `project.ops`) : le repère de la
// segmentation et de tous les outils d'analyse. {0, 0} sans image.
[[nodiscard]] std::pair<int, int> working_size(const document::Project& project);

// --- Qui a cousu ce point ? -------------------------------------------------------------------
struct LocateQuery {
    double x_mm{0.0}; // repère modèle (Y vers le haut), origine au centre de l'image source
    double y_mm{0.0};
    double radius_mm{0.4};
};

struct LocateReport {
    std::string json; // schéma `openstitch.locate/1`
    std::string text;
    std::vector<std::uint64_t> objects; // du dernier cousu (visible dessus) au premier
};

// Objets dont un brin passe à moins de `radius` du point, du dernier cousu au premier ; avec type,
// couleur, passes et paramètres principaux.
[[nodiscard]] Result<LocateReport> locate_objects(const document::Project& project,
                                                  const LocateQuery& query);

// --- Détecteurs de défauts --------------------------------------------------------------------
struct Finding {
    std::string severity; // error | warning | info
    std::string code;     // identifiant stable
    std::string message;
    std::string suggestion;
    std::vector<std::uint64_t> objects;
    std::optional<std::array<double, 2>> at_mm;
};

struct DiagnoseReport {
    std::string json; // schéma `openstitch.diagnose/1`
    std::string text;
    std::vector<Finding> findings;
    std::size_t errors{0};
    std::size_t warnings{0};
    std::size_t infos{0};
    // Mesures brutes (utilisées par `project sweep`).
    double interior_hole_ratio{0.0};
    double lost_detail_ratio{0.0};
    double hotspot_area_mm2{0.0};
    double layers_max{0.0};
    double connector_ratio{0.0}; // part du fil des remplissages posée en brins obliques visibles
    double motif_width_mm{0.0};
    double motif_height_mm{0.0};
    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    std::size_t color_changes{0};
    double thread_m{0.0};
};

[[nodiscard]] Result<DiagnoseReport> diagnose_project(const document::Project& project);

} // namespace openstitch::project_ops
