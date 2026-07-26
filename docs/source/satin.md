# Colonne satin

Public : utilisateur avancé, développeur.

> État : Présent dans le code : oui · Tests unitaires : oui · Tests visuels :
> partiels · Import/export DST : oui · Test sur machine réelle : **non** ·
> **Statut recommandé : expérimental** — génération satin simple à deux rails.
> Plusieurs propriétés d'un générateur satin correct manquent (voir
> *Limitations* ci-dessous). Vérifiez impérativement le résultat avant tout
> passage sur machine.

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
3. la **densité** fixe le pas d'avancement le long de la colonne (mesuré **le
   long du rail**, par fraction d'abscisse curviligne) ;
4. la **compensation de tirage** écarte les deux rails le long de leur
   médiatrice, pour compenser le resserrement du fil ;
5. la **sous-couche centrale** (optionnelle) est un point droit grossier sur
   l'axe, cousu avant le zigzag.

Avertissement : la densité est mesurée **le long du rail**, pas
perpendiculairement aux fils. Dans les sections **inclinées ou courbes**,
l'espacement visuel réel entre fils diffère alors de la consigne (les fils se
resserrent ou s'écartent selon l'angle). Une mesure correcte projetterait
l'avancement sur la normale aux fils ; ce n'est pas encore le cas.

Le résultat (`SatinResult`) fournit les points de sous-couche, du satin, et la
**largeur maximale** rencontrée (pour l'avertissement).

## Paramètres

Valeurs par défaut lues dans `SatinParams`.

| Paramètre | Unité | Défaut | Effet |
|---|---|---|---|
| `density` | mm | 0,4 | pas d'avancement le long de la colonne (plus petit = plus dense) |
| `pull_compensation` | mm | 0 | élargit la colonne (compense la traction du fil) |
| `center_underlay` | bool | vrai | ajoute une sous-couche centrale (point droit sur l'axe) |
| `max_width` | mm | 9 | seuil d'avertissement de largeur excessive |

> Validation physique : non effectuée. La densité satin devrait idéalement se
> mesurer perpendiculairement au fil (voir l'avertissement ci-dessus).

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
