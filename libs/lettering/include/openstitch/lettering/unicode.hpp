// SPDX-License-Identifier: Apache-2.0
// Briques Unicode du lettrage (HP-TXT-012), sans HarfBuzz ni table externe :
// composition/décomposition latine, grecque et cyrillique courantes, ordre visuel
// bidirectionnel simplifié (UAX #9 réduit aux cas usuels), repli de glyphes absents.
// Fonctions pures et déterministes.
#pragma once

#include <string>
#include <vector>

namespace openstitch::lettering {

// Marque combinante (U+0300..U+036F, points hébreux, voyelles arabes...) : se dessine
// sur la lettre qui précède, sans chasse propre.
[[nodiscard]] bool is_combining_mark(char32_t c);

// Caractère sans rendu (largeur nulle, sélecteur de variante, contrôle C0/C1 hors
// \t \n \r) : ignoré à la mise en page, sans avertissement.
[[nodiscard]] bool is_ignorable(char32_t c);

// Lettre d'une écriture de droite à gauche (hébreu, arabe, syriaque, thaana...).
[[nodiscard]] bool is_rtl_char(char32_t c);

// Écriture arabe : exige un façonnage contextuel (formes initiale/médiane/finale)
// que ce module ne fournit pas.
[[nodiscard]] bool is_arabic_script(char32_t c);

// base + marque combinante -> caractère précomposé ; 0 si la paire est inconnue.
[[nodiscard]] char32_t compose_pair(char32_t base, char32_t mark);

// Inverse : caractère précomposé -> base + marque. false si inconnu.
[[nodiscard]] bool decompose_char(char32_t composed, char32_t& base, char32_t& mark);

// Séquence de repli d'un caractère courant (guillemets typographiques -> droits,
// tirets -> « - », espaces fines -> espace, « … » -> « ... »). Vide si aucun repli.
[[nodiscard]] std::u32string fallback_sequence(char32_t c);

// Ordre visuel (de gauche à droite à l'écran) d'une ligne en ordre logique :
// les suites de caractères RTL sont inversées, les nombres gardent leur sens, les
// parenthèses sont miroirées dans un passage RTL. Une marque combinante reste
// attachée à sa lettre. Une ligne sans caractère RTL est rendue inchangée.
[[nodiscard]] std::vector<char32_t> visual_order(const std::vector<char32_t>& logical);

} // namespace openstitch::lettering
