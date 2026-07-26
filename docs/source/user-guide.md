# Guide utilisateur détaillé

Public : utilisateur débutant et avancé. Ce chapitre documente chaque menu,
outil et raccourci **réellement présents** dans `apps/desktop/main_window.cpp`.

## Le canevas

La zone centrale est un canevas dont l'unité est le **millimètre** (origine au
centre, Y vers le haut). Des **règles** graduées en mm bordent le haut et la
gauche ; une **grille** adaptative et un **cadre** de broderie (rectangle rouge,
100 × 100 mm par défaut) sont dessinés. La position du curseur en mm apparaît
dans la barre d'état.

- **Zoom** : molette (ancrée sous le curseur) ou menu Affichage.
- **Déplacement** : cliquer-glisser (main).
- **Recadrage/sélection** : bascule en mode rectangle quand l'outil de recadrage
  est actif.

Le rendu est organisé en deux couches (image/vecteurs/régions d'une part, points
d'autre part) pour rester fluide sur de gros motifs.

## Menu Fichier

| Action | Raccourci | Effet |
|---|---|---|
| Ouvrir une image… | Ctrl+O | Charge PNG/JPEG/BMP/TIFF puis demande la taille physique |
| Enregistrer le projet… | Ctrl+S | Écrit un fichier `.osp` (tout le document) |
| Ouvrir un projet… | — | Recharge un `.osp` |
| Exporter en DST… | — | Écrit un fichier `.dst` (points uniquement) |
| Importer un DST… | — | Relit un `.dst` comme séquence de points |
| Quitter | Ctrl+Q | Ferme l'application |

## Menu Édition

| Action | Raccourci | Effet |
|---|---|---|
| Annuler | Ctrl+Z | Défait la dernière opération (le libellé nomme l'action) |
| Rétablir | Ctrl+Y | Refait l'opération annulée |

L'annulation couvre les opérations d'image, la segmentation (segmenter, fusionner,
supprimer, recolorer), la création et le déplacement de nœuds vectoriels, la
création d'objets de broderie et le réordonnancement.

## Menu Image

Toutes ces opérations sont non destructives (pile de transformations sur
l'original) :

- Niveaux de gris ;
- Luminosité/contraste… (aperçu en direct) ;
- Débruitage léger / moyen (médian) ;
- Quantifier les couleurs… (k-means, nombre de couleurs 2–64) ;
- Symétrie horizontale / verticale ;
- Rotation 90° horaire / antihoraire ;
- Recadrer (sélection) — dessinez un rectangle sur le canevas.

## Menu Segmentation

- Segmenter l'image… (nombre de couleurs, taille min de région) ;
- Afficher la carte des régions (bascule) ;
- Fusionner avec… (puis clic sur la région cible) ;
- Supprimer la région sélectionnée (Suppr) — la région redevient du fond ;
- Recolorer la région sélectionnée… ;
- Convertir la région en objet vectoriel.

Sélection : cliquez une région ; ses statistiques (pixels, mm², RGB) s'affichent.

## Menu Broderie

- Numérisation automatique — crée des objets pour toutes les régions ;
- Créer un objet de point de contour… (longueur, type simple/double/triple) ;
- Créer un remplissage tatami… (espacement, longueur, angle) ;
- Créer une colonne satin… (densité, compensation, sous-couche centrale) ;
- Statistiques… (points, sauts, coupes, changements de couleur, dimensions,
  longueur de fil).

Avertissement : si une colonne satin dépasse la largeur recommandée, un dialogue
propose de continuer ou de préférer un remplissage tatami.

## Menu Affichage

| Action | Raccourci | Effet |
|---|---|---|
| Afficher les points | — | Bascule l'affichage des points générés |
| Afficher les vecteurs | — | Bascule l'affichage des objets vectoriels |
| Zoom avant | Ctrl++ | Agrandit |
| Zoom arrière | Ctrl+- | Réduit |
| Ajuster au canevas | Ctrl+0 | Cadre la vue sur le canevas |

Les points cousus sont tracés en trait continu foncé ; les **sauts** en
pointillés orange ; des pastilles marquent les pénétrations (masquées au-delà de
4000 points pour la fluidité, et pendant la simulation).

## Menu Analyse et panneau

**Analyser le motif** (F5) remplit le panneau *Analyse* (dock) avec les problèmes
détectés, triés par gravité. Un double-clic sur un problème centre la vue sur sa
localisation.

## Panneau Ordre de couture

Dock listant les objets de broderie dans l'ordre. Vous pouvez monter/descendre un
objet, le verrouiller (il ne bougera plus lors de l'optimisation), et appliquer
une stratégie (ordre du document, par couleur, par proximité, couleur puis
proximité). Un libellé affiche le coût estimé.

## Barre de simulation

Boutons de lecture/pause et un curseur qui révèle la couture jusqu'à un index de
point, avec un repère d'aiguille. La barre n'apparaît que lorsqu'une séquence de
points existe.

## Erreurs et messages

Les opérations impossibles (recadrage hors image, satin non constructible,
export d'une séquence vide…) affichent un message clair et **n'appliquent rien**.
Les erreurs connues et les entrées invalides **couvertes par les tests**
renvoient une erreur structurée au lieu de provoquer un arrêt du programme.
Aucun plantage n'a été observé dans le corpus de tests actuel — ce qui ne
constitue pas une garantie absolue en version 0.1.0.

Note : Les raccourcis proviennent des séquences standard de Qt
(`QKeySequence::Open`, `Save`, `Undo`, `Redo`, `Delete`, `ZoomIn`, `ZoomOut`,
`Quit`) et de `Ctrl+0`, `F5` définis explicitement.

## Implémentation associée

- `apps/desktop/main_window.cpp` — construction des menus, actions, docks, barre.
- `apps/desktop/canvas_view.cpp` — zoom, déplacement, grille, cadre, rendu points.
- `apps/desktop/ruler.cpp` — règles en mm.
- `apps/desktop/import_dialog.cpp`, `brightness_dialog.cpp` — dialogues.
