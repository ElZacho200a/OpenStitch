// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

#include <span>

// Design tokens centralisés (couleurs, espacements, tailles). Point de vérité
// unique de l'identité visuelle : aucun widget ne doit coder une couleur en dur.
// Deux thèmes (clair par défaut, sombre en gris profonds) et deux densités
// partagent la MÊME structure et les mêmes rôles — seules les valeurs changent.
namespace openstitch::desktop {

enum class ThemeMode { Light, Dark };
enum class Density { Comfortable, Compact };

struct Tokens {
    // --- Surfaces & texte ---
    QColor window;        // fond de fenêtre
    QColor surface;       // panneaux
    QColor surfaceRaised; // champs, éléments surélevés
    QColor surfaceSunken; // puits : listes/tables, rail de curseur, zone mono
    QColor border;        // filets décoratifs (exemptés de contraste)
    QColor borderStrong;  // bordure de champ, case, poignée (>= 3:1)
    QColor text;          // texte principal
    QColor textSecondary; // texte secondaire / aide
    QColor textDisabled;  // désactivé (règle maison >= 3:1)

    // --- Accent & états (une SEULE couleur d'accent) ---
    QColor accent; // rappel « fil », sobre
    QColor accentHover;
    QColor accentPressed;
    QColor onAccent; // texte/icône sur accent*
    QColor tonal;    // boutons secondaires
    QColor tonalHover;
    QColor tonalPressed;
    QColor selection;         // sélection dans les listes
    QColor selectionText;     // texte sur sélection
    QColor textSelection;     // sélection de texte (QPalette::Highlight)
    QColor textSelectionText; // texte sélectionné (HighlightedText)
    QColor icon;              // icônes (actives = text, désactivées = textDisabled)
    QColor focus;             // contour de focus clavier
    QColor success;
    QColor warning;
    QColor error;
    QColor info;

    // --- Canevas (thémé, double contraste pour les repères) ---
    QColor canvasBackground;
    QColor canvasPaper;         // tissu : clair dans les deux thèmes
    QColor canvasGrid;          // avec alpha
    QColor canvasAxis;          // avec alpha
    QColor canvasHoop;          // cadre de broderie
    QColor canvasStitch;        // couleur de repli des points
    QColor canvasJump;          // sauts (pointillés)
    QColor canvasNode;          // nœuds vectoriels
    QColor canvasHandle;        // poignées (rotation, etc.)
    QColor canvasRailA;         // rail satin A
    QColor canvasRailB;         // rail satin B
    QColor canvasSnap;          // indicateur d'accroche
    QColor canvasPreview;       // aperçus de tracé
    QColor canvasCutLine;       // ligne de coupe satin
    QColor canvasMask;          // surcouche du masque IA
    QColor canvasSelectionHalo; // halo clair sous la sélection
    QColor canvasSelectionLine; // trait de sélection
    // Cadre élastique de sélection (glisser dans le vide, lot L5) : double trait
    // (halo clair/sombre sous le trait) pour rester lisible sur toute couleur de
    // fond ; le remplissage dérive du trait (même teinte, alpha réduit).
    QColor canvasSelectionRectHalo;
    QColor canvasSelectionRectLine;

    // --- Métrique (dépend de la densité, pas du thème) ---
    int space1;         // 2
    int space2;         // 4
    int space3;         // 8
    int space4;         // 12
    int space5;         // 16
    int space6;         // 24
    int space7;         // 32
    int controlHeight;  // hauteur des champs/boutons (32 / 26)
    int controlPadX;    // padding horizontal des boutons (12 / 8)
    int rowHeight;      // hauteur de ligne de liste/table (28 / 24)
    int radiusSm;       // 6
    int radiusMd;       // 10
    int radiusLg;       // 14
    int radiusPill;     // 999
    int iconSize;       // 18 / 16
    int toolIconSize;   // 20 / 18
    int scrollbarWidth; // 8
    int focusRingWidth; // 2

    // --- Typographie (points, suivent le DPI ; graisses QFont::Weight) ---
    double fontSmall;   // 8
    double fontBase;    // 9 (compact 8,5)
    double fontTitle;   // 11
    double fontHeading; // 13
    double fontMono;    // 9
    int weightRegular;  // 400
    int weightMedium;   // 500
    int weightSemibold; // 600

    // --- Mouvement (consommé plus tard ; 0 si OPENSTITCH_REDUCE_MOTION) ---
    int motionShortMs; // 120
};

[[nodiscard]] Tokens light_tokens(Density density = Density::Comfortable);
[[nodiscard]] Tokens dark_tokens(Density density = Density::Comfortable);
[[nodiscard]] Tokens tokens_for(ThemeMode mode, Density density);

// Piles de polices (première famille installée gagne ; voir app_font()).
[[nodiscard]] QStringList font_families();
[[nodiscard]] QStringList mono_font_families();

// --- Contraste WCAG 2.x (pur : aucune dépendance à QApplication) ---
// Compose l'alpha de `fg` sur `bg` (résultat opaque).
[[nodiscard]] QColor composite(const QColor& fg, const QColor& bg);
[[nodiscard]] double relative_luminance(const QColor& c);
// Ratio WCAG entre `fg` (alpha composé sur `bg`) et `bg`, >= 1.
[[nodiscard]] double contrast_ratio(const QColor& fg, const QColor& bg);

// Une paire normative : `fg` doit atteindre `minRatio` sur `bg` dans les deux thèmes.
struct ContrastRule {
    const char* name; // « fg/bg »
    QColor Tokens::*fg;
    QColor Tokens::*bg;
    double minRatio;
};

// Table UNIQUE des paires (spec §1.4) : alimente test_design_tokens et contrast.md.
[[nodiscard]] std::span<const ContrastRule> contrast_rules();

// Tableau Markdown « paire | min | ratio clair | ratio sombre » généré depuis
// contrast_rules() (artefact de relecture ; même format que le plan §1.4).
[[nodiscard]] QString contrast_report_markdown();

} // namespace openstitch::desktop
