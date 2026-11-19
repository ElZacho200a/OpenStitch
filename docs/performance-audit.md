# Audit de performance

La [campagne R01-01](validation/r01/README.md) mesure le pipeline CPU de
bout en bout sur quatre motifs, avec échantillons bruts et budget pour
l'expérimentation de noyaux GPU. Ce relevé utilise un banc dédié et un
parcours différent des comparaisons historiques ci-dessous.

Date : 2026-09-22. Périmètre : pipeline image → DST (image, segmentation,
vectorisation, auto-numérisation, auto-satin, génération, finitions,
retouches manuelles), réactivité de l'interface desktop, historique
undo/redo, format `.osp`, worker Python SAM, ordre de couture, build/CI.

Règle suivie partout : **aucune accélération ne réduit la résolution, la
qualité, la précision ni la densité**. Chaque correction est soit une
réécriture strictement équivalente (vérifiée contre l'implémentation
d'origine recopiée dans un test), soit un cache à clé de contenu exacte.
Les sorties avant/après sont identiques octet pour octet sur tout le
corpus mesuré (empreintes ci-dessous) ; aucune référence golden n'a été
modifiée.

## 1. Protocole

- Machine : AMD Ryzen 7 7700X (8 cœurs/16 threads), 15 Go de RAM,
  Windows 11 Pro, MSVC 2022, build **Release** (`msvc-release`), Qt 6.8.3.
- Corpus (fixtures existantes, sans réduction) :
  - `tests/fixtures/tentabrode.png` (1117 × 1408, spirale, anneaux,
    trous, branches fines, concavités) ;
  - `sample/0215a7e5-….png` (1445 × 1088, image photographique non suivie
    par git, présente localement) ;
  - projets numérisés dérivés (`--save-osp`), avec 3 objets portant une
    retouche manuelle (point déplacé) ; 518 et 467 objets brodés,
    99 812 et 206 391 commandes.
- Outils ajoutés (hors CTest : le chronomètre ne doit jamais être un
  critère de réussite) :
  - `openstitch-bench <image> [--reps N] [--save-osp f] [--from-osp f]
    [--order]` (`tests/bench/openstitch_bench.cpp`, sans Qt) : chaque étape
    du pipeline, 1re exécution (« froid ») séparée des suivantes
    (min/médiane), pic de mémoire du processus, empreintes de la
    segmentation et de la séquence effective ;
  - `openstitch-bench-ui <projet.osp> [--reps N] [--quantize]`
    (`tests/bench/bench_main_window.cpp`, Qt headless via le seam de test
    `friend class MainWindowTest`, aucune API ajoutée) : `refreshImage`,
    `displayImage`, `renderStitches`, peinture de la scène, glisser +
    annuler, simulation complète ;
  - `sam-worker/tests/bench_dedup.py` : déduplication des masques.
- **A/B sur la même base fonctionnelle** : un worktree git temporaire au
  même HEAD (`b4cc178`), dont on a retiré toutes les optimisations de cet
  audit, exécuté en alternance avec l'arbre optimisé, sans autre build
  concurrente. Le planificateur satin étant désormais déterministe
  (`a2319ec`, budget en nombre d'évaluations), les deux versions doivent
  produire exactement la même sortie.

```powershell
cmake --build --preset msvc-release --target openstitch-bench openstitch-bench-ui
build\msvc\tests\bench\Release\openstitch-bench.exe tests\fixtures\tentabrode.png --reps 3 --save-osp t.osp
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
build\msvc\tests\bench\Release\openstitch-bench-ui.exe t.osp --reps 5 [--quantize]
```

## 2. Résultats avant → après (Release, médianes à chaud)

### Pipeline (tentabrode / sample)

| Étape | tentabrode avant | après | sample avant | après |
|---|---:|---:|---:|---:|
| `segment` 8 coul., lissage 0 | 27 505 ms | 59 ms | 2 388 ms | 58 ms |
| `segment` 8 coul., lissage 3 (défaut) | 199 ms | 111 ms | 154 ms | 111 ms |
| `segment` 16 coul., lissage 3 | 1 008 ms | 189 ms | 297 ms | 227 ms |
| `vectorize_region` (toutes régions) | 498 ms | 128 ms | 278 ms | 87 ms |
| `auto_digitize` (1 exécution) | 232 568 ms | 16 529 ms | 119 206 ms | 7 356 ms |
| `generate_sequence` | 1 272 ms | 96 ms | 2 776 ms | 152 ms |
| `effective_sequence` (+ finitions) | 1 492 ms | 113 ms | 2 976 ms | 179 ms |
| `refresh_context` (projet retouché) | 1 493 ms | 114 ms | 3 045 ms | 179 ms |
| pic mémoire du processus | 159 Mo | 234 Mo | 145 Mo | 206 Mo |

Sorties **identiques** avant/après : 190 et 121 régions (lissage 3),
empreintes de segmentation `8de5fd6bee1aaae9` / `b73fb238150dc24a`,
518 et 467 objets brodés, empreintes de séquence effective
`f0d364959c09791a` / `d943c2ad4acebcd9`, mêmes nombres de points, sauts et
coupes.

Étapes mesurées et non modifiées (pas de goulot) : `load_image` ~19 ms,
`apply_pipeline` 0,6 ms (sans op) / 26 ms (quantification) / 67 ms
(médiane + contraste), `region_adjacency` 3 ms, `encode_dst` 5 ms,
`decode_dst` 3 ms, `save_project` 330 ms, `load_project` 90 ms, copie
profonde d'un `Project` 2 ms.

### Interface (tentabrode numérisée, 99 812 commandes)

| Mesure | avant | après |
|---|---:|---:|
| chargement du projet (`applyLoadedProject`) | 1 577 ms | 174 ms |
| `refreshImage` après une mutation | 1 493 ms | 128 ms |
| idem avec une quantification dans la pile | 1 513 ms | 129 ms |
| glisser une forme + rafraîchir | 1 497 ms | 136 ms |
| annuler + rafraîchir | 1 516 ms | 139 ms |
| `displayImage` (sélection, visibilité) | 6 ms | 6 ms |
| `renderStitches` | 4 ms | 4 ms |
| peinture de la scène 1600 × 1000 | 28 ms | 28 ms |
| simulation complète (~400 pas) | 0,79 s | 0,78 s |

### Autres

- Déduplication SAM (251 masques 1024 × 1024, profil « balanced »
  simulé, WSL) : 12 620 ms → 93 ms, mêmes 178 masques conservés.
- Suite CTest Release complète (`-j 8`) : 147 s → 20 s ; le test
  d'intégration tentabrode 73,8 s → 18,6 s (Debug : 123 s).

## 3. Corrections

### 3.1 Auto-satin : amincissement de Zhang-Suen et cache du squelette

*Déjà committé dans `fa9f14d`, voir ce commit.* Une instrumentation
temporaire mesurait 94,6 s dans `thin_zhang_suen` sur 100,7 s d'analyse
(6 613 appels) : chaque demi-passe rebalayait toute la grille. La version
optimisée n'évalue que le bord (un pixel aux 8 voisins allumés n'est
jamais effaçable), avec une grille bordée et une table des 256
voisinages ; l'effacement reste différé, donc le squelette est identique
bit à bit (`test_skeleton_equivalence.cpp`, corpus procédural à deux
résolutions + 40 masques aléatoires).

57 % des appels à `analyze_region` étaient des doublons exacts (même
géométrie analysée par le solveur local, puis la décomposition, puis les
morceaux replanifiés). `auto_satin::SkeletonCacheScope` mémorise
rasterisation, distance, squelette et graphe brut sur le thread courant,
pendant `build_satin_sections`/`create_satin_plan` uniquement : clé =
positions exactes de tous les nœuds + paramètres de rasterisation,
comparées intégralement ; élagage et rapport recalculés avec leurs propres
paramètres ; libéré à la sortie du scope ; borné à 64 Mio (256 Mio
testés : 412 Mo de pic pour un gain nul). Coût mémoire mesuré : +60 à
+75 Mo de pic pendant l'auto-numérisation.

Il reste ~15 s dans deux régions de tentabrode (jonctions à haut degré,
génération de candidats de coupe de `satin_planning::split_region`) :
limite architecturale déjà documentée dans `satin_plan.hpp`, non
traitée ici.

### 3.2 Segmentation : nettoyage des petites régions

`segment()` absorbait chaque région de moins de `min_region_px` pixels en
balayant l'image ENTIÈRE deux fois (voisine majoritaire, puis
réétiquetage) : O(petites régions × pixels), 27 s sur tentabrode sans
lissage (des milliers de régions de bruit). Chaque petite région garde
désormais la liste de ses pixels ; une région absorbée transmet la sienne
à sa cible si celle-ci est une petite région pas encore traitée. Même
ordre de traitement (même `std::sort`), mêmes comptes de frontière, même
départage (plus petit label). Test : ancienne boucle recopiée, comparée
sur 18 configurations bruitées (labels, régions, comptes identiques).

