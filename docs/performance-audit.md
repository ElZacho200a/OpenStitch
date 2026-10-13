# Audit de performance

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

1. **Auto-satin sur jonctions à haut degré** : ~14 s sur deux régions de
   tentabrode (génération de candidats de coupe de `split_region`, puis
   oracle de couverture). Correctif architectural déjà identifié dans
   `satin_plan.hpp`, hors périmètre.
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
