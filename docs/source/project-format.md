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
  `params` (variant `running` | `tatami` | `satin`). Le `satin` porte ses deux
  rails et, depuis le schéma v2, ses **barreaux** (`rungs` : liste de segments
  `{ax, ay, bx, by}` en µm), ses réglages de finition (points courts, split,
  terminaisons), de sous-couche/compensation, et de **fixation/entrée-sortie**
  (`lockStart`/`lockEnd`, `lockLength`, `lockPasses`, `entryPoint`/`exitPoint`) —
  tous **optionnels** et rétrocompatibles (clés absentes → valeurs par défaut).
  Le `tatami` porte de même ses réglages avancés (Lot 7) : `underlayEdge`,
  `underlayParallel`, `underlayInset`, `underlaySpacing`, `hiddenUnderpath` et
  `entryPoint` — optionnels et rétrocompatibles. Depuis le schéma v3, un objet
  peut aussi porter ses **retouches manuelles** (Lot 8.1, ADR-014) :
  `overrides` (**tableau** JSON obligatoire de `{index, pos?, type?, trimAfter}`
  — `index` désigne une position dans la vue brute de l'objet et doit être
  **unique** dans le tableau, `pos` un déplacement `{x, y}` en µm, `type`
  `"stitch"` ou `"jump"` ; chaque entrée doit porter au moins une modification
  effective — `pos`, `type`, ou `trimAfter: true`), `editedFingerprint`
  (empreinte FNV-1a 64 bits de la vue brute au moment de la dernière édition,
  entier **exact**, jamais passé par un `double`) et `editedPointCount` —
  **obligatoires et explicites** dès que `overrides` n'est pas vide (aucune
  valeur zéro implicite). Absents, ou `overrides` vide (métadonnées
  éventuellement présentes mais alors ignorées) → objet `Clean` (comportement
  actuel inchangé).

## Versionnement et validation

`schemaVersion` vaut **3** (v1 → v2 : cadre `canvas` et barreaux satin
`rungs` ; v2 → v3 : retouches manuelles `overrides`/`editedFingerprint`/
`editedPointCount` par objet de broderie, Lot 8.1). La lecture est
**rétrocompatible** : un fichier v1 ou v2 se charge (cadre 100×100 par défaut
si absent, aucun barreau, aucune retouche → état `Clean`). Une version
**supérieure** à celle du binaire est refusée proprement (`UnsupportedFormat`) ;
un JSON invalide, une valeur hors bornes (index négatif, non entier, ou
au-delà de `numeric_limits<size_t>::max()`, coordonnée au-delà d'un `int32`,
compteur au-delà d'un `uint32`, type de point inconnu), un `overrides` qui
n'est pas un tableau, un `index` en double, une entrée sans modification
effective, des métadonnées `editedFingerprint`/`editedPointCount` manquantes
pour un tableau non vide, ou une carte de labels incohérente renvoie une
erreur utile (`InvalidFile`), jamais une troncature silencieuse.

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
- Tests : `tests/unit/project_io/test_roundtrip.cpp`,
  `tests/unit/project_io/test_overrides_persistence.cpp` (retouches v3,
  migration v1/v2, validation stricte).
