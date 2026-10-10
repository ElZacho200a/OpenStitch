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
  `params` (variant `running` | `tatami` | `satin` | `directional` | `autoSatin`). Le variant
  `autoSatin` (schéma v5) ne stocke **aucun rail** : colonnes et traversées sont dérivées du
  contour source ; il porte espacement, seuil `Lmax` et longueur `y`, réglages de finition,
  entrée/sortie et la liste de **guides** (`anchor` en µm, `angle`, `absolute`). Le `satin` porte ses deux
  rails et, depuis le schéma v2, ses **barreaux** (`rungs` : liste de segments
  `{ax, ay, bx, by}` en µm), ses réglages de finition (points courts, split,
  terminaisons), de sous-couche/compensation, et de **fixation/entrée-sortie**
  (`lockStart`/`lockEnd`, `lockLength`, `lockPasses`, `entryPoint`/`exitPoint`) —
  tous **optionnels** et rétrocompatibles (clés absentes → valeurs par défaut).
  Une section issue d'un réseau multi-rail peut aussi porter `topology` :
  `sectionIndex`, `sectionCount` et les identifiants optionnels
  `startJunction`/`endJunction`. Ces identifiants sont locaux au réseau défini
  par `sourceVector`; l'absence de `topology` désigne un satin isolé/historique.
  Le `tatami` porte de même ses réglages avancés (Lot 7) : `underlayEdge`,
  `underlayParallel`, `underlayInset`, `underlaySpacing`, `hiddenUnderpath` et
  `entryPoint` — optionnels et rétrocompatibles. Le `directional`
  (remplissage directionnel, voir *Remplissage directionnel*) porte `guides`
  et `breakLines` (listes de chemins ouverts), `rowSpacing`, `stitchLength`,
  `edgeWeight`, `inset`, `stagger`, les réglages de sous-couche du tatami,
  `hiddenUnderpath`, `sectorOverlap`, `handmade`, `handmadeIntensity` et
  `seed` — tous optionnels (défauts du modèle) ; son ajout n'a pas changé
  `schemaVersion`. Depuis le schéma v3, un objet
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
- `finishing` : finitions de la séquence (coupes automatiques, points d'arrêt,
  points courts -- `document::SequenceFinishing`) : `enabled`,
  `trimThreshold` (µm), `trimBeforeColorChange`, `lockType` (0 aucun, 1
  aller-retour, 2 triangle, 3 micro-zigzag), `lockLength` (µm), `lockPasses`,
  `filterShortStitches`, `minStitchLength` (µm). **Bloc absent** (projet
  antérieur) → finitions désactivées, séquence identique à avant ; clé absente
  dans un bloc présent → valeur par défaut. Ajout sans changement de
  `schemaVersion`. Clés ajoutées par le lot « moteur » (absentes = désactivé) :
  `splitLongStitches` (bool, défaut faux), `maxStitchLength` (µm, défaut 7000,
  borné à [1000 ; 12100]), `autoJoin` (bool, défaut faux ; entrée/sortie
  automatiques, HP-ENG-010).
- Lot « moteur de points », clés additives d'un objet de broderie, écrites
  seulement hors défaut et bornées à la lecture : `join` (0 hérite du projet, 1
  automatique, 2 désactivé) ; dans `params` : tatami `pullCompensation` (µm,
  [0 ; 3000], HP-ENG-001) et, pour tatami, directionnel, satin et auto-satin,
  `underlayMode` (0 manuelle, 1 automatique, HP-ENG-002) ; satin `border`
  (`width` µm [500 ; 20000], `side` 0 centré / 1 intérieur / 2 extérieur,
  `corner` 0 vifs / 1 arrondis, `pathSet`, `ring` : anneau suivi du vecteur
  source, HP-STI-004). Aucun changement de `schemaVersion` : un ancien lecteur
  ignore ces clés.

### Fil de nuancier d'un objet (schéma v6)

Un objet de broderie peut référencer un fil : `"thread": {"chart": "<id du
nuancier>", "code": "<référence fabricant>"}` (= `thread_palette::ThreadKey`).
Le champ est **facultatif** : absent → couleur libre. Le `rgb` de l'objet reste
la **seule source de rendu** et de blocs de couleur ; assigner un fil copie la
couleur du fil dans `rgb`. Un fil dont le nuancier n'est pas chargé (nuancier
importé retiré) n'altère donc ni l'affichage ni l'export : seul son nom n'est
plus résolu. **Migration** : un projet v1 à v5 se charge sans `thread` (couleurs
libres) ; il est enregistré en v6 à la prochaine sauvegarde (copie `.vN.osp.bak`
conservée, comme pour les autres migrations). Un `thread` sans `chart` ou `code`
texte est refusé (`InvalidFile`). Les **nuanciers eux-mêmes** ne sont jamais
stockés dans le `.osp` (voir *Palettes et fils*).

## Versionnement et validation

`schemaVersion` vaut **6** (v1 → v2 : cadre `canvas` et barreaux satin
`rungs` ; v2 → v3 : retouches manuelles `overrides`/`editedFingerprint`/
`editedPointCount` par objet de broderie, Lot 8.1 ; v3 → v4 : intermédiaire ; v4 → v5 :
variant `autoSatin`, auto-satin par squelette ; v5 → v6 : fil de nuancier
`thread` d'un objet de broderie, HP-THR-004). La lecture est
**rétrocompatible** : un fichier v1 ou v2 se charge (cadre 100×100 par défaut
si absent, aucun barreau, aucune retouche → état `Clean`). Une version
**supérieure** à celle du binaire est refusée proprement (`UnsupportedFormat`) ;
  un JSON invalide, une topologie satin incohérente (`sectionCount == 0` ou
  `sectionIndex >= sectionCount`), une valeur hors bornes (index négatif, non entier, ou
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
  migration v1/v2, topologie satin optionnelle/rétrocompatible, validation
  stricte).
