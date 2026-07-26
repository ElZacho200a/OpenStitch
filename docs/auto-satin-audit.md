# Audit — conversion automatique en satin (état actuel)

Cet audit précède l'implémentation du moteur d'auto-satin (mission cadrée par la
§43 : audit + satinabilité + rasterisation + distance + squelette + graphe +
SVG de debug + tests ; **pas** de rails définitifs ni de points satin).

## Comment une forme est actuellement convertie en satin

Deux endroits :

1. **`rails_from_contour(contour)`** (`libs/stitch_generation/src/satin.cpp`) :
   - trouve les **deux sommets les plus éloignés** du contour (diamètre, O(n²)) ;
   - coupe le contour fermé en **deux chaînes** entre ces deux sommets ;
   - inverse la seconde pour que les deux rails aillent du même bout au même bout.
2. **`auto_digitize`** (`libs/autodigitize/src/autodigitize.cpp`) : choisit le
   satin si la région est « fine » (`meanWidth = 2·aire/périmètre ≤ satin_max_width`),
   **sans trou**, et si `rails_from_contour` réussit ; sinon tatami ou contour.

La colonne satin (`document::SatinParams`) ne contient alors que `rail_a` et
`rail_b` (deux `geometry::Path`), une densité, une compensation et une
sous-couche. Le générateur `fill_satin` ré-échantillonne les deux rails par
**fraction d'abscisse curviligne** et zigzague entre eux.

## Pourquoi l'algorithme actuel échoue

`rails_from_contour` est une **découpe naïve du contour en deux moitiés** aux deux
points les plus éloignés. C'est exactement l'une des approches que la mission
interdit. Défauts :

- **Aucun axe / squelette** : la direction des fils n'est pas contrôlée ; les
  deux « rails » sont deux arcs du contour, pas les deux bords d'une bande.
- **Correspondance par fraction d'abscisse** entre rails de longueurs très
  différentes → fils obliques incohérents dans les virages.
- **Formes branchées (Y, T, croix)** : les deux points les plus éloignés sont
  deux extrémités ; une branche entière se retrouve mélangée dans un rail →
  géométrie absurde. Une forme en Y devient **une seule** colonne.
- **Anneaux / trous** : `auto_digitize` refuse (condition `holes.empty()`), mais
  `rails_from_contour` seul ne gère pas les trous.
- **Formes concaves** : les rails peuvent se croiser.
- **Densité** mesurée le long du rail, pas perpendiculairement au fil.

## Réponses aux questions imposées

- **Cas qui fonctionnent** : bande allongée quasi rectangulaire ou légèrement
  courbe, sans branche ni trou, dont les deux bords longs sont bien les deux
  chaînes séparées par les extrémités.
- **Cas qui ne fonctionnent pas** : Y/T/croix, S très courbé, anneau, cercle,
  forme large, formes à trous, largeur très variable.
- **Rails actuels** : deux arcs du contour (voir ci-dessus).
- **Barreaux** : **aucun** (`SatinRung` n'existe pas).
- **Axe central** : **non représenté**.
- **Formes branchées reconnues** : **non**.
- **Trous gérés** : non par `rails_from_contour` (refus en amont par
  `auto_digitize`).
- **Dépendance au nombre de nœuds du contour** : oui — les rails sont des
  sous-listes de nœuds du contour ; leur qualité dépend directement de la
  densité et de la position des sommets.
- **Direction dépendante de l'ordre des points** : oui — le sens de parcours du
  contour détermine quel arc devient `rail_a`.
- **Déterministe** : oui (pas d'aléa), mais fragile (dépend de la géométrie).
- **Le format projet peut-il stocker la nouvelle géométrie ?** Partiellement :
  `.osp` sérialise déjà `rail_a`/`rail_b`. Il **ne stocke pas** de barreaux
  (`SatinRung`), d'axe source, ni d'identifiant de branche — une **migration de
  schéma** sera nécessaire (mission ultérieure).

## Exemples minimaux reproduisant les défauts

Le corpus procédural (dans `apps/cli`, sous-commande `auto-satin-debug`) fournit :
`rectangle`, `capsule`, `ribbon` (bande courbe), `s` (forme en S), `y`, `t`,
`cross`, `circle`, `ring`. Pour chacun, le SVG de debug superpose **les rails
actuels** (`rails_from_contour`) et le **squelette calculé** : on y voit
directement que, sur le `y`, les deux rails actuels enjambent deux branches, là
où le squelette montre une jonction et trois branches.

## Périmètre corrigé par cette mission

Cette mission **n'implémente pas** les rails définitifs ni les points. Elle pose
les fondations (satinabilité, rasterisation, distance, squelette, graphe) et les
outils de diagnostic (SVG), pour que la construction des rails/barreaux et la
génération satin (missions suivantes) reposent sur un **axe médian** correct et
une **analyse de satinabilité** qui refuse proprement les formes inadaptées.

## Implémentation associée (existant)

- `libs/stitch_generation/src/satin.cpp` — `rails_from_contour`, `fill_satin`.
- `libs/document/.../embroidery_object.hpp` — `SatinParams` (rails, densité…).
- `libs/autodigitize/src/autodigitize.cpp` — choix du type (heuristique largeur).
- `libs/project_io/src/json_serialize.cpp` — sérialisation `SatinParams`.
