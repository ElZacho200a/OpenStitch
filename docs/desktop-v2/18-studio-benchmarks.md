# 18 — Mesurer le canevas Studio et la V2 Widgets

Le banc `openstitch-studio-bench` utilise une **fenêtre native visible de 1280 × 800 pixels
logiques**, pas un rendu Qt offscreen. Il est exclu de CTest et du build par défaut. Les cibles
de 60 FPS et de p95 local < 16,7 ms sont des critères à mesurer sur une machine de référence.

## Scènes et protocole reproductible

Les quatre scènes synthétiques sont de vrais projets C++ avec un objet point droit en serpentin,
pas des objets QML par point. `effective_sequence` génère respectivement 1 000, 10 000, 50 000 et
100 000 commandes Stitch ; le banc vérifie ces comptes. Les positions sont des µm entiers avec
un pas de 1 mm, et les finitions sont désactivées pour garder un compte exact. Le nombre de
segments visibles est rapporté séparément du nombre de commandes machine.

La cinquième scène est un `.osp` réel, par défaut Tentabrode à 300 mm de haut. Le mode de génération
du banc réutilise `run_digitize` et `project_io::save_project` existants ; la résolution d'import
est `dpi = H × 25,4 / 300` pour une image source de hauteur `H` pixels. L'emprise réellement cousue
peut être légèrement plus petite après suppression du fond. Générer le projet :

```powershell
cmake --build --preset msvc-release --target openstitch-studio-bench
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
.\build\msvc\tests\bench\Release\openstitch-studio-bench.exe --generate-project build\tentabrode-300mm.osp --image tests\fixtures\tentabrode.png --height-mm 300
.\scripts\performance\benchmark_studio.ps1 -Project build\tentabrode-300mm.osp
```

Le script exécute les dix cas (5 scènes × 2 fronts) dans des processus séparés pour isoler les
caches et mémoires des scènes précédentes. Il conserve les échantillons JSON, les empreintes
SHA256 du projet et du binaire, les versions Qt/OS, l'inventaire CPU/GPU/pilotes, l'écran/DPR et
le backend obtenu. Il vérifie que les deux fronts reçoivent le même compte de points et la même
empreinte des segments (géométrie et couleurs). L'inventaire des GPU Windows ne prouve pas quel
adaptateur Qt a choisi.

Chaque cas laisse passer une première frame puis 30 gestes de chauffe, suivis de 240 gestes
mesurés. Un geste translate de 2/0,5 pixels logiques et alterne un facteur de zoom de 1,002 et
son inverse, au centre. Le sens du pan s'inverse tous les 30 gestes. Le geste suivant est demandé
après la fin du précédent. Les fenêtres doivent rester dégagées, sur le même écran, avec le même
DPR et la même politique d'alimentation ; ne pas exécuter les deux fronts simultanément.

Commande ciblée pour diagnostic :

```powershell
.\build\msvc\tests\bench\Release\openstitch-studio-bench.exe build\tentabrode-300mm.osp --backend quick --scene 100000 --frames 240 --warmup 30 --json build\quick-100k.json
```

## Ce que les chiffres signifient