### 3.3 Vectorisation : masque borné à la région

`vectorize_region` allouait et remplissait un masque de la taille de
l'image pour chaque région, puis y cherchait les contours : O(régions ×
pixels) à l'auto-numérisation. Le masque est limité à la boîte
englobante + 1 pixel de marge, les contours recalés par l'`offset` de
`cv::findContours`. Test : ancienne fonction recopiée, comparée sur toutes
les régions de segmentations bruitées à deux résolutions, y compris une
région collée aux quatre bords de l'image.

### 3.4 Tatami et finitions : tests de liaison

Deux causes, attribuées séparément (même projet figé, même empreinte
`b8491b9a56dc59d5` à chaque étape) :

1. `fill_tatami` calculait `connector_invalid` (O(arêtes)) pour CHAQUE
   pénétration, alors que le résultat n'est lu que pour le premier point
   d'un segment : `generate_sequence` 1 231 → 606 ms.
2. `connector_invalid` et `in_region` parcouraient toutes les arêtes à
   chaque appel (liaisons de rangées, autoroute d'underpath, filtre des
   points courts). Index des arêtes par bandes horizontales, construit une
   fois par remplissage : 606 → 106 ms. Contrat d'exactitude : une
   requête renvoie un SUR-ensemble des arêtes capables de satisfaire le
   prédicat (marge de 1 µm en y), évaluées avec la même arithmétique ;
   résultats indépendants de l'ordre de visite.
3. `finish_sequence` (filtre des points courts, Lot F) appelait
   `segment_stays_in_region` par point court candidat, reconstruisant les
   polygones de l'objet à chaque fois. Nouveau
   `stitch_generation::RegionSegmentTester` (même prédicat, index construit
   une fois par objet et par appel) : `effective_sequence` 542 → 117 ms.

Test : prédicats naïfs d'origine recopiés, 9 000 segments aléatoires sur
une grille de 500 µm (contacts de sommets, suivis de bord, segments nuls)
autour de formes à trous, pour `segment_stays_in_region` et
`RegionSegmentTester`.

### 3.5 Desktop : image de travail recalculée à chaque mutation

`MainWindow::refreshImage` rejouait `image::apply_pipeline` après toute
mutation du document (glisser de nœud, paramètre, undo…), soit 26 à 67 ms
avec une quantification ou un débruitage, sans que l'image puisse avoir
changé. `processed_` n'est plus recalculée que si l'image source ou la
pile d'opérations DIFFÈRE (comparaison de contenu exacte,
`image::ImageOp` a désormais un `operator==`) : undo/redo, changement de
projet et nouvel import sont couverts sans liste de sites
d'invalidation. Test QTest headless : opération, mutation sans rapport,
annuler, rétablir, projet différent avec la même pile.

`refresh_context` (déjà mutualisé : un seul `generate_sequence` pour la
séquence effective, les états et la vue d'édition) est conservé tel
quel ; il bénéficie directement de 3.4.

### 3.6 Historique : copie de la segmentation

`SetSegmentationCommand::apply` COPIAIT la segmentation dans le projet en
gardant l'originale dans la commande : deux exemplaires de la carte de
labels (4 octets/pixel, 6,3 Mo sur tentabrode) par segmentation de
l'historique, plus la copie à chaque exécution. Échange par déplacement
dans les deux sens. Test : trois cycles annuler/rétablir restituent
exactement A puis B.

### 3.7 Worker SAM : déduplication des masques

`mask_deduplicator.deduplicate` recalculait une IoU pleine image
(conversions `astype(bool)` comprises) entre chaque candidat et chaque
masque retenu. Aire et boîte englobante calculées une fois par masque ;
deux préfiltres exacts (boîtes disjointes ⇒ IoU = 0 ; IoU ≤ aire min /
aire max) ; intersection calculée sur le recouvrement des boîtes. Mêmes
entiers intersection/union, donc même IoU flottante et mêmes décisions
(ordre de priorité inchangé). Test pytest : implémentation d'origine
recopiée, 30 tirages × 4 seuils, masques `bool`/`uint8`, masques vides.

## 4. Hypothèses écartées (mesurées)

- **Rendu du canevas / sélection / zoom** : `displayImage` 6 ms,
  `renderStitches` 4 ms, peinture de 100 000 points 28 ms. Pas de goulot.
- **Simulation** : ~2 ms par pas (400 pas), acceptable.
- **Ordre de couture quadratique** (`libs/optimization`) : 1,2 ms à
  1 000 objets, 27 ms à 5 000 (plus proche voisin), < 1 ms pour
  `LayeredColorThenProximity` utilisé par l'auto-numérisation. Non
  modifié.
- **Copies de `Project`** : 2 ms pour une copie profonde complète ; les
  commandes stockent des deltas (hors 3.6).
- **Allocation par tentative dans la trace d'arête du squelette** : déjà
  mesurée sans effet dans `satin.md`.
- **Chargement/sauvegarde `.osp`, DST** : voir « restant ».

## 5. Problèmes restants, par impact

1. ~~**Auto-satin sur jonctions à haut degré**~~ : le chemin `split_region`
   (`satin_planning`) a été retiré en août 2026 ; le coût restant de l'essai de
   satin venait du rognage aux cellules, traité le 2026-11-15 (§13).
2. **Régénération complète à chaque mutation** : ~110 ms (tentabrode) à
   ~180 ms (sample) restent pour une modification locale. Un cache par
   objet est possible (tatami/directionnel dépendent de la seule forme et
   des paramètres) mais le routage satin, les changements de couleur et
   les finitions dépendent des voisins : à concevoir avec une invalidation
   explicite, non fait. Déplacement en tâche de fond non justifié à ce
   niveau de latence.
3. **Écart de qualité de l'ordre de couture** : l'optimiseur minimise des
   distances entre centres de boîtes englobantes (6 877 mm sur
   tentabrode) alors que le déplacement réel sortie → entrée vaut
   8 836 mm (+28 %) ; 7 267 contre 5 528 mm (+31 %) sur sample. Une
   stratégie fondée sur les points réels d'entrée/sortie changerait la
   sémantique des stratégies existantes : non modifiée, à décider.
4. **Sauvegarde `.osp` (330–420 ms)** : PNG de l'image + JSON indenté +
   DEFLATE appliqué aussi au PNG déjà compressé. Ponctuelle, non
   profilée en détail.
5. **Mémoire de l'auto-numérisation** : +60 à +75 Mo de pic (cache du
   squelette, 64 Mio max).
