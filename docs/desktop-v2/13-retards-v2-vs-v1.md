# 13 — Ce que la V1 fait et que la V2 ne fait pas encore

État au 2026-11-06. Méthode : on a relevé les libellés des actions de la V1 (`apps/desktop/*.cpp` : menus,
barres, menus contextuels) et cherché chacun dans le code de la V2 (`apps/desktop_v2`). 171 libellés V1 sur
225 n'ont pas d'équivalent textuel dans la V2. Certains existent sous un autre nom (ex. « Ajuster au canevas »
↔ « Ajuster à la fenêtre ») : la liste ci-dessous a été triée à la main, mais **rien n'a été essayé à l'écran**.
Priorité : **P0** = un parcours de travail est bloqué ou très appauvri ; **P1** = gêne réelle ; **P2** = confort.

## Fichiers, import, export

| # | Manque dans la V2 | V1 | Prio |
|---|---|---|---|
| 1 | **Ouvrir une broderie machine** (DST, PES…) : la V2 n'ouvre que des images, des SVG et des `.osp` | « Importer une broderie machine », « Insérer un design » | P0 |
| 2 | **DXF** : import et export | « Importer un DXF », « Exporter en DXF » | P1 |
| 3 | Export image en **JPEG** (seul le PNG existe) ; export du rendu réaliste en PNG dédié | « Exporter l'image (PNG, JPEG) » | P2 |
| 4 | « Exporter avec les derniers réglages » (un clic) | menu Fichier | P1 |
| 5 | **Gabarit de placement 1:1** et aperçu avant impression | production | P1 |
| 6 | Export du **contour de découpe** de l'appliqué | « Exporter le contour de découpe » | P1 |
| 7 | **Liste des fils en CSV**, import de **nuanciers CSV/JSON** | threads | P1 |
| 8 | DST compensé du **décalage entre couleurs** | « Exporter un DST compensé » | P2 |
| 9 | Rapport de plantage | crash report | P2 |

## Segmentation et image (le plus gros recul)

| # | Manque | V1 | Prio |
|---|---|---|---|
| 10 | **Re-segmenter un projet ouvert** avec d'autres réglages (couleurs, taille mini, lissage) avec aperçu sur l'image ; V2 : seulement à l'import | boîte de segmentation + aperçu sur l'image | P0 |
| 11 | **Réduire / fusionner les fils (palette)** avec aperçu : nombre de couleurs, distance, fusion des voisines | panneau « couleurs de segmentation » | P0 |
| 12 | Quantifier les couleurs de l'image | menu Image | P1 |
| 13 | **Vectoriser la sélection** (une région → un contour) | « Vectoriser la sélection » | P0 |
| 14 | Fusion en cliquant la région cible, région suivante / précédente, rétablir la couleur d'origine, menu contextuel de région | `main_window_regions` | P1 (en cours : sélection partagée faite, voir `12`) |
| 15 | Effacer toutes les zones de détail ; segmentation par IA / SAM | détail, IA | P1 |
| 16 | Afficher la carte des régions comme couche d'affichage | menu Affichage | P2 |

## Création et types de points

| # | Manque | V1 | Prio |
|---|---|---|---|
| 17 | **Types de remplissage** : la liste V2 n'offre que contour, tatami, satin, directionnel, spirale, point de croix, appliqué. Absents : blanket, motif le long du contour, remplissage à motif, concentrique, radial, croisé (moteur prêt, mais `set_stitch_type` ne les accepte pas) | « Type de points (tous) » | P0 |
| 18 | **Guides de direction, lignes de rupture, guide depuis la forme, centre automatique, orientation du remplissage** (outils déclarés mais désactivés) | `main_window_directional`, `fill_center` | P0 |
| 19 | Édition **fine du satin** : réglages fins, guides satin (ajouter / supprimer), repères d'orientation, bords seuls, « Convertir les satins auto en tatami » | `main_window_satin_auto` | P0 |
| 20 | **Convertir les vecteurs en broderie** / créer un contour, un tatami, un satin auto avec réglages | menu Broderie | P1 |
| 21 | **Appliqué** : « Convertir en appliqué » en un geste | `main_window_applique` | P1 |
| 22 | Transition douce des nœuds voisins ; poignées du dégradé (axe du fondu fait, pas la transition de nœuds) | nodes | P2 |
| 23 | Options de génération (paramètres globaux de génération des points) | menu | P1 |