| Champ | Limite de mesure |
|---|---|
| `completion_fps` / `frame_interval_ms` Quick | Cadence des `QQuickWindow::frameSwapped` consécutifs, horodatés sur le thread de rendu. Inclut la régulation de présentation de Qt/pilote. Ce n'est pas un temps d'exécution GPU. |
| `completion_fps` / `frame_interval_ms` Widgets | Cadence de fin de `PreviewCanvas::paintEvent`, côté CPU. Une valeur supérieure à la fréquence écran n'est **pas** un FPS affiché ; le compositeur peut regrouper plusieurs peintures. |
| `local_interaction_to_completion_ms` | Temps entre l'appel C++ de caméra et la fin swap/peinture correspondante. Exclut le périphérique et la file d'événements d'entrée de l'OS ; ce n'est pas une latence photon à l'écran. |
| `load_ms` | Chargement du `.osp` ou construction du projet synthétique, avant l'installation. Le cache disque de l'OS n'est pas purgé. |
| `scene_build_ms` | Calcul de la séquence effective, segments, emprises et sommets CPU. Le même instantané est construit dans les deux cas pour contrôler leur équivalence. Ce surcoût de validation n'est pas requis par le shell Widgets normal. |
| `preview_raster_ms` | Widgets seulement : `render_preview` à 3 px/mm puis décodage PNG, avec finitions/qualité réaliste existantes. Ce produit visuel diffère des segments plats Quick. |
| `install_to_first_completion_ms` | Installation, création de fenêtre/ressources et première frame. Ce n'est pas le temps isolé d'upload ni celui des seuls buffers GPU. |
| `qsg_resource_creation_cpu_ms` | Temps CPU d'allocation/remplissage des géométries QSG, diagnostic du canevas. Exclut le transfert réel dans le pilote et l'exécution GPU. |
| `initial_geometry_build_count`, `geometry_build_count` | Compteurs avant/après les gestes, attendus égaux après l'installation ; ils vérifient l'invalidation du canevas. Ils ne mesurent pas les allocations internes du pilote. |
| `cpu_scene_*_bytes`, `cached_image_bytes` | Tailles de stockage connues de l'instantané/image ; ne couvrent pas la totalité de Qt, du projet et des caches moteur. |
| `process_working_set_mb`, `process_peak_working_set_mb` | Mémoire résidente/pic du processus Windows, après les frames. Le banc conserve aussi des données de contrôle ; aucune attribution VRAM n'est déduite de ces chiffres. |
| `gpu_memory_bytes`, `gpu_execution_ms` | `null` : aucune mesure portable de mémoire VRAM ou timer GPU implémentée. |
| `viewport_logical_px`, `window_device_pixel_ratio`, `window_screen` | Métadonnées de la fenêtre réellement utilisée par chaque cas, pour contrôler l'équivalence entre fronts. |
| `msaa_samples_requested` | Nombre demandé par le banc Quick ; le nombre réellement obtenu est vérifié dans le journal Qt, pas déduit de cette demande. |

Les médianes/p95 utilisent le rang supérieur (`ceil(p × N)`) sur les échantillons triés. Les
données brutes sont incluses ; conserver aussi la variance entre au moins trois exécutions avant
une décision de remplacement. La fenêtre du banc ne contient que le canevas : la fluidité de la
liste, des panneaux et des calculs lourds du shell exige une recette distincte.

La comparaison porte sur **le même projet et les mêmes gestes**, avec deux stratégies de rendu
réelles : géométrie colorée GPU contre image réaliste précalculée. Leur qualité visuelle, leurs
marges de cadrage et leur limite de mesure diffèrent. Les chiffres ne justifient pas un facteur
de supériorité globale de Qt Quick ; un rapport doit présenter ces différences avant d'en tirer
une conclusion. Le FPS Widgets présenté par l'écran reste indisponible sans instrumentation du
compositeur (par exemple une capture de présentation externe), et ne doit pas être inventé.

Le champ `software_backend` distingue le repli Qt Quick CPU d'un backend RHI. Une API RHI comme
D3D11 ne prouve pas, à elle seule, l'usage d'un GPU physique (un pilote peut utiliser WARP).
Conserver le journal `QSG_INFO=1` pour identifier l'adaptateur effectivement initialisé. Une
mesure du backend logiciel doit être étiquetée CPU et ne peut pas valider la cible GPU.

## Résultats et critères

Passe finale mesurée le **2026-11-07 en Europe/Paris** (23:09 UTC le 2026-11-06 dans les JSON),
dix processus Release : Qt 6.8.3, Windows 11 25H2,
AMD Ryzen 7 7700X, NVIDIA GeForce RTX 5060 Ti (pilote Windows 32.0.16.1088). Le journal
`QSG_INFO` confirme D3D11 et cette carte sélectionnée, avec la boucle de rendu threadée.
Le rendu Quick obtient **4 échantillons MSAA**, comme le shell livré.
Écran Samsung U28E590 annoncé à **59,997 Hz**, DPR **1,5** ; 1280 × 800 pixels logiques
correspondent à 1920 × 1200 pixels physiques. Ces métadonnées sont vérifiées pour la fenêtre de
chaque cas des deux fronts. Les backend/render loop/DPR n'étaient pas forcés par les variables
d'environnement du script. [Données brutes, métadonnées et journal](benchmarks/2026-11-07-msaa4/README.md).
La [première passe MSAA 1×](benchmarks/2026-11-06/README.md) reste disponible séparément ; une
seule passe par configuration ne permet pas d'attribuer leurs écarts au seul MSAA.