6. **Build/CI** : aucune option (LTO, parallélisme) proposée faute de
   mesure ; la suite de tests est désormais ~7× plus rapide en Release.

## 6. Limites de validation

- SAM : seule la déduplication a été mesurée et testée (WSL, numpy) ;
  l'inférence réelle (GPU, chargement du modèle) n'a pas été exécutée. Un
  test préexistant de `test_sam_service.py` échoue dans ce venv car il
  suppose `sam2` absent alors qu'il y est installé (sans rapport).
- Interface : mesurée headless (`QT_QPA_PLATFORM=offscreen`), sans
  affichage réel ni GPU ; les temps de peinture d'un vrai écran peuvent
  différer.
- Une seule machine ; les chiffres « avant » de la première exploration
  (avant `a2319ec`) dépendaient encore d'un filet de temps mur de 10 s et
  ne sont pas comparables ; seules les mesures A/B de la section 2 le
  sont.
- Tests : CTest complet Release et Debug 755/755, `test_main_window`
  69 fonctions, pytest worker 38/39 (échec préexistant ci-dessus).

## 7. Audit du 2026-10-13 : parallélisme et sauvegarde

Même machine, même protocole (section 1), mêmes fixtures. Règle inchangée :
**sorties identiques octet pour octet**. Empreintes avant = après sur les deux
images : segmentation `8de5fd6bee1aaae9` / `b73fb238150dc24a`, séquence
effective `db5603d38ab9e82b` / `acb664aa210bd648` (et `a173e13502327f4e` pour
tentabrode avec `--satin-bands`). Les empreintes de séquence diffèrent de la
section 2 parce que le moteur a évolué depuis, pas à cause de cet audit.

Constat de départ : après l'audit de septembre, seule `generate_sequence`
utilisait `parallel_for` ; segmentation, vectorisation, auto-numérisation et
écriture du `.osp` tournaient sur un seul cœur (sur 16 fils disponibles).
OpenCV est compilé sans backend parallèle (`Enabled backends(0)`), donc ses
filtres sont séquentiels eux aussi.

### Résultats (Release, médianes à chaud)

| Étape | tentabrode avant | après | sample avant | après |
|---|---:|---:|---:|---:|
| `segment` 8 coul., lissage 0 | 64 ms | 39 ms | 70 ms | 34 ms |
| `segment` 8 coul., lissage 3 (défaut) | 111 ms | 51 ms | 131 ms | 47 ms |
| `segment` 16 coul., lissage 3 | 197 ms | 88 ms | 276 ms | 82 ms |
| vectorisation de toutes les régions | 197 ms | 20 ms | 125 ms | 12 ms |
| `auto_digitize` (défauts) | 210 ms | 77 ms | 73 ms | 67 ms |
| `auto_digitize` avec « Satin automatique » | 2 357 ms | 922 ms | — | — |
| `save_project` | 154 ms | 28 ms | 197 ms | 26 ms |
| taille du `.osp` | 888 Ko | 982 Ko | 1 449 Ko | 1 579 Ko |
| pic mémoire du processus | 145 Mo | 184 Mo | 145 Mo | 227 Mo |

Non modifiés (mesurés, pas de goulot ou pas de gain sûr) : `generate_sequence`
35 ms (déjà parallèle ; 73 ms avec `OPENSTITCH_THREADS=1`), `load_project`
40 ms, `apply_pipeline` 26 à 69 ms (filtres OpenCV), `refreshImage` 57 ms,
peinture de la scène 12 ms, simulation ~1 ms par pas.

### Corrections

1. **Auto-numérisation** (`libs/autodigitize/src/autodigitize.cpp`) : la
   vectorisation et l'essai de satin de chaque région (les deux étapes
   coûteuses) se font en parallèle ; les identifiants, les objets et l'ordre
   sont attribués ensuite en séquence, dans l'ordre des régions.
   `classify_and_build_embroidery` reçoit le choix de satin précalculé
   (`satin_choice_for`). Même traitement pour `auto_digitize_vectors`.
2. **Vectorisation par lot** : `vectorization::vectorize_regions(seg, ids,
   options)`, parallèle, résultat `out[i]` identique à
   `vectorize_region(seg, ids[i], options)`. Utilisée par « Vectoriser » (F7)
   du desktop, qui bouclait région par région sur le fil de l'interface.
3. **Segmentation** (`libs/segmentation/src/segmentation.cpp`) : affectation
   des pixels au centre le plus proche (une ligne = une tâche), lissage (une
   classe de couleur = une tâche, puis vote par ligne), composantes connexes
   (par lots de classes en parallèle, fusion séquentielle dans l'ordre des
   classes, donc mêmes identifiants de région). Le lot est borné à ~256 Mio.
   Les boucles par pixel passent par des pointeurs de ligne au lieu de
   `at<>()` avec division et modulo.
