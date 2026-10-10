# Guide de prise en main

Public : utilisateur débutant. Ce tutoriel suit un flux réaliste, **adapté à ce
qui est réellement implémenté**. Il n'a qu'une voie pour débuter : **image →
numérisation automatique → auto-satin sur les bandes fines → export DST**. Le
détail de chaque menu est dans le [Guide utilisateur détaillé](user-guide.md) ;
l'installation (sans compiler) est dans [Installation](installation.md).

Important : le fichier produit **n'a pas encore été validé sur une machine à
broder réelle** (voir [Limitations](limitations.md)). Faites un essai sur chute de
tissu avant tout ouvrage.

## Vue d'ensemble

Image → préparation → segmentation en régions de couleur → **numérisation
automatique** (objets éditables) → **satin automatique** pour les formes allongées
→ vérification (analyse, simulation) → export DST. Tout est annulable
(**Ctrl+Z**) et le projet `.osp` conserve l'image, les régions et les objets
éditables, ce que le DST ne fait pas.

## 1. Ouvrir une image

**Fichier → Ouvrir une image…** (Ctrl+O). Choisissez un PNG, JPEG, BMP, TIFF ou
SVG. Un logo simple à quelques couleurs franches donne les meilleurs résultats.
Les fichiers récents sont dans **Fichier → Récents**.

## 2. Choisir les dimensions physiques

Un dialogue d'import demande la **taille physique** en millimètres (largeur et
hauteur), avec l'option « conserver les proportions ». La valeur par défaut
suppose 96 dpi. C'est ici que se fixe la résolution de travail (mm par pixel) —
elle ne changera plus ensuite.

Conseil : visez une taille qui tient dans le cadre affiché (100 × 100 mm par
défaut, réglable par **Affichage → Taille du cadre…**) ; sinon le motif dépassera
du tambour.

## 3. Préparer l'image

Menu **Image** : niveaux de gris, luminosité/contraste (avec aperçu), débruitage,
symétries, rotations 90°, recadrage (par sélection au rectangle) et quantification
des couleurs. Toutes ces opérations sont **non destructives** : l'original est
conservé, chaque opération s'annule (Ctrl+Z).

## 4. Segmenter en régions

**Segmentation → Segmenter l'image…** : choisissez le nombre maximal de couleurs
et la taille minimale de région. Le logiciel quantifie en espace perceptuel
CIELAB puis extrait les **régions connexes**. Activez **Afficher la carte des
régions** pour les visualiser. Cliquez une région pour la sélectionner (ses
infos s'affichent dans la barre d'état) ; vous pouvez la **supprimer** (elle
redevient du fond), la **recolorer** ou la **fusionner** avec une voisine
(« Fusionner avec… » puis clic sur la cible).

## 5. Numériser automatiquement

**Broderie → Numérisation automatique** crée d'un coup les objets éditables de
toutes les régions. Le dialogue propose :

- **Ignorer la plus grande région** : cochée d'office seulement si elle ressemble
  à un fond quasi blanc qui encadre le motif (la couleur et la part de l'image
  sont affichées pour vous laisser trancher) ;
- la stratégie **Formes pleines** (remplissages) ou **Contours (dessin au
  trait)**, pour un dessin fait de traits ;
- un curseur de détail.

Les zones remplissables deviennent des **tatamis**, les petites formes des
contours cousus. Le satin n'est **pas** posé automatiquement : on le demande
région par région, à l'étape suivante. Pour comprendre ce qui se passe, voir
[Numérisation automatique](auto-numerisation.md).

## 6. Auto-satin pour les bandes fines

Pour une forme allongée (lettre, trait épais, bordure), sélectionnez la forme
puis **Broderie → Créer un satin automatique…**. Le logiciel construit la
colonne par **squelette** de la forme, **sans que vous posiez de rails** :
l'aperçu annonce le nombre de colonnes et la couverture estimée. S'il **refuse**
la forme (disque, forme compacte…), il explique pourquoi et propose un tatami à
la place. Réglages : espacement, compensation de tirage, sous-couche,
fractionnement des traversées longues.

