# Lignes ouvertes

Une forme est une **ligne ouverte** quand tous ses morceaux sont des chemins non fermés, sans
trou : un trait détecté sur l'image, un tracé à la main, un chemin SVG/DXF ouvert. C'est une
notion du moteur, déduite de la géométrie seule (`document::is_open_line`,
`libs/document/include/openstitch/document/shape_kind.hpp`) : aucun drapeau à poser.

Une forme mixte (morceaux ouverts et fermés) reste une surface : comportement historique.

## Types de points

Une ligne ouverte n'a pas d'intérieur. Seuls les points qui **suivent un chemin** la cousent :

| Type | Sur une ligne ouverte |
|---|---|
| Point droit (`running`, styles simple / triple / bean / tige / arrière) | oui |
| Boutonnière (`blanket`) | oui |
| Motif le long du contour (`motif_run`) | oui |
| Satin à rails (`SatinParams`, satin de bordure) | oui (rails ouverts) |
| Tatami, directionnel, auto-satin, concentrique, radial, spirale, croisé, à motif, point de croix, appliqué, fondu multicolore | refusé |

`document::stitch_needs_area(params)` dit si un type demande une surface. Un remplissage sur une
ligne ouverte fermerait le chemin en silence (corde entre les extrémités) : il est **refusé** au
lieu d'être produit.

## Où la règle s'applique

- **Opérations** (`project_ops`, donc Studio, CLI `project`, MCP) : `set_stitch_type`,
  `apply_recipe` et `auto_satin` refusent un type qui demande une surface sur une ligne ouverte
  (code `E_REFUSED`, message « ligne ouverte », indice avec les types possibles). Document
  inchangé.
- **`set_geometry`** accepte `closed:false` (2 sommets au moins) pour poser une ligne ouverte ;
  refusé si un objet de broderie de la forme demande une surface.
- **Auto-numérisation de formes existantes** (`auto_digitize_vectors`, donc import SVG direct et
  `convert_vectors`) : une ligne ouverte devient un point droit simple, ajouté **après** les
  surfaces du lot (cousu par-dessus), sans passer par la classification satin / tatami.
- **Inspecteur Studio** : si la sélection contient une ligne ouverte, la liste « Type de points »
  ne propose que contour, motif le long du contour et boutonnière.

La génération des points (`stitch_generation`) traitait déjà les chemins ouverts pour ces types
(point droit, boutonnière, motif, satin de bordure). Les fichiers existants ne changent pas :
un remplissage déjà posé sur une ligne ouverte dans un ancien projet reste généré comme avant.

## Traits de l'image (lot 2)

« Préserver les traits fins » détecte maintenant les traits par contraste local, quelle que soit
leur couleur, cousus chacun dans sa couleur ; la mise à jour des formes retire les morceaux de
trait absorbés par les aplats. Détail : `auto-numerisation.md`, « Détection par contraste local ».

## Suite prévue

Identifiant stable par trait (au lieu de l'appariement rang par rang) et retrait du drapeau
historique `VectorObject::linework` ; fidélité du tracé des treillis très fins. Dessin et édition d'une ligne ouverte dans Studio : lot 3.