4. **Écriture du `.osp`** (`libs/project_io/src/`) :
   - les trois entrées (JSON, PNG, étiquettes) sont préparées en parallèle ;
   - le PNG, déjà compressé, est **stocké** au lieu d'être recompressé ;
   - DEFLATE au niveau rapide pour le JSON et les étiquettes (c'était le
     poste dominant : ~100 ms pour 6 Mo d'étiquettes) ;
   - le PNG de la dernière image enregistrée est mis en cache, clé exacte
     (dimensions + octets comparés), une entrée, 64 Mio max : la sauvegarde
     automatique (toutes les 2 min, sur le fil de l'interface) ne réencode
     plus une image inchangée.
   Le format reste un ZIP/DEFLATE standard, relu par les versions
   précédentes. Contrepartie mesurée : fichier ~10 % plus gros.

### Contreparties et limites

- **Mémoire** : +40 à +80 Mo de pic (cartes de composantes connexes de
  plusieurs classes en même temps, plafonnées).
- **Satin automatique** : 2,6× seulement sur 16 fils. Une ou deux régions à
  jonctions de haut degré dominent (problème 1 de la section 5, inchangé) ;
  le parallélisme entre régions ne peut pas les accélérer.
- `parallel_for` crée ses fils à chaque appel (~1 ms) : pas de pool
  persistant. Non justifié aux granularités actuelles (région, classe, ligne),
  à revoir si une étape appelée à chaque mutation descend sous ~10 ms.
- Interface mesurée headless, comme en section 6.

### Tests ajoutés

- « meme resultat avec un seul fil et avec plusieurs » pour `segment` et
  `auto_digitize` (avec et sans `satin_bands`), via `OPENSTITCH_THREADS`.
- `vectorize_regions` = boucle sur `vectorize_region`, erreur d'une région
  inexistante conservée à sa place.
- Cache PNG de `save_project` : invalidation quand un pixel ou les dimensions
  changent.
- Suite complète Release : 1078/1078.

Méthode à suivre pour tout nouveau code : skill `openstitch-performance`.

## 8. Audit du 2026-10-15 : la mémoire des squelettes auto-satin

**Constat** (projet retouché à la main, 344 objets dont 275 en auto-satin, 120 738 points ;
Release, 4 cœurs) : `generate_sequence` coûtait **1,9 s à chaud comme à froid**, donc chaque
modification (glisser un nœud, changer un réglage) rejouait presque 2 s de calcul. Isolé par
visibilité : les 69 tatami seuls = 55 ms ; les auto-satin seuls = 1,9 s.

**Cause racine.** `generate_skeleton_satin` possède une mémoire de résultats (clé exacte, LRU),
mais bornée à **128 entrées**. Un projet de plus de 128 auto-satin parcourt ses objets dans
l'ordre : chaque calcul évince l'entrée qu'on relira 128 pas plus tard (balayage séquentiel d'un
LRU trop petit : zéro succès). Le préchauffage parallèle de `generate_sequence` (phase 2) remplissait
la mémoire pour rien : les entrées étaient évincées avant la relecture de la phase 3, ce qui explique
aussi que 4 cœurs ne donnaient que 3,2 s → 1,9 s.

**Correction.** Mémoire bornée en **octets** (192 Mio, une entrée de plus de la moitié du budget
n'est pas mémorisée) et retrouvée par empreinte 64 bits (FNV-1a) au lieu d'un balayage de clés ;
l'égalité reste confirmée sur la clé entière, donc le résultat servi est identique à un recalcul.
`skeleton_result_cache_stats()` expose entrées, octets, succès et échecs (tests et banc seulement).

| Mesure (Release, même projet) | avant | après |
|---|---:|---:|
| `generate_sequence`, à chaud | 1 910 ms | 60 ms |
| `generate_sequence`, à froid | 1 910 ms | 428 ms |
| `effective_sequence` | 1 840 ms | 83 ms |
| équivalent de `refreshImage` | 1 900 ms | 120 ms |

Empreinte de la séquence effective **identique** avant et après (`f1d2347c3dbdfff0`).
Test : `memoire des squelettes : plus de 128 regions distinctes, 2e passe sans recalcul`
(échoue avec un budget minuscule, réussit avec le budget réel).

**Contrepartie.** Jusqu'à 192 Mio de mémoire conservée par processus (estimé à quelques Mio pour ce
projet : environ 30 000 traversées et les clés de contour). Reste ouvert : le premier calcul à froid
(428 ms ici) ; un projet de plusieurs milliers d'auto-satin dépasserait le budget et retrouverait
l'éviction.

**Ce que cela change pour le clipping demandé.** La régénération complète n'est plus le goulot
sur ce projet (120 ms par modification). Un clipping de fenêtre ne s'applique de toute façon pas à
la séquence : l'ordre, les changements de couleur, les coupes et les liaisons dépendent de tous les
objets, et l'export doit rester identique. Il resterait utile pour le rendu (peindre les objets
visibles) et pour calculer le froid en arrière-plan ; à décider après mesure sur un plus gros projet.

## 9. Audit du 2026-10-18 : régénération incrémentale par objet (HP-PERF-002)

**Constat.** `effective_sequence` régénérait tous les objets à chaque modification du document :
63 ms à chaud sur `tentabrode.png` (158 objets, 16 fils), 116 ms avec `OPENSTITCH_THREADS=1`,
alors qu'un seul objet avait changé.

**Correctif.** `generate_sequence` mémorise la séquence de chaque objet **indépendant** (droit,
tatami, directionnel) dans un cache (`libs/stitch_generation/src/chunk_cache.*`). Ces générateurs
ne lisent que l'identifiant de l'objet, ses paramètres et la géométrie du vecteur source : leur
résultat est une fonction pure de ces trois éléments, le lien avec l'objet précédent (entrée
automatique, orientation du bloc, changement de couleur) se faisant à l'assemblage séquentiel de
la phase 3, qui n'est pas modifié.

- **Clé exacte.** L'entrée garde une copie des paramètres et de la géométrie et les compare
  intégralement avec les `operator==` par défaut du document : aucune empreinte seule, donc aucune
  collision possible, et un champ ajouté plus tard à un type de paramètres est comparé
  automatiquement. Un identifiant réutilisé avec un autre contenu (autre projet, annulation)
  provoque un recalcul, jamais un résultat périmé.
- **Mémoire.** Une case par identifiant, budget de 64 Mio estimés, éviction du moins récemment
  utilisé. Ici : 158 entrées, 2,2 Mo.
- **Déterminisme.** Les tâches parallèles lisent le cache sous mutex et ne retiennent qu'une
  copie ; l'assemblage reste séquentiel. Test : même séquence avec 1 et 8 fils, avec et sans cache.

**Résultats** (Release, `tentabrode.png`, médianes à chaud, empreinte de la séquence effective
identique : `cc5819a2746b6f37`) :

| Scénario | Avant | Après |
|---|---|---|
| `effective_sequence`, rien ne change | 63 ms | **7 ms** |
| un objet tatami médian modifié | 63 ms | **8 ms** |
| le plus gros objet (fond) modifié | 63 ms | 67 ms |