L'entrée voisine **Convertir automatiquement en satin (expérimental)…** utilise
le même moteur avec des réglages par défaut et une simple confirmation.

> Limitation : l'auto-satin par squelette est **expérimental et non validé sur
> machine**. Vérifiez l'aperçu et la simulation avant de broder, et préférez un
> tatami en cas de doute (clic droit ▸ Type de points).

On change ensuite le type d'une forme par **clic droit ▸ Type de points**, et on
règle l'orientation d'un tatami à la souris (poignée de rotation) ou par
**Orientation du remplissage…**.

## 7. Régler les points et les couleurs

Les points se régénèrent automatiquement à chaque changement ; l'**inspecteur**
(à droite) expose les paramètres de couture de l'objet sélectionné (densité,
longueur, compensation, sous-couche…). La couleur d'un objet reprend celle de sa
région : le logiciel gère des couleurs **RGB libres**, pas encore des références
de fils de fabricant (voir [Palettes et fils](palettes-and-threads.md)).

## 8. Organiser l'ordre de couture

Le panneau **Ordre de couture** liste les objets ; vous pouvez les monter/descendre,
les verrouiller, et appliquer une stratégie automatique (par couleur, par
proximité) avec une estimation de coût.

## 9. Simuler et analyser

La **barre de simulation** rejoue la couture point par point (lecture/pause,
curseur). **Analyse → Analyser le motif** (F5) liste les problèmes détectés
(points trop courts/longs, sauts trop longs, hors cadre) ; un double-clic centre
la vue sur le problème.

## 10. Exporter en DST

**Fichier → Exporter en DST…** (Ctrl+E) : un résumé (dimensions, points, coupes,
changements de couleur) s'affiche avant l'écriture. Le DST **ne conserve pas**
les objets éditables : gardez aussi votre projet `.osp` (**Ctrl+S**). Le seuil
de coupe de fil, la coupe avant changement de couleur et les points d'arrêt se
règlent dans **Broderie → Options de génération…** ; le fichier se termine par
une coupe finale. Ces coupes sont **à tester sur votre machine** (voir
[Format DST](dst-format.md)).

## 11. Vérifier le fichier

En ligne de commande : `openstitch-cli stats motif.dst` affiche points, sauts,
dimensions et longueur de fil estimée ; `openstitch-cli dst2svg motif.dst
apercu.svg` produit un aperçu vectoriel. Voir [Ligne de commande](cli.md).

Si la machine ne lit pas le fichier ou si quelque chose ne va pas :
[Dépannage](troubleshooting.md).

## Avancé : colonne satin à rails manuels

Réservé aux besoins que l'auto-satin ne couvre pas. **Cette voie est en partie
archivée** (voir [Colonne satin](satin.md) : seules certaines parties sont encore
maintenues). L'outil **Colonne satin** (touche `S` dans la palette) trace deux
rails à la main. Pour une colonne satin sélectionnée, **Broderie ▸ Modifier la
colonne satin (rails + guides)…** (`Maj+E`) affiche les nœuds des rails et les
barreaux d'orientation : glissez une extrémité le long de son rail pour infléchir
localement les points ; **Ajouter un guide satin** (`Maj+G`) partage le plus grand
intervalle ; le logiciel garde au moins deux guides et chaque geste est annulable.
Le sous-menu **Remodelage satin (avancé)** n'affiche que les rails ou que les
guides.

## Implémentation associée

- `apps/desktop/main_window.cpp` — tous les menus et actions ci-dessus.
- `apps/desktop/main_window_satin_auto.cpp` — « Créer un satin automatique ».
- `apps/desktop/import_dialog.cpp` — dialogue de taille physique.
- `libs/autodigitize/src/autodigitize.cpp` — numérisation automatique.
- `apps/cli/main.cpp` — `stats`, `dst2svg`.
