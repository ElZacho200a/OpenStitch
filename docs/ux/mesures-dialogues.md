# Mesures de taille minimale : fenêtre, panneaux et dialogues

Mesures de HP-QA-008 (garde de taille minimale), prises par `tests/unit/desktop/test_ui_responsive.cpp`
(Qt 6.8.3, plateforme `offscreen`, build Release, police par défaut, densité « Confortable »). Les
tailles sont des `minimumSizeHint()` de la fenêtre / des docks / des barres, et, pour un dialogue,
la contrainte réellement imposée par son layout (`layout()->activate()` puis `minimumSize()`, qui
honore un `setMinimumSize` explicite). Ce sont des mesures (classe A), pas des estimations. Pour les
relire : `ctest --preset msvc-release -R test_ui_responsive -V` (lignes `MESURE …`).

Budgets (HP-QA-008) : fenêtre, tous docks ouverts, ≤ 911 × 480 ; dialogues ≤ 880 × 440 ; aides
≤ 460 × 320 ; dock Fils ≤ 200 px de haut.

## Fenêtre principale (tous docks visibles, séquence chargée)

| Mesure | Avant (lot A) | Après (lot B) |
|---|---|---|
| `window.minimumSizeHint()` | **1291 × 1443** | **701 × 373** |
| dock Analyse | 607 × 399 | 68 × 84 (zone défilante) |
| dock Fils | 697 × 393 | 68 × 84 (zone défilante) |
| dock Couleurs | 665 × 254 | 68 × 84 (zone défilante) |
| dock Ordre | 438 × 202 | 68 × 84 (zone défilante) |
| dock Workflow | 360 × 228 | 68 × 84 (zone défilante) |
| dock Document | 127 × 142 | 127 × 142 (inchangé) |
| dock Propriétés / Filtres / Historique | 68 × 84 / 68 × 84 / 70 × 86 | inchangé |
| barre contextuelle (minimum) | 921 × 27 | 45 × 27 (étiquettes élidables) |
| barre d'état (minimum) | 754 × 24 | 250 × 24 |
| barre des fils / barre de simulation (minimum) | 128 × 48 / 161 × 30 | 488 × 48 / 161 × 30 (même rangée) |
| canevas à 911 × 480, séquence chargée | 127 × 275 | 205 × 305 (471 × 305 en mode portable) |

## Dialogues et écran d'accueil

| Dialogue | Avant | Après |
|---|---|---|
| Texte / Lettrage | 1152 × 658 | 296 × 116 |
| Export machine | 1265 × 448 | 356 × 116 |
| Mélange de couleurs | 970 × 515 | 296 × 122 |
| Segmentation IA | 1172 × 866 | 200 × 116 |
| Écran d'accueil | 894 × 200 (894 × 336 avec 5 récents longs) | 70 × 70 |
| Fiche de production | 732 × 272 | 732 × 272 (déjà conforme) |
| Import d'image | 766 × 352 | inchangé |
| Segmentation | 634 × 193 | inchangé |
| Réglages | 789 × 233 | inchangé |
| Rendu réaliste | 629 × 384 | inchangé |
| Export image | 802 × 227 | inchangé |
| **Préférences IA** | **802 × 523** | **inchangé** (hors périmètre du lot B ; plafond de test provisoire à 560 px de haut) |
| Aide : gestes | 460 × 320 | 460 × 320 |
| Aide : prise en main | 380 × 300 | 380 × 300 |

Les minimums faibles après correction viennent de la zone défilante : le contenu garde sa taille
confortable (taille initiale du dialogue, bornée à 92 % de la zone utile de l'écran à
l'affichage) mais peut être réduit ; une barre de défilement apparaît à la place du débordement.
Boutons OK / Annuler / Valider restent hors de la zone défilante.

## Notes de lecture

- Le `minimumSizeHint()` d'une aide (Gestes) vaut 696 × 172 ; sa contrainte effective est
  460 × 320 (`setMinimumSize`). Le test retient la contrainte effective.
- À 911 × 480 avec Document (260) et Propriétés (320) ouverts de part et d'autre, le canevas reste
  étroit (205 px) : 450 px de large ne sont atteints qu'en mode portable (HP-UX-027, 471 px).
