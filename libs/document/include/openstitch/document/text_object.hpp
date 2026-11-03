// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"

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

// Base du texte (HP-TXT-005) : courbe le long de laquelle les lettres sont posées.
//   Line   : ligne droite (historique) ;
//   Arc    : arc de cercle de rayon `radius`, la ligne de base démarre à `origin` ;
//   Circle : cercle complet de rayon `radius` ; les lettres sont réparties sur toute la
//            circonférence (espacement ajouté pour boucler) ;
//   Path   : polyligne `path` (µm, relatifs à `origin`), abscisse curviligne.
enum class TextBaseKind : std::uint8_t { Line, Arc, Circle, Path };

struct TextBase {
    TextBaseKind kind{TextBaseKind::Line};
    Micrometers radius{50'000}; // Arc / Circle (> 0)
    // Faux : texte sur le dessus (convexe, sommets des lettres vers l'extérieur, lecture
    // horaire). Vrai : texte en dessous (concave, sommets vers le centre, lecture
    // antihoraire).
    bool inside{false};
    std::vector<Vec2um> path; // Path : points relatifs à `origin`

    bool operator==(const TextBase&) const = default;
};

// Édition individuelle d'une lettre (HP-TXT-008) : écarts appliqués à la lettre
// d'index `index` (ordre de lecture, espaces exclus) APRÈS la mise en page ; le reste
// du texte reste régénéré. Rotation et échelle autour du centre de la boîte de la lettre.
struct GlyphEdit {
    std::uint32_t index{0};
    Micrometers dx{};
    Micrometers dy{};
    Angle rotation{0.0};
    double scale{1.0};
    std::optional<std::array<std::uint8_t, 3>> rgb; // couleur propre à la lettre

    bool operator==(const GlyphEdit&) const = default;
};

// Crénage manuel d'une paire de caractères (HP-TXT-007). `adjust` REMPLACE le crénage de
// la police pour la paire (µm, signé ; négatif = rapproche). Voir lettering::KerningOverride.
struct KerningPair {
    char32_t left{0};
    char32_t right{0};
    Micrometers adjust{};

    bool operator==(const KerningPair&) const = default;
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
    // Crénage manuel par paire (HP-TXT-007) : vide = comportement historique. Ordre conservé ;
    // pour une paire répétée, la première entrée s'applique (le dialogue n'en produit pas).
    std::vector<KerningPair> kerning_pairs;
    // Ligatures f (ff, fi, fl, ffi, ffl) quand la police les porte (HP-TXT-012).
    bool ligatures{false};
    TextAlign align{TextAlign::Left};
    Micrometers justify_width{0}; // largeur cible des lignes en mode Justify

    // Base du texte : droite (HP-TXT-005 n'ajoutera que d'autres bases). `origin` est le
    // point de la ligne de base de la première ligne ; son abscisse désigne le bord
    // gauche, le centre ou le bord droit selon `align`. `rotation` tourne tout le
    // texte autour de `origin` (sens antihoraire, repère Y vers le haut).
    Vec2um origin{};
    Angle rotation{0.0};

    // Base courbe (HP-TXT-005) ; Line = comportement historique.
    TextBase base;
    // Éditions par lettre (HP-TXT-008), triées par index, une entrée par index.
    std::vector<GlyphEdit> glyph_edits;
    // Connecteurs (HP-TXT-014) : coupe entre deux lettres d'un même mot / entre deux mots
    // (ou lignes). Inherit = règle générale des finitions.
    ConnectorMode letter_connector{ConnectorMode::Inherit};
    ConnectorMode word_connector{ConnectorMode::Inherit};

    std::array<std::uint8_t, 3> rgb{0, 0, 0};
    TextFill fill{TextFill::Auto};
    Micrometers max_satin_width{6'000}; // au-delà, la lettre passe en tatami (avertissement)
    Micrometers density{400};           // écart des traversées satin / rangées tatami

    bool operator==(const TextObject&) const = default;
};

} // namespace openstitch::document
