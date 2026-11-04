# ADR 0003 — Technologie de Desktop V2 : Qt Widgets, architecture à deux couches

Statut : **accepté, avec conditions de révision explicites** (2026-11-04). Voir aussi `docs/desktop-v2/03-architecture.md`.

## Contexte

La mission demande d'évaluer (1) Qt Widgets, (2) Qt Quick / QML, (3) un hybride, « sur la base de mesures ».

## Ce qui a été mesuré ou établi, et ce qui ne l'a **pas** été

Établi dans le code :

- Le rendu des points est **raster côté moteur** : `stitch_render` produit une image RGBA (`render_realistic_png`,
  `raster.hpp`) ; l'interface n'a pas à tracer des milliers de segments pour l'aperçu. Cela réduit l'avantage théorique
  d'un scene graph GPU (QML) pour l'affichage de l'aperçu.
- Les outils de manipulation directe (poignées de nœuds, rails, guides) de l'ancien desktop sont écrits en
  `QGraphicsItem` (`node_handle.hpp`, `satin_guide_item.hpp`, `canvas_view.cpp`) et couverts par 79 fichiers de tests
  QTest headless (≈ 39 000 lignes). Ce savoir-faire est **réutilisable** en Widgets, pas en QML.
- Le dépôt ne contient aucun QML ; la chaîne de build (CMake, `windeployqt`, CI) ne gère que Widgets.
- Mesuré dans cette livraison : `PreviewCanvas` (`QGraphicsView`) affiche l'aperçu 3 px/mm de Tentabrode à 300 mm
  (138 objets, ≈ 43 700 points) ; zoom ancré au curseur et « ajuster » testés (QTest). Pas de mesure de fluidité chiffrée.

**Non mesuré** (à ne pas présenter comme un résultat) : aucun prototype QML n'a été construit ; aucune comparaison de
temps de trame, de latence de poignée ou de coût mémoire entre Widgets et QML n'existe à ce jour.

## Décision

1. **Qt Widgets** pour l'interface de Desktop V2.
2. La **logique** vit dans `core/`, sans Qt (ADR 0004). Le choix de technologie d'affichage devient ainsi **réversible** :
   un futur front QML (ou tactile) consommerait les mêmes `DocumentSession`, `SelectionModel`, `TaskManager`,
   `ToolRegistry`. C'est la vraie réponse à « hybride » : on ne mélange pas deux piles dans une fenêtre, on isole la
   logique pour qu'aucune pile ne soit définitive.
3. Le calque d'interaction du canevas (poignées, rails, guides) reste en `QGraphicsItem` au-dessus de l'aperçu raster.

## Conditions de révision (déclencheurs mesurables)

Réouvrir cet ADR si, dans le lot « canevas et interactions » (voir `06`), le banc de mesure à écrire montre :

- un calque d'interaction de **N objets sélectionnés × leurs points** (N à fixer sur Tentabrode : ≈ 43 700 points) dont
  le glisser dépasse **16 ms par trame** sur la machine de référence ; ou
- un besoin tactile (pincement, pression de stylet) qu'on ne parvient pas à servir proprement par `QGestureEvent` /
  `QTabletEvent` ; ou
- une exigence d'accessibilité (lecteur d'écran) non tenable avec `QGraphicsView`.

Dans ces cas : prototype QML du seul canevas (`QQuickWidget` ou fenêtre Quick), comparaison chiffrée, nouvel ADR.

## Conséquences

- Pas de dépendance nouvelle ; compatible avec la licence Apache-2.0 (Qt LGPL déjà utilisée).
- Les raccourcis de gestes de l'ancien `interaction_map` restent la référence tant que le canevas V2 n'a pas ses outils.
- Coût assumé : le tactile et le HiDPI avancé restent à valider (HiDPI : l'aperçu natif 1920×1200 a été capturé sans défaut visible).
