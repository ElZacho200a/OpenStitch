// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <variant>
#include <vector>

#include "openstitch/image/image.hpp"

namespace openstitch::image {

// Opérations de prétraitement NON DESTRUCTIVES : l'image source n'est jamais
// modifiée, le document conserve la pile d'opérations et l'image de travail
// est recalculée par apply_pipeline (cahier des charges §4.2).
//
// Note : toutes ces opérations conservent la résolution de travail (mm/px) —
// le recadrage réduit la taille physique, la rotation 90° échange largeur et
// hauteur. Le rééchantillonnage (resize) est volontairement absent : il
// changerait la résolution de travail et sera conçu avec la segmentation.

struct CropOp {
    int x{0}, y{0}, width{0}, height{0}; // en pixels de l'image d'entrée
    bool operator==(const CropOp&) const = default;
};

struct FlipOp {
    bool horizontal{true}; // false = symétrie verticale
    bool operator==(const FlipOp&) const = default;
};

struct Rotate90Op {
    int quarter_turns{1}; // 1..3, sens horaire
    bool operator==(const Rotate90Op&) const = default;
};

struct GrayscaleOp {
    bool operator==(const GrayscaleOp&) const = default;
};

struct BrightnessContrastOp {
    double brightness{0.0}; // -100..100 (décalage)
    double contrast{0.0};   // -100..100 (pente autour de 128)
    bool operator==(const BrightnessContrastOp&) const = default;
};

struct MedianDenoiseOp {
    int strength{1}; // 1 => noyau 3, 2 => noyau 5
    bool operator==(const MedianDenoiseOp&) const = default;
};

struct QuantizeOp {
    int colors{8}; // 2..64
    bool operator==(const QuantizeOp&) const = default;
};

// Lissage bilatéral : adoucit les aplats et les dégradés (bruit, grain JPEG)
// en gardant les contours nets, ce qu'un médian fait moins bien sur un
// dégradé. Utile avant l'analyse de couleurs d'une région (fondu à deux fils).
struct BilateralDenoiseOp {
    int strength{2}; // 1..3 : noyau 5/7/9 px, tolérance de couleur 20/40/60
    bool operator==(const BilateralDenoiseOp&) const = default;
};

// Nouvelles alternatives toujours AJOUTÉES EN FIN (index stables).
using ImageOp = std::variant<CropOp, FlipOp, Rotate90Op, GrayscaleOp, BrightnessContrastOp,
                             MedianDenoiseOp, QuantizeOp, BilateralDenoiseOp>;

// Nom lisible de l'opération (menus, historique d'annulation).
[[nodiscard]] std::string op_name(const ImageOp& op);

[[nodiscard]] Result<Image> apply_op(const Image& input, const ImageOp& op);

// Rejoue la pile complète depuis l'original. Déterministe.
[[nodiscard]] Result<Image> apply_pipeline(const Image& original, std::span<const ImageOp> ops);

} // namespace openstitch::image
