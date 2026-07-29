# Guide de prise en main

Public : utilisateur débutant. Ce tutoriel suit un flux réaliste, **adapté à ce
qui est réellement implémenté**. Les étapes non disponibles dans l'interface sont
signalées.

## Vue d'ensemble

Le flux typique est : importer une image → la préparer → la segmenter en régions
→ vectoriser → créer des objets de broderie (ou laisser l'auto-numérisation le
faire) → régler les points → organiser l'ordre → analyser → exporter en DST.

## 1. Ouvrir une image

**Fichier → Ouvrir une image…** (Ctrl+O). Choisissez un PNG, JPEG, BMP ou TIFF.
Un logo simple à quelques couleurs franches donne les meilleurs résultats.

## 2. Choisir les dimensions physiques

Un dialogue d'import demande la **taille physique** en millimètres (largeur et
hauteur), avec l'option « conserver les proportions ». La valeur par défaut
suppose 96 dpi. C'est ici que se fixe la résolution de travail (mm par pixel) —
elle ne changera plus ensuite.

Conseil : visez une taille qui tient dans le cadre affiché (100 × 100 mm par
défaut) ; sinon le motif dépassera du tambour.

## 3. Préparer l'image

Menu **Image** : niveaux de gris, luminosité/contraste (avec aperçu), débruitage,
symétries, rotations 90°, recadrage (par sélection au rectangle) et quantification
des couleurs. Toutes ces opérations sont **non destructives** : l'original est
conservé, chaque opération s'annule (Ctrl+Z).

## 4. Réduire les couleurs et segmenter

**Segmentation → Segmenter l'image…** : choisissez le nombre maximal de couleurs
et la taille minimale de région. Le logiciel quantifie en espace perceptuel
CIELAB puis extrait les **régions connexes**. Activez **Afficher la carte des
régions** pour les visualiser.

## 5. Nettoyer les régions

Cliquez une région pour la sélectionner (ses infos s'affichent dans la barre
d'état). Vous pouvez la **supprimer** (elle redevient du fond), la **recolorer**,
ou en **fusionner** deux (« Fusionner avec… » puis clic sur la cible).

## 6. Vectoriser

Avec une région sélectionnée : **Segmentation → Convertir la région en objet
vectoriel**. Le contour (et ses trous) devient un objet éditable ; ses **nœuds**
apparaissent et se déplacent à la souris.

## 7. Créer des objets de broderie

Sur un objet vectoriel sélectionné, menu **Broderie** :

- **Créer un objet de point de contour…** (point simple, double ou triple) ;
- **Créer un remplissage tatami…** (densité, longueur, angle) ;
- **Créer une colonne satin…** (densité, compensation, sous-couche) — avec un
  avertissement si la colonne est trop large.

Alternative rapide : **Broderie → Numérisation automatique** crée des objets pour
toutes les régions : **tatami** pour les zones remplissables, **contour** cousu
pour les petites (le satin automatique naïf, qui débordait, est désactivé par
défaut). On change ensuite le type d'une forme par **clic droit ▸ Type de
points**, et on règle l'orientation d'un tatami à la souris (poignée de rotation).

## 8. Régler les points et les couleurs

Les points se régénèrent automatiquement à chaque changement. La couleur d'un
objet reprend celle de sa région.

Limitation : il n'existe pas encore de **palette de fils** réelle ni de
sélection de fil par référence fabricant (voir *Palettes et fils*).

## 9. Organiser l'ordre de couture

Le panneau **Ordre de couture** liste les objets ; vous pouvez les monter/descendre,
les verrouiller, et appliquer une stratégie automatique (par couleur, par
proximité) avec une estimation de coût.

## 10. Simuler et analyser

La **barre de simulation** rejoue la couture point par point (lecture/pause,
curseur). **Analyse → Analyser le motif** (F5) liste les problèmes détectés
(points trop courts/longs, sauts trop longs, hors cadre) ; un double-clic centre
la vue sur le problème.

## 11. Exporter en DST

**Fichier → Exporter en DST…**. Le logiciel rappelle qu'un DST **ne conserve pas**
les objets éditables : gardez aussi votre projet `.osp`.

## 12. Vérifier le fichier

En ligne de commande : `openstitch-cli stats motif.dst` affiche points, sauts,
dimensions et longueur de fil estimée ; `openstitch-cli dst2svg motif.dst
apercu.svg` produit un aperçu vectoriel.

## Implémentation associée

- `apps/desktop/main_window.cpp` — tous les menus et actions ci-dessus.
- `apps/desktop/import_dialog.cpp` — dialogue de taille physique.
- `libs/autodigitize/src/autodigitize.cpp` — numérisation automatique.
- `apps/cli/main.cpp` — `stats`, `dst2svg`.
