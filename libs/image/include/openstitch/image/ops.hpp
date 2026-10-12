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

// Lignes cohérentes (Kang, Lee & Chui, « Coherent Line Drawing », NPAR 2007) :
// transforme une photo ou un dessin en traits noirs sur fond blanc. Un champ de
// flux de tangentes (ETF) suit les contours de l'image ; une différence de
// gaussiennes (DoG) est évaluée dans la direction du gradient puis lissée LE
// LONG du flux, ce qui donne des traits continus et nets, bien moins bruités
// qu'un seuillage de gradient. Le résultat alimente directement le mode
// « Contours / dessin au trait » (segmentation à deux couleurs). L'alpha
// d'origine est conservé ; le fond transparent reste transparent.
struct CoherentLinesOp {
    double sigma_c{1.0};   // échelle du détail détecté, px (0,3..5) ; sigma_s = 1,6 * sigma_c
    double sigma_m{3.0};   // longueur de lissage le long du flux, px (0..10)
    double rho{0.997};     // équilibre de la DoG (0,9..1) ; proche de 1 = traits fins
    double tau{0.98};      // seuil (0,5..1) ; plus bas = moins de traits, plus nets
    int etf_iterations{3}; // 0..10 passes de lissage du champ de tangentes
    int etf_radius{5};     // rayon (px) du voisinage ETF, 1..15
    bool operator==(const CoherentLinesOp&) const = default;
};

// Nouvelles alternatives toujours AJOUTÉES EN FIN (index stables).
using ImageOp = std::variant<CropOp, FlipOp, Rotate90Op, GrayscaleOp, BrightnessContrastOp,
                             MedianDenoiseOp, QuantizeOp, BilateralDenoiseOp, CoherentLinesOp>;

// Nom lisible de l'opération (menus, historique d'annulation).
[[nodiscard]] std::string op_name(const ImageOp& op);

[[nodiscard]] Result<Image> apply_op(const Image& input, const ImageOp& op);

// Rejoue la pile complète depuis l'original. Déterministe.
[[nodiscard]] Result<Image> apply_pipeline(const Image& original, std::span<const ImageOp> ops);

} // namespace openstitch::image
