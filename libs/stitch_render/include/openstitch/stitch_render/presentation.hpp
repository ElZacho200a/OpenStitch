// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_render/params.hpp"
#include "openstitch/stitch_render/raster.hpp"
#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::stitch_render {

// Rendu « présentation » d'une séquence entière (HP-FMT-011) : image réaliste hors écran, sans
// Qt, à une résolution choisie, pour montrer un design à un client ou l'insérer dans un devis.
struct PresentationOptions {
    double px_per_mm{10.0}; // résolution demandée (abaissée si l'image dépasse max_pixels)
    double margin_mm{3.0};  // marge autour des points
    std::size_t max_pixels{50'000'000}; // plafond mémoire de l'image produite
    RenderParams params{};              // tissu (opaque ou transparent), fil, qualité

    bool operator==(const PresentationOptions&) const = default;
};

// Couleur de fil de repli du bloc de couleur n° `block` (0 = avant le premier changement de
// fil), pour les fichiers machine qui ne portent pas de couleurs : palette fixe, cyclique.
[[nodiscard]] std::array<std::uint8_t, 3> block_palette_color(std::size_t block);

// Copie de `sequence` où chaque commande porte comme source l'identifiant `block + 1` de son
// bloc de couleur (un ColorChange ouvre le bloc suivant). Permet de colorer un fichier machine.
[[nodiscard]] stitch::StitchSequence assign_color_blocks(const stitch::StitchSequence& sequence);

// Image réaliste de toute la séquence. Image vide (0 x 0) si la séquence ne contient aucun brin.
[[nodiscard]] RasterImage render_sequence(const stitch::StitchSequence& sequence,
                                          const ThreadColorFn& color_of,
                                          const PresentationOptions& options);

// Rendu d'un fichier machine : couleurs de repli par bloc de couleur.
[[nodiscard]] RasterImage render_machine_sequence(const stitch::StitchSequence& sequence,
                                                  const PresentationOptions& options);

// RGBA « droit » (non prémultiplié) de l'image prémultipliée, prêt pour un encodage PNG.
[[nodiscard]] std::vector<std::uint8_t> to_straight_rgba(const RasterImage& image);

} // namespace openstitch::stitch_render