Le projet Tentabrode généré contient **138 objets, 43 670 commandes Stitch et 43 660 segments
visibles**. Le paramètre 300 mm concerne l'image source ; l'emprise effectivement cousue est
**230,202 × 276,870 mm** après suppression du fond. Les quatre scènes synthétiques comptent
respectivement 999, 9 999, 49 999 et 99 999 segments visibles. Les empreintes des segments et les
comptes de points sont identiques entre les deux fronts pour les cinq scènes.

### Cadence et interaction Quick

Ces valeurs sont des intervalles `frameSwapped`, régulés par l'écran ; elles ne donnent pas le
temps d'exécution du GPU. Durées en millisecondes.

| Scène | Cadence swap (Hz) | Trame médiane | Trame p95 | Interaction médiane | Interaction p95 | Constructions des points avant → après |
|---|---:|---:|---:|---:|---:|---:|
| 1 000 | 59,85 | 16,657 | 17,808 | 16,515 | 17,506 | 1 → 1 |
| 10 000 | 59,85 | 16,653 | 19,388 | 16,554 | 18,888 | 1 → 1 |
| 50 000 | 60,00 | 16,680 | 16,914 | 16,528 | 16,749 | 1 → 1 |
| 100 000 | 60,00 | 16,660 | 17,028 | 16,521 | 16,808 | 1 → 1 |
| Tentabrode | 60,00 | 16,663 | 17,018 | 16,488 | **16,829** | 1 → 1 |

La cadence moyenne approche celle de l'écran (59,85 à 60,00 Hz), avec de la variabilité dans les
intervalles. La cible stricte **p95 local < 16,7 ms n'est atteinte sur aucune des cinq scènes**
dans cette passe ; les p95 vont de 16,749 à 18,888 ms. Tous les p95 d'intervalle de trame dépassent
également 16,7 ms (16,914 à 19,388 ms). Les variations ne sont pas monotones avec la taille de
scène : une passe ne permet pas d'en attribuer la cause. Les compteurs restent à une construction
malgré les 270 gestes de caméra : le buffer de points est réutilisé. Ces résultats n'établissent
ni une marge à 120/144 Hz, ni une cadence avec édition lourde ou sélection massive. Il faut
répéter la passe et profiler les écarts avant une décision de bascule.

### Peinture du canevas Widgets

La fin de `paintEvent` est mesurée côté CPU. Les cadences ci-dessous ne sont **pas des FPS
présentés à l'écran** et ne doivent pas être divisées par les valeurs Quick pour calculer un gain.

| Scène | Peintures CPU/s | Intervalle médian (ms) | Intervalle p95 (ms) | Interaction médiane (ms) | Interaction p95 (ms) |
|---|---:|---:|---:|---:|---:|
| 1 000 | 781,75 | 1,179 | 2,046 | 0,573 | 1,121 |
| 10 000 | 772,85 | 1,179 | 2,165 | 0,588 | 0,993 |
| 50 000 | 858,66 | 1,080 | 1,562 | 0,541 | 0,801 |
| 100 000 | 838,16 | 1,082 | 1,686 | 0,545 | 0,878 |
| Tentabrode | 899,58 | 1,015 | 1,694 | 0,477 | 0,795 |

Le canevas Widgets déplace une image réaliste déjà calculée. Ces temps CPU courts confirment
qu'il n'est pas nécessaire de recalculer les fils pour pan/zoom dans cette architecture non plus.
Le FPS réellement présenté et la latence de présentation Widgets restent **non mesurés**.

### Chargement et création des ressources