**Limites.** Un objet unique très gros reste régénéré en entier (67 ms ici) : le cache est par
objet, pas par région. Les satins (colonnes routées, entrée dépendant de l'objet précédent) et
l'auto-satin ne passent pas par ce cache ; leur squelette garde son propre cache (§8). Le banc
désactive le cache pour ses étapes historiques afin de rester comparable aux relevés précédents et
ajoute trois étapes « cache chaud », « objet médian modifié », « plus gros objet modifié ».

## 10. Audit du 2026-11-13 : marqueurs des vecteurs denses dans Studio

**Constat.** La sélection d'un objet vectoriel dense créait un `QVariantMap` et un
`ToolOverlayItem` par nœud, puis demandait au canevas de dessiner chaque marqueur. Sur le cas de
régression à 10 000 nœuds, toute la liste traversait ainsi la frontière C++/Qt et le rendu, même
quand plusieurs points occupaient le même voisinage à l'écran.

**Correctif.** `ToolController` regroupe les nœuds ordinaires et sélectionnés dans deux lots. Le
canevas applique ensuite un niveau de détail déterministe en espace écran aux nœuds ordinaires :
au plus un marqueur par cellule, avec environ 3 000 cellules visibles pour une fenêtre usuelle.
Les points hors écran restent éliminés et le point sélectionné contourne toujours cette limite.
Le contour de sélection élimine également ses sommets sous-pixel, tout en conservant ses
extrémités : sur la scène circulaire de régression, 10 001 sommets source deviennent environ
436 à 456 sommets d'affichage selon le cadrage.

La géométrie source complète reste dans le modèle et dans les lots du contrôleur : sélection,
édition et hit-testing continuent donc à voir les 10 000 nœuds. Le test clique notamment un nœud
non dessiné par le niveau de détail et vérifie qu'il devient sélectionné et visible.

**Preuve structurelle.** `test_desktop_v2_studio_tools` passe avec 10 000 nœuds source, au plus
3 100 marqueurs ordinaires affichés et moins de la moitié des marqueurs initiaux. Ce relevé ne
prouve la borne de travail côté rendu et la conservation des interactions.

**Mesure locale Release.** Le micro-banc opt-in `perfDenseNodeSelectionOverlay`
(`OPENSTITCH_PERF=1`) mesure le chemin sélection de l'objet dense → reconstruction des lots →
conversion de la surcouche visible en types Qt, après 10 passages de chauffe et sur 100 passages.
Sur Windows 11, Qt 6.8.3, backend Quick logiciel/offscreen, quatre exécutions après le LOD du
contour donnent un p50 entre 3,68 et 3,81 ms, un p95 entre 4,05 et 4,63 ms et un p99 entre 8,57 et
9,91 ms. La scène circulaire de 10 000 nœuds produit 74 marqueurs visibles avec ce cadrage. Cette
mesure ne couvre ni la peinture QPainter, ni la présentation GPU/écran : elle ne constitue donc
pas un relevé FPS global.

**Banc natif.** `perfDenseNodeSelectionNative` ouvre le shell complet, vérifie un backend RHI
matériel et compare une cadence témoin sans sélection aux sélections légère et dense. Dans la
session du 14 novembre 2026, `QSG_INFO=1` confirme D3D11 sur une NVIDIA GeForce RTX 5060 Ti, mais
la cadence témoin elle-même est bridée à un p95 de 262,46 ms ; les sélections légère (262,16 ms)
et dense (263,64 ms) sont indiscernables de ce témoin. Ces valeurs ne mesurent donc pas le coût de
l'overlay et ne valident ni n'invalident un portage QSG. Le banc doit être rejoué dans une session
graphique non bridée, fenêtre réellement présentée et dégagée.

**Limite et suite.** Les marqueurs restent dessinés par le canevas QPainter actuel. Un passage à
des instances GPU/QSG ne sera justifié qu'après profilage du nouveau chemin : le principal coût
confirmé ici était la création et le transfert de milliers d'objets Qt, pas le calcul de leurs
coordonnées.

## 11. Audit du 2026-11-14 : liaisons cachées du tatami (`HiddenRouter`)

**Constat.** `generate_sequence` était passé de 63 ms (§9) à ~160 ms sur `tentabrode.png`, et le
plus gros objet modifié de 67 à 161 ms. Le cache chaud restant à 6,5 ms, tout le temps venait du
seul fond tatami. Profil temporaire : `HiddenRouter::route` (#343, 96 points de passage au lieu de
24) fait ~190 appels par génération et **5,6 M de tests de visibilité** sur le banc (~0,6 µs
chacun), 70 % dans les liaisons réussies (A* : chaque nœud développé teste ses ~96 voisins).

**Corrections** (`libs/stitch_generation/src/tatami.cpp`), toutes à sortie identique :

- `connector_invalid` : rejet en x des arêtes (les bandes de l'index ne trient qu'en y ; marge
  d'1 µm, très au-delà des tolérances paramétriques de 1e-9) et tampon `ts` réutilisé.
- `in_region` : parités réutilisées et remises à zéro case par case, au lieu d'allouer un vecteur
  par polygone à chaque milieu de sous-segment testé.
- A* : un nœud dont la borne inférieure `dist + |v b|` dépasse le plafond (marge d'1 µm) n'est pas
  testé ; visibilités depuis `a` et vers `b` paresseuses et mémoïsées ; candidats sélectionnés par
  `nth_element` puis triés (ordre total, mêmes candidats).
- Visibilité entre deux points de passage mémoïsée sur la vie du routeur (clé ordonnée, le test
  n'étant pas exactement symétrique en virgule flottante ; 4 M d'entrées au plus).

**Résultats** (Release, médianes à chaud, même machine, `main` contre la branche ; empreintes de
segmentation et de séquence effective identiques sur les trois images, et avec `OPENSTITCH_THREADS=1`) :

| Image | `generate_sequence` avant | après | empreinte |
|---|---|---|---|
| `tentabrode.png` | 161 ms | **79 ms** | `0e0ebfe85091dd02` |
| `sample/0215a7e5…png` | 168 ms | **67 ms** | `b55ebf70db688425` |
| `marine-synth-01/source.png` | 3,4 ms | **2,5 ms** | `d1f73e507e36b047` |

**Hypothèse restante.** Les tests depuis les extrémités, propres à chaque liaison, dominent
désormais. Un index 2D (grille parcourue le long du segment) à la place des bandes en y réduirait
le nombre d'arêtes visitées par test sans changer le résultat (déduplication par marques, `ts`
trié) ; non fait, à mesurer.

## 12. Audit du 2026-11-14 : ce qui passe sur le GPU dans Studio

**État des lieux.** Les points, le cadre, la grille, les repères, les sauts et le rendu réaliste
sont déjà des géométries du scene graph (RHI D3D11). Banc natif `openstitch-studio-bench`
(scène de 10 000 points) : scène construite en 2,7 ms, géométrie créée une seule fois, rendu
GPU 1–2 ms par image ; `QSG_RENDER_TIMING=1` donne `time in renderer: total=0–2 ms`.

**Le « bridage » à ~260 ms par image n'est pas la session.** Il apparaît en `sync=244–254 ms`
(`render=2`, `swap=0`) : dans la boucle threadée, la phase « sync » inclut `beginFrame`, qui
attend le swapchain. La machine était sans clavier ni souris depuis 66 min, écran en veille :
Windows ne présente plus alors que ~4 images/s. Preuve indépendante de Studio :
`DwmGetCompositionTimingInfo` compte 0 à 1 rafraîchissement en 3 s pour trois écrans déclarés à
60 Hz, et le même plafond apparaît sur le GPU intégré AMD (`QT_D3D_ADAPTER_INDEX=1`). Ni
`SetThreadExecutionState(ES_DISPLAY_REQUIRED)`, ni une entrée souris simulée, ni
`SC_MONITORPOWER` ne rallument ces écrans : une action physique est nécessaire. Les relevés
natifs du §10 (cadence témoin à 262 ms) ont la même cause. Mesurer la cadence avec un écran allumé et actif ; à défaut, lire les
temps `polish` (fil d'interface) et `render` (GPU), indépendants de la présentation.

**Surcouches `Canvas` (QML, JavaScript) : laissées telles quelles.** Sélection dense de 10 000
nœuds (`perfDenseNodeSelectionNative`, D3D11) : polish médian 2 ms, p95 5 ms grâce au niveau de
détail du §10. Un portage en géométrie ne gagnerait que quelques millisecondes, sous le budget
d'une image : non fait.

**Couche vecteurs : triangles GPU au lieu d'une image rastérisée.** `VectorLayer::build`
rastérisait toutes les formes au QPainter dans une image jusqu'à 4096 px de côté (12 px/mm au plus,
floue au fort zoom) à chaque changement de géométrie. Désormais il triangule les remplissages
pair-impair (`qTriangulate`, API privée de QtGui 6.8.3, isolée dans `layers/vector_layer.cpp`) et
les contours (traits pleins ou pointillés `Qt::DashLine`, en quadrilatères), en mm scène ; le
backend RHI les dessine avec la matrice de la caméra (lissage MSAA 4× du swapchain). Le backend
logiciel garde exactement l'image d'avant, rastérisée à la demande et une seule fois par calque
(`call_once`), dans le worker quand le canevas est logiciel.

| Projet (formes) | Avant : construction + rastérisation | Après : construction + triangles |
|---|---|---|
| synthétique 1 000 carrés | 51 ms | **5,2 ms** |
| `tentabrode` (158) | 91 ms | **5,9 ms** |
| `Impossibke.osp` (496) | 244 ms | **16,5 ms** |
| `RuieGothique.osp` (435) | 83 ms | **28 ms** |

(Release, médianes de 5 passes, `perfVectorLayer` avec `OPENSTITCH_PERF=1` et
`OPENSTITCH_PERF_OSP=<projet>`.) Le pick reste sur les chemins `QPainterPath`, inchangé.

**Preuves.** `layerTrianglesCoverFillAndSkipRaster` : aire des triangles de remplissage = 84 mm²
pour un carré de 10 mm troué de 4 mm, trou vide, couleur prémultipliée, aucune rastérisation tant
que seul le GPU lit le calque. Suite `test_desktop_v2_studio_space_vector` rejouée sur D3D11 natif
avec `OPENSTITCH_TEST_RHI=1 QT_ENABLE_HIGHDPI_SCALING=0` : 20/20, dont les tests de pixels (formes
visibles, masquage, republication). Sans `QT_ENABLE_HIGHDPI_SCALING=0`, trois tests de pixels
échouent aussi sur `main` à DPR 1,5 (coordonnées logiques contre image physique) : défaut des
tests, pas du rendu.

**Restent sur CPU, volontairement.** Image, segmentation et génération des points : un calcul GPU
en virgule flottante varie selon le pilote et casserait le déterminisme octet pour octet. La
couche régions (une passe sur les étiquettes à chaque changement de segmentation) et l'image
source sont déjà des textures envoyées une fois par révision.

## 13. Audit du 2026-11-15 : rognage aux cellules de l'auto-satin

**Constat.** Avec « Satin automatique », l'essai de satin de chaque région
(`choose_satin` → `generate_skeleton_satin` avec mesure de couverture) faisait
presque tout le temps de `auto_digitize` : 2,3 s sur 2,3 s pour tentabrode en monofil.
Une seule région (n° 95, 1 241 mm², jonctions) prenait 882 ms et bornait le temps en
parallèle (968 ms). Profil temporaire par phase : `sample_axis` 1 908 ms, dont la quasi-totalité
dans `clip_to_cell` ; l'analyse raster et le squelette ne pèsent que ~120 ms. Pour chaque
traversée, le rognage avance le long de la corde par pas de 40 µm (avec dichotomie), soit
557 000 tests `inside()` pour 8 681 traversées. Chaque test mesurait la distance exacte du point
à toute la polyligne de son morceau (axe échantillonné tous les 50 µm, jusqu'à 3 223 points) et
parcourait tous les autres morceaux (jusqu'à 69 sur sample).

**Corrections** (`libs/auto_satin/src/skeleton_satin.cpp`), toutes à résultat identique :

- Arbre de boîtes par polyligne de morceau (feuilles de 8 segments consécutifs) : la distance au
  morceau ne visite que les nœuds plus proches que le meilleur déjà trouvé, avec une marge d'1e-6 µm
  contre l'arrondi ; le minimum ne dépend pas de l'ordre de parcours, donc même valeur. Le segment le
  plus proche au point précédent sert de borne initiale (les points avancent de 40 µm).
- Pour chaque traversée, liste des morceaux voisins qui peuvent compter : la limite d − biais vaut au
  plus d(p) + |t| sur toute la corde (inégalité triangulaire) ; un morceau dont la boîte est plus loin
  que cette borne de la boîte de la corde serait écarté à chaque point par le test exact.
- Filtres de boîtes sans racine carrée, qui n'écartent que ce que le test exact écarte.

Deux essais intermédiaires mesurés puis remplacés : blocs plats de 8 segments triés par distance
(plus lent : 2 557 ms, le tri coûte plus qu'il ne rapporte) et blocs plats sans tri (1 747 ms :
62 M de boîtes parcourues, 112 par appel en moyenne).

**Résultats** (Release, `--satin-bands`, médianes, 16 fils ; `main` contre la branche, même machine) :

| Entrée | `auto_digitize` avant | après | empreinte de séquence |
|---|---|---|---|
| `tentabrode.png` | 963 ms | **231 ms** | `66a0e13ef20e3a3e` |
| `sample/0215a7e5…png` | 684 ms | **304 ms** | `5de2d277161421b0` |
| `marine-synth-01/source.png` | 99 ms | **44 ms** | `7ba378266ebee199` |

En monofil, tentabrode passe de 2 300 à 1 010 ms et la région 95 de 882 à 173 ms ; sample de
2 755 à 1 553 ms (région 91 : 435 → 223 ms). Empreintes également identiques pour
`Impossibke.osp` et `RuieGothique.osp` régénérés depuis le projet (`--from-osp`). Tests : suites
auto-satin et golden (175 cas), puis `ctest` Release complet.

**Reste.** Le rognage reste le premier poste (~0,7 s sur 1 s en monofil pour tentabrode) : le pas
de 40 µm et la dichotomie à 10 niveaux fixent le nombre de tests ; les réduire changerait les
traversées, donc la broderie : à traiter comme un changement de comportement, mesuré sur la qualité.

## 16. Audit du 2026-11-15 : lecture des projets (`load_project`)

Sur de vrais projets, la lecture coûtait 2 à 4 fois l'enregistrement (Impossibke 215 ms, RuieGothique
395 ms). Mesure par étape (RuieGothique, sondes temporaires) : zip 19 ms, première lecture du JSON
36 ms, **migration 137 ms**, deuxième lecture du JSON 49 ms, `project_from_json` 14 ms, PNG 22 ms,
labels 3 ms. `project.json` était lu quatre fois et réécrit une fois : `load_project_impl` le lisait,
`migrate_project_json` le relisait, `declared_schema_version` le relisait encore, puis `dump()`, puis
`load_project_impl` relisait le texte migré. C'était le cas même pour un fichier déjà à la dernière
version, où aucune étape ne s'applique.

**Changement.** `detail::migrate_project_json_in_place` applique les mêmes contrôles de version et les
mêmes étapes à l'arbre déjà lu ; `migrate_project_json` (API texte, inchangée) l'enveloppe ;
`load_project_impl` et `fragment_from_json` l'appellent directement. Le document obtenu est le même :
les lecteurs de `json_serialize.cpp` acceptent indifféremment entiers signés et non signés (seule
différence possible entre une valeur insérée par une étape et la même valeur relue), et les réels
sortent identiques d'un aller-retour `dump`/`parse`.

**Preuve d'équivalence.** Les 17 projets de `Documents\Broderie` sont chargés puis réenregistrés
(`openstitch-bench --from-osp p --save-osp q`, option ajoutée) avec l'ancien et le nouveau code :
SHA-256 identiques pour les 16 projets lisibles ; `TheyAskedForIT.osp` (ancien format de fondu
multicolore) est refusé par les deux versions.

**Résultats** (Release, `--reps 7`, médianes) :

| Projet | `load_project` avant | après | `save_project` |
|---|---:|---:|---:|
| Impossibke.osp (1,9 Mo) | 215 ms | **63 ms** | 55 ms |
| RuieGothique.osp (2,4 Mo) | 395 ms | **105 ms** | 98 ms |

**Reste.** La lecture vaut maintenant l'enregistrement. Le zip (19 ms) et le PNG (22 ms) sont
séquentiels et indépendants de l'analyse du JSON (36 ms) : les recouvrir gagnerait encore ~20 ms.
## 15. Audit du 2026-11-15 : opérations d'image (`apply_pipeline`)

`cv::medianBlur` ne tourne que sur un fil, et luminosité/contraste ainsi que l'affectation des
pixels de la quantification parcouraient l'image en une seule boucle séquentielle. Trois changements,
sortie identique octet pour octet :

- **Débruitage médian** : bandes de 64 lignes filtrées en parallèle (`parallel_for`), chacune avec
  `ksize / 2` lignes de voisinage lues dans l'image ; le bord répliqué ne s'applique qu'aux vrais bords
  de l'image, comme l'appel entier. Chaque tâche écrit ses propres lignes de la sortie. Test
  `debruitage median par bandes` : 151 lignes (trois jonctions, dernière bande incomplète), noyaux 3
  et 5, comparés à une médiane naïve.
- **Luminosité/contraste** : la fonction ne dépend que de la valeur du canal ; le même `convertTo` est
  appliqué une fois aux 256 valeurs, puis `cv::LUT` (alpha identité) remplace `split`/`merge`.
- **Quantification** : affectation au centre le plus proche ligne par ligne en parallèle (le k-means
  sur l'échantillon est inchangé).

**Résultats** (Release, `openstitch-bench tentabrode.png --reps 9`, 1117 × 1408, médianes, 16 fils) :

| Pipeline | avant | après | empreinte de l'image |
|---|---:|---:|---|
| médiane 3 + luminosité/contraste | 63 ms | **11 ms** | `6a5786d520733673` |
| médiane 3 seule | 55 ms | **9 ms** | — |
| médiane 5 seule | 56 ms | **10 ms** | `a7cfcaa5aede66d4` |
| luminosité/contraste seul | 9 ms | **2 ms** | `67e7266b9376b54a` (−40/−30) |
| quantification 8 couleurs | 25 ms | **9 ms** | `ce49d00a44412900` |

Empreintes relevées sur l'ancien et le nouveau code (même binaire de banc, `ops.cpp` permuté). Le banc
mesure désormais chaque opération séparément et imprime ces empreintes.

**Reste.** La quantification passe l'essentiel de ses 9 ms dans `cv::kmeans` (20 000 échantillons,
3 essais) ; le reste du pipeline (bilatéral, aplat, CLAHE, netteté) n'est pas sur le chemin par défaut.
## 14. Audit du 2026-11-15 : régénération, gros objet troué et état des routeurs

**« Mettre à jour » après retouche d'une région** (banc `[.perf]` de `test_region_edit.cpp`, #348) :
19 + 1 + 3 ms sur tentabrode, 33 + 1 + 23 ms sur `Impossibke.osp` (1 395 régions, 494 objets sur
496 réutilisés), 27 + 1 + 16 ms sur `RuieGothique.osp`. La mise à jour région par région notée dans
PERF-STUDIO-001 n'est plus justifiée.

**Gros objet troué.** Sur `Impossibke.osp`, un seul tatami (« Remplissage Région 26112 », 37 trous,
1 372 nœuds) fait ~370 ms des ~600 ms d'une génération complète en monofil, et ~320 ms quand on le
modifie dans Studio (étape « le plus gros objet modifié »). Presque tout est dans les liaisons
cachées de `HiddenRouter` : 2,78 M tests de visibilité entre points de passage et 0,53 M depuis les
extrémités pour 8 générations du banc.

Hypothèses écartées, mesurées sans instrumentation et en alternance A/B :

- Grille 2D des arêtes pour collecter les intersections : 6,5× moins d'arêtes visitées (437 M → 67 M)
  mais aucun gain mesurable (monofil 536 contre 530 ms) ; le coût est par appel, pas par arête.
- A* paresseux à résultat identique (validation de la visibilité au choix du nœud) : −34 % de tests
  seulement, aucun gain ; avec 37 trous, la plupart des candidats sont invisibles et doivent être
  validés un à un.

**Correction retenue : état du routeur gardé d'une génération à l'autre.** L'index des arêtes, les
points de passage (contour rentré par Clipper) et le mémo des visibilités ne dépendent que de la
géométrie (région, retrait). `RouterState` est emprunté à un cache à clé exacte (région entière et
retrait comparés) à la construction du routeur et rendu à sa destruction : jamais deux fils sur le
même état, budget de 64 Mio, éviction du plus ancien. Il suit `set_chunk_cache_enabled` et
`clear_chunk_cache` (les étapes historiques du banc restent comparables). Résultat inchangé :
valeurs pures de la géométrie.

| `Impossibke.osp`, Release, 16 fils | Avant | Après |
|---|---|---|
| le plus gros objet modifié (`row_spacing`) | ~320 ms | **~130–140 ms** |
| génération complète, cache désactivé | ~310 ms | ~310 ms (inchangé) |
| pic mémoire du banc | 88 Mo | 116 Mo |

Empreintes de séquence identiques à `main` sur les 5 entrées habituelles. Test :
« etat du routeur reutilise entre deux reglages » (tatami percé de 9 trous, trois réglages
successifs, comparés à un calcul à neuf).

**Reste.** La première génération du gros objet (ouverture du projet) garde son coût : réduire les
candidats (`kMaxCandidates`) ou le pas de recherche changerait les liaisons, donc la broderie.

## 17. Audit du 2026-11-17 : chantier R02, calcul GPU et optimisation

Dossier complet : [`performance/gpu-compute/`](performance/gpu-compute/README.md).

**Constats.** Sur la machine de développement (Ryzen 7 7700X, RTX 5060 Ti), la simulation du
tirage pesait 53 % du parcours de marine sur un seul fil (95 ms). Les finitions de tentabrode
(68 ms) étaient à plus de 80 % dans la construction de la grille des ponts cachés : chaque
point y testait toute sa boîte englobante. La portée `hidden_travel` du profil incluait cette
construction et faisait croire que le coût venait de la recherche de chemin.

**Corrections** (`libs/stitch_generation`, sortie identique octet pour octet) :
- `PullField` : index CSR, grandeurs par segment précalculées, rejet exact avant `exp` ;
- simulation, carte et tirage prédit évalués en parallèle par tranches ;
- grille des ponts cachés : intervalle utile par rangée, carré de la distance hors zone
  ambiguë, bandes de rangées en parallèle (une seule bande à un fil) ;
- plans des ponts pré-calculés en parallèle.

**Résultats** (A/B entrelacé, p50 chauds, natif) : marine 179 → 74 ms (−58 %), tentabrode
270 → 216 ms (−20 %), texte 40 → 32 ms (−20 %), logo inchangé. Sur un fil : −33 %, −6 %,
−9 %. Empreintes identiques sur tous les passages.

**GPU.** Le prototype D3D11 de R01, rejoué sur GPU de bureau, reste 3 à 5 fois plus lent que le
CPU, transferts compris : pas de backend GPU dans le moteur.

## 20. Audit du 2026-11-18 : recalculs inutiles de la génération de points

Question : après une modification locale, que recalcule-t-on sans nécessité ? Chronomètres
temporaires (retirés) sur `effective_sequence` à cache chaud, tentabrode, 1 fil :
`generate_sequence` 0,9 ms (le cache par objet de HP-PERF-002 fait son travail), puis la
finition : `enforce_length_bounds` **9 ms sur ~12**, dont 4 à 6 ms pour reconstruire, à chaque
appel et pour chaque objet portant un point court, ses testeurs de région (rétrécissement
Clipper du tirage effectif + index des arêtes), 1,3 ms de tests de corde (5369) et ~1 ms de
copies du vecteur de commandes. Les testeurs ne dépendent que des paramètres de l'objet et de
ses tracés : ils étaient reconstruits à l'identique à chaque régénération.

**Correction** (`libs/stitch_generation/src/length_bounds.cpp`, sortie identique) :
- cache des testeurs par identifiant d'objet, **clé exacte** (copie des paramètres et des
  tracés, comparés entièrement, comme `chunk_cache`), même interrupteur
  (`set_chunk_cache_enabled`), vidé par `clear_chunk_cache`, borné à 4096 entrées ;
- l'index d'arêtes écrit dans des marqueurs `mutable` à chaque requête : chaque jeu de testeurs
  partagé porte son propre verrou (deux régénérations simultanées restent correctes) ;
- plus de recopie de l'entrée quand la fusion ou le découpage produit déjà sa propre liste.

**Résultats** (banc `openstitch-bench`, tentabrode, Release, médianes de 5) :

| Étape | Avant | Après (1 fil) | Après (8 fils) |
|---|---|---|---|
| `effective_sequence` cache chaud | 9,7 ms | 4,0 ms | 4,6 ms |
| 1 objet médian modifié | 8,8 ms | 3,9 ms | 4,9 ms |
| plus gros objet modifié | 56 ms | 34–48 ms (bruit) | 37 ms |
| `effective_sequence` à froid | 166 ms | 172 ms (inchangé) | 78 ms |

Empreinte de la séquence effective `ea127e23e0aa42b8` identique avant et après, à 1 et 8 fils.
Test : « chunk cache : testeurs de la fusion des points courts suivent paramètres et géométrie »
(avec/sans cache, invalidation par paramètre et par géométrie, 1 fil = 8 fils).

**Suite : « knockout » (`knocked_out_source`)**. Avec l'option « Retirer les recouvrements »
(`--knockout` du banc, ajouté), la phase 1 de `generate_sequence` recalculait pour CHAQUE objet,
et pour chaque remplissage opaque posé au-dessus, le rétrécissement Clipper de la couverture puis
la différence booléenne, séquentiellement et hors de tout cache : `effective_sequence` à cache
chaud passait de 5 à **~230 ms** (tentabrode, 158 objets). Deux caches à clé exacte
(`generate.cpp`, `KnockoutPlanner`) : couvertures rétrécies par (forme, marge) et résultat par
objet, validé par la liste ordonnée (forme, version) des couvertures (une version neuve à chaque
changement de contenu, après comparaison exacte des tracés) ; index des formes par identifiant à
la place de la recherche linéaire. Mêmes interrupteur et vidage que `chunk_cache`, 4096 entrées
au plus.

| Étape (tentabrode, `--knockout`, Release, médianes de 5) | Avant | Après 1 fil | Après 8 fils |
|---|---|---|---|
| cache chaud | 262 ms | 4,8 ms | 5,3 ms |
| 1 objet médian modifié | 261 ms | 4,4 ms | 5,1 ms |
| plus gros objet modifié | 317 ms | 37 ms | 38 ms |

Empreinte `ea127e23e0aa42b8` inchangée (sur ce projet, les régions ne se recouvrent pas : le
banc ne prouve donc que l'absence de régression). L'identité est prouvée par le test « le
knockout en cache suit marge, géométrie et couvertures » : fond sous deux remplissages qui le
recouvrent, résultat avec/sans cache identique après chaque modification (marge, couverture
déplacée, fond modifié, couverture rendue non opaque), le knockout retire bien des points, 1 fil
= 8 fils.

**Satin : mesuré, pas de cache.** Avec `--satin-bands` (115 auto-satins), la phase 3 entière
coûte 2,5 ms à chaud, dont 2,0 ms de `generate_auto_satin` : le squelette, la partie coûteuse,
est déjà en cache. Mettre la séquence en cache serait délicat (l'ordre des colonnes et les
jointures lisent la fin de la séquence déjà assemblée, `sequence.commands.back()`) pour moins de
2 ms : écarté. Satin manuel non mesuré à part (même ordre de grandeur attendu).

**Pistes restantes** (non faites) : plancher à chaud ~4 ms = copies de 60 000 commandes
(assemblage, trois passes de finition) ; gain à mesurer avant de toucher.

## 18. Audit du 2026-11-18 : zones de fondu (`segment_blend_zones`)

Nouveau chemin (PR #385), sans banc ni parallélisme. Banc `[.perf_blend_zones]` de
`test_segmentation` (Release, 2400 x 1800 à 0,1 mm/px, 4 zones, meilleur de 5) : 79 ms.

**Répartition** (rep chaude) : étiquetage final 22 ms, conversion Lab 17, réduction à la
résolution de travail 16, partage des eaux 16, Sobel 7, flou 4, fusions et adjacence < 3.

**Corrections** (`libs/segmentation/src/blend_zones.cpp`, sortie identique octet pour octet) :
- réduction : tables colonne et ligne de travail précalculées (plus de division par pixel),
  lignes de travail réparties en bandes (`parallel_for`) ;
- conversion Lab, Sobel et flou gaussien : bandes de lignes en parallèle ;
- étiquetage final : racines de l'union-find précalculées, bandes de lignes sources en parallèle
  (sommes entières exactes, donc indépendantes de l'ordre), emplacements numérotés dans l'ordre de
  première apparition du balayage, puis conversion racine -> emplacement en parallèle.

**Résultats** : 79 -> 36,5 ms (-54 %) en parallèle ; 82 ms à un fil (`OPENSTITCH_THREADS=1`,
pas de régression). Empreinte du banc identique (6c0bfe59623ab669) ; test à empreintes figées
sur l'implémentation d'origine (3 tailles impaires avec trous transparents), à 1 et 8 fils.

**Reste.** Le partage des eaux (file de priorité, 16 ms) et les fusions sont séquentiels par
nature ; une version par tuiles changerait les zones.
**Suite : « knockout » (`knocked_out_source`)**. Avec l'option « Retirer les recouvrements »
(`--knockout` du banc, ajouté), la phase 1 de `generate_sequence` recalculait pour CHAQUE objet,
et pour chaque remplissage opaque posé au-dessus, le rétrécissement Clipper de la couverture puis
la différence booléenne, séquentiellement et hors de tout cache : `effective_sequence` à cache
chaud passait de 5 à **~230 ms** (tentabrode, 158 objets). Deux caches à clé exacte
(`generate.cpp`, `KnockoutPlanner`) : couvertures rétrécies par (forme, marge) et résultat par
objet, validé par la liste ordonnée (forme, version) des couvertures (une version neuve à chaque
changement de contenu, après comparaison exacte des tracés) ; index des formes par identifiant à
la place de la recherche linéaire. Mêmes interrupteur et vidage que `chunk_cache`, 4096 entrées
au plus.

| Étape (tentabrode, `--knockout`, Release, médianes de 5) | Avant | Après 1 fil | Après 8 fils |
|---|---|---|---|
| cache chaud | 262 ms | 4,8 ms | 5,3 ms |
| 1 objet médian modifié | 261 ms | 4,4 ms | 5,1 ms |
| plus gros objet modifié | 317 ms | 37 ms | 38 ms |

Empreinte `ea127e23e0aa42b8` inchangée (sur ce projet, les régions ne se recouvrent pas : le
banc ne prouve donc que l'absence de régression). L'identité est prouvée par le test « le
knockout en cache suit marge, géométrie et couvertures » : fond sous deux remplissages qui le
recouvrent, résultat avec/sans cache identique après chaque modification (marge, couverture
déplacée, fond modifié, couverture rendue non opaque), le knockout retire bien des points, 1 fil
= 8 fils.

**Satin : mesuré, pas de cache.** Avec `--satin-bands` (115 auto-satins), la phase 3 entière
coûte 2,5 ms à chaud, dont 2,0 ms de `generate_auto_satin` : le squelette, la partie coûteuse,
est déjà en cache. Mettre la séquence en cache serait délicat (l'ordre des colonnes et les
jointures lisent la fin de la séquence déjà assemblée, `sequence.commands.back()`) pour moins de
2 ms : écarté. Satin manuel non mesuré à part (même ordre de grandeur attendu).

**Pistes restantes** (non faites) : plancher à chaud ~4 ms = copies de 60 000 commandes
(assemblage, trois passes de finition) ; gain à mesurer avant de toucher.
