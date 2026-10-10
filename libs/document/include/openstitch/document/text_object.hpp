// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Seuils de brodabilité d'un texte (HP-TXT-009/013), partagés par le lettrage
// (avertissements à la génération) et l'analyse (règle « texte trop petit »).
inline constexpr Micrometers kTextSatinMinCapHeight{5'000}; // en dessous : satin fragile
inline constexpr Micrometers kTextMinCapHeight{3'000};      // en dessous : illisible
inline constexpr Micrometers kTextMinStrokeWidth{1'000};    // trait < 1 mm : pas de satin
inline constexpr Micrometers kTextSmallLetterHeight{
    8'000}; // en dessous : réglages « petites lettres »

// Alignement horizontal des lignes d'un texte, par rapport à `TextObject::origin`.
enum class TextAlign : std::uint8_t {
    Left,    // le bord gauche de chaque ligne est à origin.x
    Center,  // chaque ligne est centrée sur origin.x
    Right,   // le bord droit de chaque ligne est à origin.x
    Justify, // lignes (sauf la dernière) étirées à `justify_width` par les espaces
};

// Type de remplissage des lettres. Auto choisit lettre par lettre selon la
// largeur de trait (satin pour les traits fins, tatami pour les gros, contour
// si le trait est trop fin pour être cousu en satin).
enum class TextFill : std::uint8_t { Auto, Satin, Tatami, Contour };

// Référence à la police d'un texte. Le fichier de police n'est jamais copié
// dans le document : seul un nom/chemin est mémorisé. Si la police est
// introuvable à l'ouverture, les lettres déjà générées restent valides (elles
// sont persistées) et seule l'ÉDITION du texte est bloquée, avec un message.
struct TextFontRef {
    std::string family;  // nom d'affichage (« Bitstream Vera Sans »)
    std::string file;    // chemin du fichier TTF/OTF (vide pour une police intégrée)
    std::string builtin; // identifiant d'une police intégrée à l'application (« vera-sans »)
    int face_index{0};   // visage dans une collection (.ttc)

    bool operator==(const TextFontRef&) const = default;
};

// Objet TEXTE (HP-TXT-001) : l'INTENTION de lettrage. Les contours des lettres
// (objets vectoriels annotés `text_owner`) et leurs objets de broderie sont
// des dérivés matérialisés dans le projet, remplacés en un seul pas
// d'annulation à chaque édition du texte ; les POINTS ne sont jamais stockés
// (ADR-014). Voir docs/source/lettering.md.
struct TextObject {
    ObjectId id;
    std::string text; // UTF-8 ; '\n' sépare les lignes
    TextFontRef font;

    // Hauteur de capitale (« H ») : c'est ce que l'utilisateur lit sur une règle.
    Micrometers cap_height{10'000}; // 10 mm
    Micrometers letter_spacing{0};  // ajout entre deux lettres (peut être négatif)
    Micrometers word_spacing{0};    // ajout à chaque espace
    double line_spacing{1.5};       // interligne, en multiple de la hauteur de capitale
    bool kerning{true};             // crénage de la police (table `kern`)
    TextAlign align{TextAlign::Left};
    Micrometers justify_width{0}; // largeur cible des lignes en mode Justify

    // Base du texte : droite (HP-TXT-005 n'ajoutera que d'autres bases). `origin` est le
    // point de la ligne de base de la première ligne ; son abscisse désigne le bord
    // gauche, le centre ou le bord droit selon `align`. `rotation` tourne tout le
    // texte autour de `origin` (sens antihoraire, repère Y vers le haut).
    Vec2um origin{};
    Angle rotation{0.0};

    std::array<std::uint8_t, 3> rgb{0, 0, 0};
    TextFill fill{TextFill::Auto};
    Micrometers max_satin_width{6'000}; // au-delà, la lettre passe en tatami (avertissement)
    Micrometers density{400};           // écart des traversées satin / rangées tatami

    bool operator==(const TextObject&) const = default;
};

} // namespace openstitch::document