## Vecteur

| # | Manque | V1 | Prio |
|---|---|---|---|
| 24 | **Formes prédéfinies** ; polygone régulier ; décaler la forme, contour autour de la sélection | vector tools | P1 |
| 25 | Accrochage à la grille, pas de la grille, accrochage des nœuds | vector_edit | P1 |
| 26 | Aligner sur…, convertir le type de plusieurs objets, « Supprimer le texte » | arrange, lettering | P2 |
| 27 | Rappeler / retrouver une sélection mémorisée | `selection_tools` | P2 |

## Points (retouche)

| # | Manque | V1 | Prio |
|---|---|---|---|
| 28 | **Retouche de points** : V2 = clic-clic seulement. Absents : sélection multiple, supprimer / restaurer, passer en saut ou en point cousu, couper le fil après, tout sélectionner | `main_window_point_edit` | P0 |

## Analyse

| # | Manque | V1 | Prio |
|---|---|---|---|
| 29 | **Fidélité à l'image** (comparaison broderie / image), **statistiques**, **diagnostic des défauts connus**, centrer la vue sur un problème | `main_window_analysis` | P1 |
| 30 | Carte de **tirage** du tissu et aperçu avec tirage ; décalage entre couleurs | pull preview, colour registration | P1 |
| 31 | Sauts à vide de l'ordre (surimpression) | `order_jumps` | P1 |

## Affichage et navigation

| # | Manque | V1 | Prio |
|---|---|---|---|
| 32 | **Commandes de zoom** : avant, arrière, sélection, précédent, 100 % (taille réelle), rectangle, étalonnage ; V2 : molette et « Ajuster » seulement | `view_display` | P0 |
| 33 | **Grille et guides** : guides horizontaux / verticaux, effacer, couleur, réglages de la grille | `view_display` | P1 |
| 34 | **Plans d'affichage** (vecteurs / broderie / image), passes de couture, aperçu en direct pendant la saisie | menu Affichage | P1 |
| 35 | **Filtres d'ordre** : afficher uniquement ce type / cette couleur, tout afficher | `order_filter`, menu contextuel | P1 |
| 36 | Simulation : navigation point par point, objet par objet, couleur par couleur ; V2 = curseur + lecture | `simulation_nav_bar` | P1 |
| 37 | Disposition : réinitialiser, restaurer la précédente, mode portable, masquer les panneaux | `ux` | P2 |
| 38 | Coloris et tissu (fenêtre de coloris), réduction des fils | `colorways` | P1 |

## Préférences, aide

| # | Manque | V1 | Prio |
|---|---|---|---|
| 39 | Préférences générales et IA dans une fenêtre | menu | P1 |
| 40 | **Aide** : guide de prise en main, tutoriel « de l'image à la broderie », manuel, aide sur l'outil actif, gestes souris et clavier | `help_menu` | P1 |
| 41 | Pinceau : réglages, rayon + / − au clavier | `brush` | P1 (voir `12`) |

## Ordre de rattrapage proposé

1. Ouvrir DST/PES, DXF (1, 2) · types de remplissage complets (17) · guides de direction et lignes de rupture (18).
2. Segmentation : re-segmenter avec aperçu (10), réduction de palette (11), vectoriser une région (13).
3. Retouche de points complète (28) · zoom, grille, guides (32, 33).
4. Satin : édition fine et guides (19).
5. Analyse (29-31), affichage (34-36), aide (40), le reste.
