// SPDX-License-Identifier: Apache-2.0
// Lettrage : texte -> contours de glyphes (µm) -> objets vectoriels et objets de
// broderie éditables (HP-TXT-001/003/004/007). Fonctions pures et déterministes ;
// aucune dépendance Qt. Voir docs/source/lettering.md.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/document/text_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/lettering/font.hpp"

namespace openstitch::lettering {

// Avertissement non bloquant sur un texte (jamais d'échec silencieux).
struct TextWarning {
    std::string code;    // slug court : "texte-trop-petit", "glyphe-absent", ...
    std::string message; // phrase montrable à l'utilisateur
};

// Une lettre placée : formes en µm absolus (repère du modèle, Y vers le haut),
// après alignement, interligne et rotation.
struct PlacedGlyph {
    char32_t code_point{0};
    int line{0};
    Vec2um pen{}; // origine du glyphe (ligne de base), après rotation
    std::vector<geometry::PathSet> shapes;
};

struct TextLayout {
    std::vector<PlacedGlyph> glyphs; // ordre de lecture, espaces exclus
    std::vector<TextWarning> warnings;
    int line_count{0};
    // Étendue de la boîte de la mise en page AVANT rotation (largeur de la plus
    // longue ligne, hauteur de capitale + interlignes), pour l'aperçu.
    Micrometers width{};
    Micrometers height{};
};

[[nodiscard]] std::string encode_utf8(char32_t code_point);

// Décode de l'UTF-8 ; une séquence invalide devient U+FFFD (jamais d'exception).
[[nodiscard]] std::vector<char32_t> decode_utf8(const std::string& text);

// Positionne les glyphes du texte : crénage, espacements, alignement, lignes,
// rotation. Les caractères absents de la police sont omis et signalés.
[[nodiscard]] Result<TextLayout> layout_text(const Font& font, const document::TextObject& text);

struct BuildOptions {
    // Vérifie chaque lettre satin par l'auto-satin (couverture) et la bascule en
    // tatami si le squelette ne la couvre pas. Désactivable pour les aperçus rapides.
    bool verify_satin{true};
    double min_satin_coverage{0.92};
};

// Objets dérivés d'un texte, prêts à être insérés par une commande (ids alloués
// par l'appelant dans `ids`, avant la création de la commande).
struct TextBuild {
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
    std::vector<TextWarning> warnings;
    TextLayout layout;
};

// Construit une lettre = un objet vectoriel (`text_owner` = text.id) + un objet de
// broderie. Choix du point par lettre selon `text.fill` (cf. docs) :
//   Satin  : auto-satin (traits fins) ; repli tatami (trait trop large ou squelette
//            insuffisant) ou contour (trait trop fin), avec avertissement ;
//   Auto   : comme Satin, sans avertissement quand le tatami est le choix normal ;
//   Tatami / Contour : appliqués tels quels.
[[nodiscard]] Result<TextBuild> build_text_objects(const Font& font,
                                                   const document::TextObject& text,
                                                   IdGenerator<ObjectId>& ids,
                                                   const BuildOptions& options = {});

// Déplacement subi par les lettres d'un texte depuis leur génération (l'utilisateur a
// déplacé les objets du texte au canevas) : écart entre le coin bas-gauche de leur
// boîte actuelle et celui de la mise en page à `text.origin`. Permet à l'édition du
// texte de repartir de la position visible plutôt que de ramener le texte à son
// origine mémorisée. nullopt si le texte n'a plus de lettres ou n'en produit pas.
[[nodiscard]] std::optional<Vec2um> text_displacement(const document::Project& project,
                                                      const document::TextObject& text,
                                                      const Font& font);

// Avertissements ne dépendant que des réglages (taille minimale brodable) : permet
// d'avertir dans le dialogue avant même de charger la police.
[[nodiscard]] std::vector<TextWarning> check_text_size(const document::TextObject& text);

// Épaisseur moyenne de trait d'une forme (2 x aire / périmètre), en µm.
[[nodiscard]] double mean_stroke_width_um(const std::vector<geometry::PathSet>& shapes);

} // namespace openstitch::lettering
