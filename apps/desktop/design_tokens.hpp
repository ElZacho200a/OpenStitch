// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>

// Design tokens centralisés (couleurs, espacements, tailles). Point de vérité
// unique de l'identité visuelle : aucun widget ne doit coder une couleur en dur.
// Deux thèmes (clair par défaut, sombre en gris profonds) et deux densités
// partagent la MÊME structure et les mêmes rôles — seules les valeurs changent.
namespace openstitch::desktop {

enum class ThemeMode { Light, Dark };
enum class Density { Comfortable, Compact };

struct Tokens {
    // --- Surfaces & texte ---
    QColor window;          // fond de fenêtre
    QColor surface;         // panneaux
    QColor surfaceRaised;   // champs, éléments surélevés
    QColor border;          // séparateurs, bordures
    QColor text;            // texte principal
    QColor textSecondary;   // texte secondaire / aide

    // --- Accent & états (une SEULE couleur d'accent) ---
    QColor accent;          // rappel « fil », sobre
    QColor accentHover;
    QColor selection;       // sélection dans les listes
    QColor selectionText;   // texte sur sélection
    QColor focus;           // contour de focus clavier
    QColor success;
    QColor warning;
    QColor error;
    QColor info;

    // --- Canevas (thémé, double contraste pour les repères) ---
    QColor canvasBackground;
    QColor canvasGrid;          // avec alpha
    QColor canvasAxis;          // avec alpha
    QColor canvasHoop;          // cadre de broderie
    QColor canvasStitch;        // couleur de repli des points
    QColor canvasJump;          // sauts (pointillés)
    QColor canvasNode;          // nœuds vectoriels
    QColor canvasHandle;        // poignées (rotation, etc.)
    QColor canvasSelectionHalo; // halo clair sous la sélection
    QColor canvasSelectionLine; // trait de sélection

    // --- Métrique (dépend de la densité, pas du thème) ---
    int space1;         // 2
    int space2;         // 4
    int space3;         // 8
    int space4;         // 12
    int controlHeight;  // hauteur des champs/boutons
    int radiusSm;       // 3
    int radiusMd;       // 5
    int iconSize;       // 16
};

[[nodiscard]] Tokens light_tokens(Density density = Density::Comfortable);
[[nodiscard]] Tokens dark_tokens(Density density = Density::Comfortable);
[[nodiscard]] Tokens tokens_for(ThemeMode mode, Density density);

}  // namespace openstitch::desktop
