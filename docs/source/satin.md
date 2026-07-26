# Colonne satin

Public : utilisateur avancé, développeur. État : **Implémenté** (base) ;
auto-satin **partiel**.

## Définition

Une colonne satin est un zigzag serré entre **deux rails** (bords gauche et
droit) qui remplit une bande. Elle donne un effet lisse et brillant, adapté aux
lettrages et aux bordures étroites.

## Modèle

`SatinParams` porte `rail_a` et `rail_b` (deux `geometry::Path`), la densité
(`density`), la compensation de tirage (`pull_compensation`), et l'activation
d'une sous-couche centrale (`center_underlay`).

## Génération

`fill_satin(rail_a, rail_b, config)` :

1. ré-échantillonne **les deux rails** par fraction d'abscisse curviligne (les
   rails peuvent avoir des longueurs et courbures différentes) ;
2. produit un **zigzag alterné** d'un bord à l'autre (L0, R0, L1, R1, …) ;
3. la **densité** fixe le pas le long de la colonne ;
4. la **compensation de tirage** écarte les deux rails le long de leur
   médiatrice, pour compenser le resserrement du fil ;
5. la **sous-couche centrale** (optionnelle) est un point droit grossier sur
   l'axe, cousu avant le zigzag.

Le résultat (`SatinResult`) fournit les points de sous-couche, du satin, et la
**largeur maximale** rencontrée (pour l'avertissement).

## Colonne trop large

À la création, si la largeur dépasse le seuil recommandé, l'interface **prévient**
et propose de préférer un remplissage tatami — la limite physique n'est jamais
masquée.

## Rails automatiques

`rails_from_contour(contour)` découpe un contour fermé en deux rails, coupé aux
**deux sommets les plus éloignés** (les « bouts » de la colonne). Cela convient
aux formes allongées ; l'auto-numérisation l'utilise pour proposer un satin.

Limitation : c'est une heuristique. Il n'y a **pas** d'axe médian (medial axis /
straight skeleton), pas de barreaux de direction, pas de correspondance par
sections, pas de gestion fine des points courts dans les virages, ni de split
stitch pour les colonnes très larges. Ces éléments (§12 de l'étude de cadrage)
sont **prévus**. La densité est mesurée le long du rail, pas encore strictement
perpendiculairement au fil.

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — `SatinParams`.
- `libs/stitch_generation/include/openstitch/stitch_generation/satin.hpp`
- `libs/stitch_generation/src/satin.cpp` — `fill_satin`, `rails_from_contour`.
- `libs/stitch_generation/src/generate.cpp` — `generate_satin`.
- Tests : `tests/unit/stitch/test_satin.cpp`.
