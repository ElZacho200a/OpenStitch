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

Compression (2026-10) : l'image PNG, déjà compressée, est stockée telle quelle dans le ZIP ; le JSON et les étiquettes sont en DEFLATE niveau rapide. Le fichier reste un ZIP standard, lisible par les versions précédentes.

## Contenu de `project.json`

- `schemaVersion` (entier) et un objet `document` ;
- `mmPerPx` (résolution de travail) et `objectIdLast` (compteur d'ids) ;
- `canvas` : taille du cadre de broderie (`width`, `height` en µm) et marge de
  sécurité optionnelle (`margin`, µm, écrite seulement si > 0 : la zone brodable est le
  cadre réduit de cette marge). **Optionnel et rétrocompatible** : un projet
  antérieur sans ce champ retombe sur 100×100 mm, et sans `margin` sur une marge nulle ;
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
  `hiddenUnderpath`, `sectorOverlap`, `handmade`, `handmadeIntensity`,
  `seed`, `spacingRegularity` (réel [0 ; 1], absent = 0 ; borné à la
  lecture) et l'objet `densityGradient` (`fromX`, `fromY`, `toX`, `toY`,
  `spacingFrom`, `spacingTo`, µm entiers ; `curve` entier 0 linéaire / 1 douce /
  2 marquée, absent = 0 ; absent = écart uniforme) — tous
  optionnels (défauts du modèle) ; son ajout n'a pas changé
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
  Sous-bloc `hiddenTravel` (HP-SEQ-006, absent = désactivé) : `enabled` (bool,
  défaut faux), `maxHiddenJump` (µm, défaut 2000, [0 ; 20000]),
  `maxBridgeLength` (µm, défaut 15000, [0 ; 100000]), `bridgeStitchLength` (µm,
  défaut 2500, [500 ; 12100]), `coverThreadWidth` (µm, défaut 400, [100 ; 2000]),
  `coverMargin` (µm, défaut 150, [0 ; 2000]), `allowSameColorSurface` (bool,
  défaut faux), `betweenObjects`, `withinObject`, `reorderUnderlay` (bool, défaut vrai : niveaux de
  l'optimisation des trajets), `reorderRegions` (bool, défaut faux : mode avancé).
- Lot « moteur de points », clés additives d'un objet de broderie, écrites
  seulement hors défaut et bornées à la lecture : `join` (0 hérite du projet, 1
  automatique, 2 désactivé) ; `connector` (HP-SEQ-005 : 0 hérite, 1 toujours couper, 2 ne jamais couper) ; dans `params` : tatami `pullCompensation` (µm,
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

### Méthode de l'auto-satin (schéma v7, conditionnel)

L'objet `autoSatin` porte la **méthode de calcul de ses traversées**, choisie par
l'utilisateur pour sa région :

- `strategy` : **nom** de la méthode, jamais un entier : `parallel_sweep` (balayage parallèle)
  ou `medial_axis` (axe médian vectoriel). Clé **absente** = squelette (le défaut) ;
- `sweepAngle` (radians, repère du modèle) : angle des traversées du balayage parallèle ;
  absent = automatique (perpendiculaire à l'axe principal de la forme).

**Règle de version** : le fichier n'est écrit en `schemaVersion` **7** que s'il contient au
moins un auto-satin dont la méthode n'est pas le squelette ; sinon il reste en **6** et se lit
dans les versions précédentes. Raison : un lecteur v6 ignorerait la clé `strategy` et
régénérerait un **autre** satin à l'export, à l'insu de l'utilisateur ; mieux vaut qu'il
refuse le fichier v7. Une méthode **inconnue** (fichier d'une version plus récente) est
refusée au chargement avec un message nommé : elle n'est jamais remplacée par le squelette.
Le presse-papiers d'objets transporte la méthode comme le fichier.

### Typographie fine d'un texte (schéma v8, conditionnel)

Un objet texte (`textObjects`) peut porter deux réglages de lettrage (HP-TXT-007, HP-TXT-012) :

- `kerningPairs` : tableau de `{left, right, adjust}` — `left`/`right` sont des **points de
  code Unicode** (entiers), `adjust` un écart en µm entiers (négatif = rapproche) qui
  **remplace** le crénage de la police pour la paire. Clé **absente** = aucun crénage manuel ;
  une entrée dont `left` ou `right` vaut 0 est ignorée à la lecture ;
- `ligatures` : booléen (ff, fi, fl, ffi, ffl quand la police les porte). Clé **absente** =
  `false`.

Ces clés ne sont écrites **que si elles sortent du défaut** (tableau non vide, `ligatures`
vrai). **Règle de version** (même principe que la méthode d'auto-satin) : le fichier n'est
écrit en `schemaVersion` **8** (`kSchemaVersionTextTypography`) que si au moins un texte
porte l'un de ces réglages ; sinon il garde sa version antérieure (6 ou 7), **octet pour
octet**. Raison : un lecteur v7 ignorerait ces clés et recomposerait le texte autrement à
la prochaine édition ; mieux vaut qu'il refuse le fichier v8. Les points ne sont toujours pas
stockés : les mêmes champs vides redonnent exactement les mêmes points qu'avant
(`tests/unit/lettering/test_text_typography.cpp`).

### Calques du document (schéma v9, conditionnel)

Le document peut définir des calques (HP-OBJ-024) : une clé `calques` au niveau du document,
tableau ordonné du **fond vers le dessus** de `{id, name, visible, locked}`, et une clé `calque`
(identifiant) sur les objets vectoriels. Un objet sans `calque`, ou dont le calque est inconnu,
appartient au calque du fond ; sans clé `calques`, le document a un seul calque implicite. Le rang
du calque est la clé principale de l'ordre de couture, l'ordre des `embroideryObjects` ne
départageant que les objets d'un même calque.

Le fichier n'est écrit en **v9 que si le document définit des calques** ; sinon il garde sa
version antérieure, octet pour octet. Raison : un lecteur v8 ignorerait les calques et coudrait
dans un autre ordre (`tests/unit/project_io/test_calque_persistence.cpp`).

## Versionnement et validation

`schemaVersion` vaut **6**, **7** si un auto-satin utilise une méthode autre que le squelette, ou **8** si un texte porte un crénage manuel ou des ligatures, ou **9** si le document définit des calques (v8 → v9 : clés `calques` et `calque`, voir plus haut ; v7 → v8 : `kerningPairs` et `ligatures` d'un texte, voir plus haut ; v6 → v7 : clé `strategy` et `sweepAngle` de l'auto-satin, voir plus haut ; v5 → v6 : fil de nuancier `thread` des objets de broderie (`{chart, code}`, voir *Palettes et fils*) ; objets texte du lettrage, clé `textObjects`
(id, text, fontFamily/fontFile/fontBuiltin/faceIndex, capHeight, letterSpacing, wordSpacing,
lineSpacing, kerning, align, justifyWidth, origin, rotation, rgb, fill, maxSatinWidth, density ;
µm entiers, rotation en radians) et `textOwner` sur les objets vectoriels, voir `lettering.md` ;
v1 → v2 : cadre `canvas` et barreaux satin
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

### Migrations de schéma (HP-FMT-021)

`libs/project_io/src/migrations.cpp` tient une **table d'étapes versionnées** `vN → vN+1`
(`migration_steps()`), contiguë de v1 à `kMaxReadableSchemaVersion - 1` (garde
`static_assert`). `load_project` appelle `migrate_project_json` après les contrôles de
version : le JSON d'un fichier de version N subit, dans l'ordre, les étapes N, N+1, … jusqu'à
la version lisible la plus récente, puis seulement `project_from_json` le lit. Un fichier
déjà à jour ne subit aucune étape (idempotent) ; la sortie est déterministe.

| Étape | Transformation explicite |
|---|---|
| v1 → v2 | satin sans `rungs` → `rungs: []` ; document sans `canvas` → 100×100 mm |
| v2 → v3 | aucune réécriture (absence de `overrides` = objet `Clean`) |
| v3 → v4 | satin sans `maxWidthHard` → 48 000 µm |
| v4 → v5 | aucune (finitions optionnelles, défauts du lecteur) |
| v5 → v6 | aucune (absence de `thread` / `textObjects` = aucun) |
| v6 → v7 | aucune (absence de `strategy` = squelette) |
| v7 → v8 | aucune (absence de `kerningPairs` / `ligatures` = pas de crénage manuel, pas de ligatures) |
| v8 → v9 | aucune (absence de `calques` / `calque` = un seul calque implicite) |

Les étapes « aucune » existent quand même : la table reste sans trou et une future version
n'a qu'à ajouter une ligne. Un fichier d'une version **future** (> `kMaxReadableSchemaVersion`)
est refusé en `UnsupportedFormat` avec un message nommant les deux versions. Tests :
`tests/unit/project_io/test_schema_migrations.cpp` construit un `project.json` par version
(v1 à v9) et le relit. `tests/fixtures/osp/` n'existe pas : les anciens fichiers sont forgés
en test, plus lisibles qu'un binaire figé.

## Sauvegarde atomique

`save_project` écrit d'abord un fichier temporaire `.osp.tmp` puis le **renomme** :
un plantage pendant l'écriture ne corrompt jamais le fichier existant.

## Aller-retour

Le test d'intégration vérifie que `save` puis `load` restitue exactement l'image
(PNG sans perte), les opérations, les labels de segmentation, les tangentes de
nœuds et les paramètres des trois types de points.

Limitation : la **sauvegarde automatique** et la **récupération après crash** sont
**prévues**, non implémentées.
Le cache des points générés n'est pas stocké (il est recalculé au chargement).

## Implémentation associée

- `libs/project_io/include/openstitch/project_io/project_io.hpp` —
  `save_project`, `load_project`, `kSchemaVersion`.
- `libs/project_io/src/json_serialize.cpp` — sérialisation du document.
- `libs/project_io/src/archive.cpp` — lecture/écriture ZIP (minizip-ng encapsulé).
- `libs/project_io/src/project_io.cpp` — orchestration, écriture atomique.
- `libs/project_io/include/openstitch/project_io/migrations.hpp`, `src/migrations.cpp` —
  table de migrations versionnées.
- Tests : `tests/unit/project_io/test_roundtrip.cpp`,
  `tests/unit/project_io/test_overrides_persistence.cpp` (retouches v3,
  migration v1/v2, topologie satin optionnelle/rétrocompatible, validation
  stricte).
