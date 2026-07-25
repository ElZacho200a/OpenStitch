// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Avertissements structurés (§5). Aucun générateur ne plante : il diagnostique.
enum class WarningCode {
    PathEmpty,
    PathTooShort,
    DegenerateGeometry,
    StitchTooLong,
    StitchTooShort,
};

struct GenerationWarning {
    WarningCode code;
    std::string message;
    Vec2um location{};
};

struct RunningStats {
    std::size_t stitches{0};
    double length_um{0.0};
    double min_segment_um{0.0};
    double max_segment_um{0.0};
};

struct RunningResult {
    std::vector<Vec2um> points;
    std::vector<GenerationWarning> warnings;
    RunningStats stats;
};

// Configuration du point courant. Les longueurs sont en micromètres ; les noms
// distinguent explicitement la cible, le minimum et le maximum (§30).
struct RunningConfig {
    Micrometers target_length{3'000};    // longueur de point visée
    Micrometers min_length{500};         // en deçà : fusion/avertissement
    Micrometers max_length{7'000};       // au-delà : subdivision + avertissement
    Micrometers flatten_tolerance{100};  // aplatissement de Bézier (0,1 mm)
    Angle corner_threshold{0.6108652};   // ~35° : au-delà, sommet = pénétration exacte
    bool reverse{false};                 // inverse le sens de parcours
    double phase{0.0};                   // décalage curviligne initial [0,1) (boucles lisses)
    std::optional<Vec2um> start;         // point de départ imposé (projeté), boucles fermées
};

// Point courant correct : aplatissement (courbes comprises) → découpe aux coins
// vifs → ré-échantillonnage par LONGUEUR D'ARC de chaque tronçon (§7). Les coins
// sont des pénétrations exactes ; les portions lisses ont un espacement régulier.
[[nodiscard]] RunningResult run_stitch(const geometry::Path& path, const RunningConfig& config);

// Compatibilité : échantillonnage simple (délègue à run_stitch, un passage).
[[nodiscard]] std::vector<Vec2um> sample_path(const geometry::Path& path, Micrometers target_length,
                                              Micrometers min_length);

// Modes de répétition explicites (§8-9).
enum class RepeatMode {
    SinglePass,    // un passage
    BackAndForth,  // aller complet puis retour (termine au départ)
    BeanStitch,    // chaque intervalle cousu `passes` fois (impair)
    Backstitch,    // progression avec recouvrement
};

// Applique un mode de répétition à une liste de points ordonnés. `passes` n'est
// utilisé que pour BeanStitch (rendu impair, minimum 3). Supprime les mouvements
// nuls consécutifs.
[[nodiscard]] std::vector<Vec2um> apply_repeat_mode(const std::vector<Vec2um>& points,
                                                    RepeatMode mode, int passes = 3);

// Compatibilité (ancien paramètre entier) : 1 = simple, 2 = aller-retour,
// >= 3 = bean à `repeats` passages.
[[nodiscard]] std::vector<Vec2um> apply_repeats(const std::vector<Vec2um>& points, int repeats);

}  // namespace openstitch::stitch_generation