Durées d'une exécution par cas, en millisecondes ; ce ne sont pas des distributions. « Charger »
est la fabrication du projet pour les scènes synthétiques et la lecture OSP pour Tentabrode.
La colonne QSG exclut les transferts/exécutions du pilote. « Première » inclut l'installation,
la création de fenêtre et la première peinture/swap, après les calculs de préparation.

| Scène | Front | Charger | Scène CPU | Ressources QSG CPU | Raster réaliste + PNG | Première |
|---|---|---:|---:|---:|---:|---:|
| 1 000 | Quick | 0,022 | 0,342 | 0,030 | — | 397,983 |
| 1 000 | Widgets | 0,024 | 0,290 | — | 27,451 | 127,678 |
| 10 000 | Quick | 0,153 | 2,771 | 0,270 | — | 313,915 |
| 10 000 | Widgets | 0,108 | 2,120 | — | 28,736 | 126,815 |
| 50 000 | Quick | 0,550 | 9,666 | 1,627 | — | 290,723 |
| 50 000 | Widgets | 0,564 | 8,623 | — | 62,750 | 130,008 |
| 100 000 | Quick | 0,934 | 19,185 | 1,645 | — | 260,103 |
| 100 000 | Widgets | 1,163 | 21,412 | — | 118,732 | 128,300 |
| Tentabrode | Quick | 60,462 | 35,787 | 0,800 | — | 264,376 |
| Tentabrode | Widgets | 55,876 | 34,789 | — | 68,872 | 133,201 |

Le temps de première frame Quick est ici de 260 à 398 ms, supérieur à l'objectif local
d'interaction ; ce coût de démarrage doit être suivi séparément. Les scènes CPU et le rendu
réaliste n'offrent pas la même qualité visuelle. La scène CPU construite aussi dans le cas
Widgets sert au contrôle d'équivalence du banc ; le shell Widgets normal n'en a pas besoin.
Le cache disque de l'OS n'a pas été purgé et aucune variance entre processus n'est estimée.
Ces calculs synchrones du banc ne décrivent pas le threading du shell, qui les place en tâche.

### Mémoire CPU observée

Valeurs en MiB (2^20 octets). La taille connue de scène additionne segments et sommets ; elle
est identique dans les deux cas. La mémoire résidente/pic inclut Qt, le projet, les caches et
les données de contrôle du banc : elle ne mesure pas seulement le renderer.

| Scène | Scène segments + sommets | Image Widgets | Quick résident / pic | Widgets résident / pic |
|---|---:|---:|---:|---:|
| 1 000 | 0,099 | 0,061 | 65,56 / 66,14 | 45,67 / 45,67 |
| 10 000 | 0,992 | 0,415 | 66,58 / 67,06 | 46,88 / 46,88 |
| 50 000 | 4,959 | 1,880 | 92,09 / 96,39 | 60,64 / 65,02 |
| 100 000 | 9,918 | 3,660 | 113,82 / 123,63 | 75,04 / 100,16 |
| Tentabrode | 4,330 | 2,202 | 98,79 / 102,49 | 73,83 / 76,02 |

Dans cette passe, le processus Quick consomme davantage de mémoire résidente que le processus
Widgets. Aucune consommation GPU ni durée d'exécution GPU n'est disponible ; ces champs restent
`null` dans les données. Aucun chiffre de capture offscreen n'est utilisé comme mesure GPU.
**L'absence de résultat sur une autre plateforme ou un autre backend n'est pas une réussite implicite.**

### Essai matériel OpenGL supplémentaire

Le même binaire et la même scène 100k ont aussi fonctionné sous **OpenGL 4.6 NVIDIA**, MSAA 4×,
sur la RTX 5060 Ti. Les dimensions, l'écran et le DPR restent identiques. Cet essai utilise
**120 frames après 20 gestes de chauffe**, au lieu de 240/30 en D3D11 ; il n'est pas inclus
dans les statistiques de la passe principale.

