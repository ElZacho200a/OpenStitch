// SPDX-License-Identifier: Apache-2.0
#pragma once

// Rendu réaliste accéléré par le GPU : données de géométrie SANS Qt ni Qt Quick (testables).
// Le rendu réaliste est une VUE : il ne lit que la scène immuable (séquence effective) et ne
// modifie jamais le document. Le moteur CPU `render_preview` reste la référence.
//
// Principe : les segments consécutifs de même couleur forment un « run » ; l'ordre des runs est
// l'ordre de couture (le recouvrement en dépend). Chaque run est découpé en tranches bornées
// (un nœud de géométrie par tranche). Un segment = un quadrilatère texturé (6 sommets) étendu
// d'une demi-largeur à chaque bout ; la texture « sprite de fil » (thread_textures) porte
// l'éclairage cylindrique, la brillance, la torsion et l'arrondi de bord.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "render_settings.hpp"
#include "studio_scene.hpp"

namespace openstitch::desktop_v2::studio {

// Réglages visuels du mode réaliste du canevas (sous-ensemble de RenderSettings utile au GPU).
struct RealisticStyle {
    double threadWidthMm{0.35};
    double relief{0.7};
    double sheen{0.5};
    double twist{0.5};
    double shadow{0.5};
    std::array<std::uint8_t, 3> fabricRgb{245, 243, 238};
    bool fabricWeave{false};
    double fabricRelief{0.4};

    bool operator==(const RealisticStyle&) const = default;
};
[[nodiscard]] RealisticStyle realisticStyleFrom(const RenderSettings& settings);

struct RealisticRun {
    std::array<std::uint8_t, 3> rgb{};
    std::size_t first{0}; // indice du premier segment de la scène
    std::size_t count{0};
};

// Runs de segments consécutifs de même couleur, dans l'ordre de couture. Couvre exactement
// tous les segments, sans trou ni recouvrement.
[[nodiscard]] std::vector<RealisticRun> buildRealisticRuns(std::span<const StudioSegment> segments);

// Tranche d'un run (au plus `maxSegments` segments) : unité d'un nœud de géométrie.
struct RealisticChunk {
    std::size_t run{0};
    std::size_t first{0}; // indice de scène
    std::size_t count{0};
};
inline constexpr std::size_t kRealisticChunkSegments = 5'000; // 30 000 sommets
[[nodiscard]] std::vector<RealisticChunk>
buildRealisticChunks(std::span<const RealisticRun> runs,
                     std::size_t maxSegments = kRealisticChunkSegments);

// Même disposition que QSGGeometry::TexturedPoint2D (x, y, u, v).
struct RealisticVertex {
    float x{0}, y{0}, u{0}, v{0};
};
static_assert(sizeof(RealisticVertex) == 16);
inline constexpr std::size_t kVerticesPerSegment = 6;

// Atlas de sprites : kOrientationBins cellules côte à côte (l'éclairage dépend de l'orientation
// du fil par rapport à la lumière), u = (bin + coordonnée locale) / kOrientationBins.
inline constexpr int kOrientationBins = 16;
[[nodiscard]] int orientationBin(float dx, float dy); // direction unitaire, repère écran (Y bas)

// Torsion à pas physique constant : l'atlas empile kTwistClasses rangées ; la rangée c porte
// c + 1 périodes de torsion sur la longueur du sprite. Un segment choisit la rangée dont le
// nombre de périodes approche sa longueur (bouts compris) divisée par le pas, au lieu d'étirer
// une torsion fixe. Pas = kTwistPitchWidths largeurs de fil.
inline constexpr int kTwistClasses = 8;
inline constexpr double kTwistPitchWidths = 2.0;
[[nodiscard]] int twistClass(double quadLengthMm, double threadWidthMm);

// Écrit 6 sommets par segment (quadrilatère du fil / de son ombre portée) ; renvoie le nombre de
// sommets écrits. `out` doit contenir 6 * segments.size() sommets. Déterministe.
std::size_t writeThreadQuads(std::span<const StudioSegment> segments, const RealisticStyle& style,
                             RealisticVertex* out);
std::size_t writeShadowQuads(std::span<const StudioSegment> segments, const RealisticStyle& style,
                             RealisticVertex* out);

// Ombres entre fils (shader de fil, RENDER-PBR-01 lot C) : pour chaque segment, son ombre portée
// PUIS son fil, dans l'ordre de couture et dans un même flux. L'ombre du segment i tombe ainsi sur
// les segments 0..i-1 du même run (et sur le tissu), jamais sur le segment i. Les quads d'ombre
// gardent l'encodage (cellule d'orientation, rangée) du fil, décalé de kShadowUOffset en u, ce
// qui permet au shader de reconnaître le mode ombre. `out` : kInterleavedVerticesPerSegment par
// segment. Déterministe.
inline constexpr std::size_t kInterleavedVerticesPerSegment = 2 * kVerticesPerSegment;
inline constexpr float kShadowUOffset = 2.0F;
inline constexpr float kShadowWidthScale = 1.2F;
std::size_t writeInterleavedShadowThreadQuads(std::span<const StudioSegment> segments,
                                              const RealisticStyle& style, RealisticVertex* out);

// Décalage de l'ombre portée (mm, repère écran) pour une largeur de fil donnée.
struct ShadowOffset {
    float dx{0}, dy{0};
};
[[nodiscard]] ShadowOffset shadowOffset(double threadWidthMm);

// Période (mm) de la texture de tissage tuilée.
inline constexpr double kFabricTileMm = 4.0;

} // namespace openstitch::desktop_v2::studio
