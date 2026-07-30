# Format de projet `.osp`

Public : développeur, mainteneur. État : **Implémenté**.

## Nature

Un projet `.osp` est une **archive ZIP** (via minizip-ng) contenant :

| Entrée | Contenu |
|---|---|
| `project.json` | tout le document, en JSON versionné |
| `original.png` | l'image source, encodée PNG (sans perte) |
| `segmentation.u32` | la carte des labels, en binaire little-endian (si présente) |

Le JSON et le binaire sont séparés pour ne pas alourdir le JSON (la carte des
labels peut faire plusieurs mégaoctets).

## Contenu de `project.json`

- `schemaVersion` (entier) et un objet `document` ;
- `mmPerPx` (résolution de travail) et `objectIdLast` (compteur d'ids) ;
- `canvas` : taille du cadre de broderie (`width`, `height` en µm). **Optionnel
  et rétrocompatible** : un projet antérieur sans ce champ retombe sur 100×100 mm ;
- `ops` : la pile d'opérations d'image (chaque opération porte son `type`) ;
- `segmentation` : dimensions et régions (id, rgb, nombre de pixels ; les labels
  sont dans `segmentation.u32`) ;
- `vectorObjects` : id, nom, couleur, visibilité, région source, `paths`
  (chemins avec nœuds et tangentes optionnelles) ;
- `embroideryObjects` : id, nom, couleur, visibilité, vecteur source, et
  `params` (variant `running` | `tatami` | `satin`).

## Versionnement et validation

`schemaVersion` vaut actuellement **1**. À la lecture, une version inconnue ou
supérieure est **refusée** proprement (`UnsupportedFormat`) ; un JSON invalide ou
une carte de labels incohérente avec ses dimensions renvoie une erreur.

## Sauvegarde atomique

`save_project` écrit d'abord un fichier temporaire `.osp.tmp` puis le **renomme** :
un plantage pendant l'écriture ne corrompt jamais le fichier existant.

## Aller-retour

Le test d'intégration vérifie que `save` puis `load` restitue exactement l'image
(PNG sans perte), les opérations, les labels de segmentation, les tangentes de
nœuds et les paramètres des trois types de points.

Limitation : la **sauvegarde automatique**, la **récupération après crash** et
les **migrations** entre versions de schéma sont **prévues**, non implémentées.
Le cache des points générés n'est pas stocké (il est recalculé au chargement).

## Implémentation associée

- `libs/project_io/include/openstitch/project_io/project_io.hpp` —
  `save_project`, `load_project`, `kSchemaVersion`.
- `libs/project_io/src/json_serialize.cpp` — sérialisation du document.
- `libs/project_io/src/archive.cpp` — lecture/écriture ZIP (minizip-ng encapsulé).
- `libs/project_io/src/project_io.cpp` — orchestration, écriture atomique.
- Tests : `tests/unit/project_io/test_roundtrip.cpp`.