| Backend / scène | Échantillons / chauffe | Swap (Hz) | Trame médiane / p95 (ms) | Interaction médiane / p95 (ms) | Constructions |
|---|---:|---:|---:|---:|---:|
| OpenGL 4.6 / 100 000 | 120 / 20 | 59,98 | 16,667 / 17,283 | 16,533 / 17,221 | 1 → 1 |

Chargement synthétique : 0,834 ms ; scène CPU : 15,528 ms ; ressources QSG CPU : 1,629 ms ;
première frame : 245,148 ms. Mémoire résidente/pic du processus : **259,77 / 266,02 MiB**.
Les [données et le journal OpenGL](benchmarks/2026-11-07-msaa4/README.md) confirment le renderer
matériel et le MSAA réellement obtenu. Le seuil local p95 < 16,7 ms n'est pas atteint ici non
plus. Un seul essai avec un autre nombre d'échantillons ne démontre ni une supériorité de backend,
ni une parité de mémoire ; Vulkan, Metal et les autres plateformes restent non validés.

Le prototype est validé fonctionnellement quand il charge une vraie broderie, garde le document
inchangé et manipule sa caméra/sélection. La cible de performance est évaluée séparément : 60 FPS
sur des scènes représentatives, p95 local visé à 16,7 ms, aucun recalcul des points ou des buffers
pendant les gestes. Le protocole de repli est décrit dans [l'architecture](17-studio-gpu.md).

Références : [QQuickWindow::frameSwapped](https://doc.qt.io/qt-6.8/qquickwindow.html#frameSwapped),
[rétention des géométries du renderer Qt Quick](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph-renderer.html).

## Survol et clic : `StudioSceneSnapshot::pick` (PERF-015, volet scène de points)

`pick` est appelé à chaque mouvement de souris par le survol (`GpuCanvas::hoverMoveEvent`), au
clic et par les outils. Il parcourait tous les segments à l'envers : coût **linéaire**, sur le fil
d'interface. Le snapshot porte désormais une grille uniforme immuable (`StudioPickGrid`, format
CSR, cellules de 2 mm, plafond de 4 M de cellules) construite sur le worker par
`build_studio_scene`. Chaque cellule liste les segments par indice croissant ; `pick` prend le plus
grand indice qui touche le point, donc **le même objet que le balayage** (dernier segment cousu,
`objectId` 0 ignoré, segments non finis ignorés). Garde-fous : grille absente, périmée (taille de
`segments` différente) ou requête de plus de 4 096 cellules → repli sur le balayage ; scène
pathologique (> 32 entrées par segment) → pas de grille.

Mesure : `test_desktop_v2_core "[pick]"`, Release, MSVC, scène synthétique en serpentin (pas de
0,5 mm, 1 000 segments par objet), tolérance 0,3 mm, 2 000 requêtes pseudo-aléatoires fixes par
taille, 3 exécutions (écarts < 5 %). Machine de développement non étalonnée : ces chiffres ne
valent que pour comparer avant/après sur la même machine ; pas de GPU, pas de fenêtre.

| Segments | balayage p50 / p95 | grille p50 / p95 | construction (worker) |
| ---: | ---: | ---: | ---: |
| 10 000 | 14 µs / 28 µs | 0,1 µs / 0,6 µs | 0,75 ms |
| 100 000 | 144 µs / 283 µs | 0,4 µs / 1,0 µs | 7,3 ms |
| 500 000 | 720 µs / 1 410 µs | 0,5 µs / 1,3 µs | 36 ms |

Lecture honnête : à 100 000 segments le balayage coûtait ~1,7 % d'une frame de 16,7 ms, et ~8,5 %
à 500 000. Ce n'était pas un gel, mais une marge consommée par un mouvement de souris sans rapport
avec la charge de travail ; la grille la rend négligeable. **Non mesuré** : le coût mémoire exact de
la grille sur un `.osp` réel (Tentabrode), le temps de frame de l'application entière, et l'effet
sur le p95 d'interaction bout en bout. `test_studio_scene.cpp` vérifie l'équivalence exacte avec le
balayage sur 20 000 requêtes (tolérances 0 à 150 mm, requêtes hors scène, segments d'`objectId` 0,
longs, diagonaux et de longueur nulle, grille périmée).
