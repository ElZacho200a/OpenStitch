# Simulation de couture

Public : utilisateur. État : **Implémenté** (base).

## But

Rejouer visuellement la couture pour repérer les sauts, l'ordre et la trajectoire
de l'aiguille avant l'export.

## Fonctionnement

La barre de simulation apparaît dès qu'une séquence de points existe. Elle offre :

- un bouton **Lecture / Pause** ;
- un **curseur** qui révèle la couture jusqu'à un index de point ;
- un **compteur** « point courant / total » ;
- un **repère d'aiguille** (cercle rouge) à la position courante.

Pendant la lecture, un minuteur (~60 pas/seconde) avance l'index proportionnellement
à la taille du motif. Seule la **couche des points** est reconstruite à chaque
image (l'image de fond et les vecteurs ne sont pas recalculés), pour rester
fluide.

Limitation : la simulation est **point par point** via le curseur. Le réglage de
vitesse explicite, l'avance objet-par-objet ou couleur-par-couleur, et
l'affichage de l'objet actif ne sont **pas** exposés (le modèle en garde
l'information : chaque `StitchCommand` porte son `ObjectId` source).

## Implémentation associée

- `apps/desktop/main_window.cpp` — `buildSimulationToolbar`, `toggleSimulation`,
  `onSimTick`, `onSimSliderMoved`, `renderStitches`.
- `libs/stitch/include/openstitch/stitch/sequence.hpp` — `StitchSequence`,
  `StitchCommand` (avec `source`).
