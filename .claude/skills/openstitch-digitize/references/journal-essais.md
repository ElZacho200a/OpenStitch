# Journal d'apprentissage et mesures (essais 1 à 14)

*Historique complet conservé tel quel ; chargé à la demande. Ne pas le lire en entier : repérer l'essai pertinent par l'outil Grep (motif `^\*\*Essai|^### Essai`, avec numéros de ligne), puis lire la plage utile (Read `offset`/`limit`).*

## Essais (erreurs faites, règles qui en sortent)

> Les entrées ci-dessous citent des scripts Python écrits à la volée (`fidelity.py`, `green_holes.py`,
> `render_dst.py` en lignes). Ils sont **remplacés par les commandes du CLI** (`project fidelity`,
> `density`, `render`, `diagnose`, `sweep`, `digitize --debug-dir`), plus fiables : repère exact de
> l'image source, plus de calage par boîte englobante.

Chaque entrée : ce qui s'est passé, ce qui l'a corrigé, la mesure. Image de référence : le navire
au soleil couchant (1448 × 1086 px, 7 teintes + fond beige `#E3D4BB`, `--dpi 122.6` → 290 × 201 mm).

**Essai 1 (2026-10-15, environnement cloud Linux, CLI seul).**

| # | Erreur / piège | Cause | Règle |
|---|---|---|---|
| 1 | Fausse alerte « le crème (soleil, écume) a disparu » : soleil rendu bleu, écume absente | Le rendu colorait les blocs par la **médiane de l'image source** ; le crème `#FCEEC2` était bien segmenté (7,7 %) et brodé (32 objets, 4 933 mm²) | Vérifier la palette réelle (`osp_palette.py --report`) **avant** de conclure à un défaut de numérisation, et rendre avec `--palette` |
| 2 | `project check` : 50 erreurs « hors-cadre » | `project digitize` laisse le cadre à 100 × 100 mm | `project set --width-mm --height-mm` à la taille de la pièce (+ marge) avant tout contrôle ; `workflow.sh` le fait |
| 3 | Pas de points/coupes dans la sortie de `project digitize` | Seul `digitize` imprime ces lignes | Passer `--dst` et lire `stats` ; c'est la source des chiffres comparés |
| 4 | Le JSON de `digitize` ne dit pas quelles couleurs sont brodées ; le SVG de diagnostic n'a pas de couleurs | — | Passer par l'`.osp` (`project inspect --json`) |

Mesures de l'essai 1 (même `--dpi 122.6`, `--max-colors 12`, fond ignoré automatiquement) :

| Variante | Objets | Points | Coupes | Fils | Observation |
|---|---|---|---|---|---|
| base | 344 | 80 393 | 887 | 6 chg. | mâts/haubans/contours de nuages absents (~8 % non couvert) |
| `--max-colors 8` | 300 | 80 285 | 893 | 5 chg. | le crème est quand même brodé (le défaut n'est pas la palette) |
| `--island-cleanup 20` | 173 | 78 420 | 681 | 6 chg. | −49 % d'objets, −23 % de coupes |
| `--dark-lines` | 492 | 92 698 | 1 459 | 7 chg. | gréement lisible, +64 % de coupes |
| `--dark-lines --island-cleanup 20` | 369 | 91 755 | 1 359 | 7 chg. | **meilleur compromis visuel** ; 214 traits cousus en dernier |
| idem `--hidden-travel` | — | 91 475 | 1 317 | 7 chg. | −3 % de coupes, 0 erreur de contrôle, **sortie déterministe** (octets identiques sur 2 exécutions) |
| `--skip-background no` | 346 | 107 096 | 941 | 7 chg. | 299 × 224 mm : le fond beige est brodé (+33 % de points) |

Constats à retenir :

- `--dark-lines` ajoutait **un fil quasi noir distinct** (`#0C121C`) du noir déjà segmenté
  (`#08080F`) : corrigé à l'essai 3 (ci-dessous).
- `project check` plafonne l'affichage à 100 avertissements (sauts longs, points très courts) ;
  seul le verdict de la première ligne est fiable pour un résumé.
- Non testé volontairement : `--satin-bands` (point brevet, §4). À essayer sur demande explicite.
- Non fait : comparaison pixel à pixel avec l'image source (pas d'outil de score de fidélité) ;
  le jugement reste visuel. Idée : script qui reprojette les régions brodées et mesure l'écart ΔE.

**Essai 2 (2026-10-16) : les interstices entre couleurs voisines.**

Hypothèse (vérifiée) : chaque tatami est rétréci de `inset_mm` (0,2 mm par défaut), donc deux
objets voisins laissent ~0,4 mm de vide, visible comme un filet clair autour de chaque zone.
Test sur la variante `--dark-lines --island-cleanup 20` (151 tatami), lot `set_params` via
`scripts/set_params_all.py`, mesure `project check --json` → `project_metrics.uncovered_ratio`
(inclut la part du fond ignoré, ici 0,324 : seul l'écart à cette valeur compte) :

| `inset_mm` | Points | Coupes | `uncovered_ratio` | Lecture |
|---|---|---|---|---|
| 0,2 (défaut) | 91 755 | 1 359 | 0,366 | ~4 % d'interstices, filets clairs visibles |
| 0,1 | 96 706 | 1 362 | 0,342 | ~2 % |
| **0,0** | 106 764 | 1 368 | **0,325** | interstices quasi nuls, rendu net (zoom navire) |
| 0,0 + `pull_compensation_mm` 0,15 | 112 137 | 1 559 | 0,321 | recouvrement, +5 % de points et +14 % de coupes pour rien de plus |
| −0,1 | — | — | — | refusé : `inset_mm` ≥ 0 |

Règle : sur une numérisation multi-couleurs aux zones jointives, **`inset_mm = 0`** (≈ +16 % de
points, coupes inchangées) ; `0,1` si le poids compte. Ne pas ajouter de compensation de tirage
sans raison. À reporter à terme dans le défaut de `digitize` (point à décider avec l'utilisateur :
cela change la sortie de toutes les numérisations).

**Essai 3 (2026-10-16) : défauts corrigés dans le code, plus seulement contournés.**

| Défaut | Cause racine | Correctif | Preuve |
|---|---|---|---|
| 8e fil : noir des traits `#0C121C` à côté de `#08080F` | `line_layer.cpp` : couleur des traits = moyenne des pixels de trait, jamais comparée à la palette ; et les remplissages noirs étaient cousus avant un bleu, donc le noir deux fois | couleur calée sur la région la plus proche (RGB <= 30) ; remplissages de cette couleur placés en dernier (`stable_partition`) | 7 -> 6 changements de fil, points/coupes identiques ; le test échoue sans le correctif (2 assertions) |
| Filets clairs entre zones | `inset_mm` 0,2 sur chaque tatami | option `AutoOptions::fill_inset` + `--fill-inset` (défaut inchangé) | 106 764 points, non couvert 0,325 ; test `fill_inset regle le retrait` |

Résultat de référence (navire, `--max-colors 12 --dark-lines --island-cleanup 20 --fill-inset 0`) :
106 764 points, 1 368 coupes, 6 changements de fil, 290,6 x 200,9 mm, 0 erreur de contrôle.
Suite complète en série (`ctest`) : 1 257 tests sur 1 258 ; seul `cli_help` échoue (environnement, ci-dessous).

Pièges d'environnement (build apt, pas de régression) : `cli_help` attend `POSITIONALS` en
majuscules, le CLI11 d'apt (2.4.1) écrit `Positionals` ; deux tests `project_ops` (`ops malformed
or unknown batches…`, `param ops set_order modes`) ont échoué en `ctest -j4` et passent seuls et en
série (cause non investiguée). Lancer `ctest` sans `-j` pour juger.

**Essai 5 (2026-10-16) : retour de l'utilisateur sur le rendu de l'application (fond vert).**
Défauts signalés : (a) des détails disparaissent, (b) les traits noirs sont posés même quand ils ne
font que séparer deux zones.

Nouvel outil `scripts/fidelity.py` (source contre couverture réelle des points, même échelle, vraie
palette) : classe chaque pixel manquant / mauvaise couleur / en trop et liste les plus grosses
composantes. Erreurs de méthode corrigées en chemin : (1) le crème est à ΔE 10,8 du beige : une
tolérance de fond de 14 le prenait pour du fond (tolérance 6 requise) ; (2) un trou de la couleur
exacte du tissu entre deux nuages n'est PAS un défaut (456 mm2 « manquants » = faux positif) ;
(3) les pixels de transition anti-crénelés ne sont proches d'aucun fil : ne compter une erreur que si
le pixel source est proche (ΔE < 18) d'une couleur de la palette et cousu avec une autre.

Balayage (un réglage à la fois, base `--max-colors 12 --dark-lines --fill-inset 0 --satin-bands`) :

| Variante | Objets | Points | Coupes | Manquant | Détails perdus |
|---|---|---|---|---|---|
| `--island-cleanup 20` | 369 | 114 623 | 1 675 | 0,29 % | 1,60 % |
| `--island-cleanup 0` | 492 | 119 086 | 1 855 | 0,29 % | 0,90 % |
| `--island-cleanup 5` | 458 | 118 526 | 1 865 | 0,29 % | 0,86 % |
| `0` + `--min-region-mm2 1` | 587 | 121 698 | 1 989 | 0,30 % | **0,76 %** |
| idem + `--smoothing-px 1` | 609 | 128 347 | 2 109 | 0,30 % | 0,92 % (pire : bruit) |

Lecture : `--island-cleanup 20` DOUBLE les détails perdus (reflets du soleil sur la mer, petites
formes) ; le gain en coupes (-10 %) se paie en fidélité, à ne pas utiliser par défaut sur une
illustration riche en petits détails. Baisser `--smoothing-px` n'aide pas.
Traits : `--dark-lines-objects-only` (séparation ou vrai objet, voir la doc) : objets de ligne
327 -> 135, coupes -12 %, voiles plus propres ; détails perdus 0,76 % -> 2,31 % (traits non cousus
par choix) et quelques haubans fins perdus. Défaut restant, commun aux deux : autour du mât et de la
coque le noir devient un amas de passes superposées, plus épais que les traits de la source.

**Essai 9 (2026-10-16) : les outils d'analyse sont dans le CLI.**
Bilan demandé : qu'est-ce qui aurait simplifié ma tâche ? Les tâches répétées sont devenues des
commandes (`project sweep`, `fidelity`, `density`, `at`, `diagnose`, `render`, `digitize --debug-dir`,
`scripts/linux-core-setup.sh`) et le corpus `[invariants]`. Gain mesuré : le balayage de 8 variantes qui
demandait une quinzaine d'allers-retours est UNE commande de 40 s.

Ce que les outils ont trouvé immédiatement, sans effort :
- `digitize --debug-dir` (`traits.png`) : en mode `--dark-lines-objects-only`, presque tous les cordages
  étaient classés « séparation » (vert) : les pixels anti-crénelés d'un trait sont étiquetés dans la
  région noire, donc « ciel + noir » = deux zones. **Défaut corrigé** (voir la doc), avec un test qui
  échoue sans le correctif. Cela explique pourquoi ce mode « perdait trop de cordages » (essai 5).
- `project density --heatmap` : les surépaisseurs (rouge) ne sont pas dans les aplats (vert = 1 épaisseur)
  mais le long des frontières entre zones, dans le gréement et dans la mer : c'est la mesure objective
  du « chaotique ». 1 007 mm2 à >= 4 épaisseurs sur le navire (objet 468, satin crème, en tête).
- `project diagnose` sur les anciens projets défectueux : bug des 2 m (erreur), 8e fil noir (info),
  réseau géant de barres (avertissement), liaisons visibles (1,58 % contre 0,00 % avec `--no-underpath`).

Erreurs de méthode : (a) mon test de classification passait avec ET sans le correctif : toujours
vérifier qu'un test échoue sans le correctif (remplacer temporairement la ligne) ; (b) mon corpus
d'invariants (fixture + image synthétique) passait AVEC le bug des poignées réintroduit : il fallait une
image de stress (centaines de petits contours) ; (c) lancer une tâche longue dans un sous-shell détaché
(`( … ) &`) la tue quand la commande se termine : utiliser `run_in_background` directement.

**Essai 8 (2026-10-16) : « c'est bien mais c'est pas mal chaotique » + rendu réaliste headless.**
Je jugeais à l'aveugle (rendu de script en lignes, mesure de bord). Ajout de `openstitch-cli project
render` (moteur `stitch_render` de l'application, fond vert) : on voit désormais ce que voit
l'utilisateur, au niveau du fil. **Toujours regarder le rendu réaliste (`workflow.sh` le produit) avant
de conclure.** Mesure : `scripts/green_holes.py` = tissu visible à l'INTÉRIEUR du motif (1,43 % du
motif sur le navire, alors que `fidelity.py` n'en voyait que 0,03 %).

Constat au fil : (1) dents de scie vertes le long des frontières (bouts de rangées de deux zones
voisines qui ne se recouvrent pas) ; (2) gréement noir en amas : barres satin et triples passes
superposées près des mâts ; (3) contours anguleux (polygones) ; (4) mosaïque d'angles dans la mer.

BUG trouvé en testant `--curve-tolerance` : avec `--dark-lines`, un tatami envoyait des points à 2 m
(DST de 2 130 mm de large). Cause racine : `fit_curves` (Schneider) sans borne haute sur la longueur des
poignées (voir `docs/source/vectorization.md`). Corrigé, test de propriété. Méthode : bissection par
`set_visible` des objets -> un seul tatami -> ses anneaux à 2 nœuds -> poignées.

Compensation de tirage (`pull_compensation_mm`, tatami + satin) contre le vert intérieur : 0 -> 1,43 % ;
0,1 -> 1,18 % ; 0,2 -> 1,24 % ; 0,35 -> 0,98 % (+4 % de points) : aide peu, ne ferme pas la dentelure.
**Brins diagonaux dans les fills (cause du « chaotique » en vue normale)** : liaisons `hidden_underpath`
cousues par-dessus. `--no-underpath --hidden-travel` : coupes 1 906 -> 2 653 (+39 %), fills propres
(`--no-underpath` seul : 3 222). Leviers écartés par la mesure du vert intérieur : `--fill-overlap`
(1,52 -> 1,30 % pour +13 % de points), compensation de tirage, filtre des points courts.
Contours lisses (`--curve-tolerance`, base `--island-cleanup 0 --min-region-mm2 1 --dark-lines-bars 4
--dark-lines-min-length 6`) :

| Variante | Points | Coupes | Vert intérieur | Détails perdus |
|---|---|---|---|---|
| polygones (défaut) | 129 844 | 2 282 | 1,43 % | 0,42 % |
| `--curve-tolerance 0.4` | 117 553 | 1 906 | 1,52 % | 0,50 % |
| `--curve-tolerance 0.8` | 109 219 | 1 758 | 2,20 % | 1,41 % |

Lecture : 0,4 donne des bords de nuages plus lisses, -9 % de points et -16 % de coupes pour un peu plus
de détails perdus ; 0,8 perd trop. Recommandé : 0,4. Non résolu : amas noir du gréement, dentelure verte,
angles de la mer.

**Essai 7 (2026-10-16) : capture de l'application (fond vert) de l'essai 6 : « pas besoin d'expliquer ».**
Lecture de la capture (zoom navire et mer) : (1) taches noires rondes sur la coque ; (2) trous verts
(tissu visible) partout, surtout dans la mer à côté des bouts de trait noir ; (3) tirets noirs plats
dans la mer ; (4) mer en mosaïque d'angles de fil. La métrique de bord (0,3 % manquant) ne voyait RIEN :
les trous sont à l'intérieur ; `fidelity.py` donne maintenant « trou interieur » (`--close 1 --open 0
--min-px 12 --thread-mm 0.6` pour voir les trous de 1 mm2 ; sans fermeture le tatami lui-même passe
pour 11 % de trous, inutilisable).

Cause racine commune de (1), (2), (3) : le seuil `--dark-lines-lightness` 25 englobait le bleu marine
(L* ~ 16). Voir la doc (« Seuil de clarté des traits »). Correctif : seuil effectif = pixel le plus sombre
de la palette + 8.

| Variante (`--island-cleanup 0 --min-region-mm2 1 --dark-lines-bars 4`) | Objets | Points | Coupes | Détails perdus | Trous intérieurs |
|---|---|---|---|---|---|
| seuil 25 (avant) | 562 | 125 993 | 2 076 | 0,79 % | 522 px, 24 trous |
| **seuil = noir + 8** | 558 | 130 319 | 2 319 | **0,35 %** | 330 px, 18 trous |
| + `--dark-lines-min-length 6` | 521 | 129 844 | 2 282 | 0,42 % | — |
| + `--dark-lines-min-length 10` | 492 | 129 358 | 2 251 | 0,53 % | — |

Erreurs de méthode : (a) j'avais jugé « 0,3 % manquant » acceptable sans regarder où : toujours
séparer bord et intérieur ; (b) mes zooms de script ne montraient pas les trous que l'application
affiche (rendu réaliste + fond vert) : le fond vert de l'utilisateur est le meilleur révélateur de
trous, le reproduire (`fidelity.py` ou rendu sur fond vert) avant d'annoncer un résultat.
Non traité : (4) angles de remplissage (`AutoOptions::auto_fill_angle` suit l'axe de chaque forme : choix
esthétique à trancher avec l'utilisateur) ; jonctions entre trois zones (<= 1,5 mm2).

**Essai 6 (2026-10-16) : l'amas noir du mât, corrigé par un niveau « barres ».**

Constat (comparaison source sombre / fil noir réel) : cordages fins corrects, mais le mât et les
vergues (barres de 2 à 3 mm) sortaient en amas hachuré. Cause : plus larges que le trait (1,6 mm), donc
laissées au remplissage noir (réseau mât + vergues + coque d'un seul bloc). Correctif :
`--dark-lines-bars 4` (barres cousues en bandes, avant les traits). Voir la doc pour le détail.

| Variante (base : `--island-cleanup 0 --min-region-mm2 1`) | Objets | Points | Coupes | Manquant | Détails perdus |
|---|---|---|---|---|---|
| tous les traits (avant) | 587 | 121 698 | 1 989 | 0,30 % | 0,76 % |
| + `--dark-lines-bars 4` | 562 | 125 993 | 2 076 | 0,29 % | **0,79 %** |
| + barres + `--dark-lines-objects-only` | 370 | 120 382 | 1 840 | 0,92 % | 2,35 % |
| barres, seuil large L* <= 25 (essai raté) | 310 | 124 012 | 1 894 | 1,10 % | 5,53 % |
| traits fins aussi au seuil serré (hypothèse écartée) | 631 | 129 959 | 2 318 | 0,33 % | 0,81 % |

Erreurs de méthode : (1) un seuil « sombre » de L* <= 25 inclut le bleu marine : toute mesure de
« noir » doit se faire au seuil du trait réel (noir, L* ~ 4) plus une marge ; (2) un score de fidélité
baisse mécaniquement quand on retire des traits voulus (`--dark-lines-objects-only`) : ne jamais le
comparer à une variante sans ce filtre sans le dire ; (3) un objet de 1 963 mm2 sur 290 mm de large
est le signe d'un réseau connecté géant : toujours lister les objets les plus grands d'une couleur.
Sur ce navire `--dark-lines-objects-only` perd trop de cordages : à réserver aux images où les
séparations sont nombreuses ; le niveau barres est le gain net.

**Essai 4 (2026-10-16) : les trois méthodes d'auto-satin de `main` (PR #50, #51).**

Fusion de `main` sans conflit. `--satin-bands` (auto-numérisation) utilise **toujours le squelette**
(`autodigitize::choose_satin`) : pour tester les autres méthodes, numériser avec `--satin-bands`
puis convertir les objets `auto_satin` avec
`scripts/set_params_all.py CLI p.osp auto_satin '{"strategy":"medial_axis"}'`
(valeurs : `skeleton` | `parallel_sweep` | `medial_axis`). Un nom inconnu est refusé, document intact.
Navire, `--max-colors 12 --dark-lines --island-cleanup 20 --fill-inset 0 --satin-bands`
(71 objets auto-satin, 80 tatami, 218 traits) :

| Méthode des 71 satin | Points | Coupes | Fil m | Non couvert | Points courts | Erreurs |
|---|---|---|---|---|---|---|
| (sans `--satin-bands`) | 106 764 | 1 368 | — | 0,325 | — | 0 |
| `skeleton` | 114 623 | 1 675 | 211,8 | 0,3253 | 6 543 | 0 |
| `parallel_sweep` | 104 077 | 1 469 | 199,9 | 0,3249 | 5 682 | 0 |
| `medial_axis` | 105 822 | 1 429 | 204,4 | **0,3337** | 5 554 | 0 |

Lecture : à couverture égale, `parallel_sweep` économise 9 % de points et 12 % de coupes sur le
squelette ; `medial_axis` coupe le plus mais **couvre moins** (+0,8 point de non-couvert). Le rendu
de script ne distingue presque pas les trois (le satin y ressemble à un aplat dense) : seules les
mesures tranchent, pas de recommandation avant essai machine (consigne de la doc `satin-squelette.md`).
`satin-compare` (corpus de formes) : `parallel_sweep` génère des fils très longs sur les formes
ramifiées (36 à 40 mm sur `t`, `cross`, `h`) et couvre moins (95,7 à 98,6 %) ; `medial_axis` égale le
squelette et couvre les formes compactes qu'il refuse (`circle`, `disc_15mm`, `petal` : 99,5 à 99,9 %).
Pour des bandes étroites et régulières (rayons de nuages), `parallel_sweep` suffit ; pour des formes
ramifiées ou compactes, `medial_axis`.

### Essai 10 — traiter l'image avant la segmentation (`--pre`)

Idée de l'utilisateur : OpenStitch sait déjà débruiter l'image avant la segmentation (pile `project.ops`
du bureau). Le CLI l'ignorait : ajout de `--pre` (`median:1`, `bilateral:2+median:1`, `quantize:12`…),
non destructif, et les outils d'analyse lisent désormais l'image de travail (un `crop` ne les désaligne plus).
Mesure sur le navire (`project sweep --vary "pre=…"`, 12 couleurs, `--dark-lines --satin-bands`) :

| pre | objets | points | coupes | tissu visible % | détails perdus % |
|---|---|---|---|---|---|
| aucun | 493 | 125 097 | 2 162 | 0,35 | 0,27 |
| median:1 | 549 | 119 545 | 1 926 | 0,33 | 0,35 |
| bilateral:2 | 482 | 121 543 | 1 962 | 0,40 | 0,31 |
| bilateral:3 | 502 | 121 953 | 1 887 | 0,39 | 0,35 |
| bilateral:2+median:1 | 545 | 120 145 | 1 930 | 0,45 | 0,37 |
| quantize:12 | 429 | 124 643 | 2 006 | 0,38 | 0,48 |

Lecture : l'image est déjà un aplat illustré (peu de bruit) ; le gain est modeste : −9 à −13 % de coupes,
−3 à −4 % de points, mais détails perdus +0,04 à +0,2 point et tissu visible stable ou pire. `median:1`
est le meilleur compromis (moins de coupes, tissu visible inchangé) ; `quantize` fait perdre des détails.
Règle : prétraiter les photos / JPEG bruités ; sur une illustration propre, mesurer d'abord avec `sweep`.

### Essai 11 — nouveaux prétraitements : `flatten`, `clahe`, `sharpen`

Ajoutés à `image::ImageOp` (fin du variant, sérialisés dans le `.osp`, menu *Image* du bureau) puis mesurés
sur le navire comme l'Essai 10 :

| pre | points | coupes | tissu visible % | détails perdus % | surépaisseur mm² |
|---|---|---|---|---|---|
| aucun | 125 097 | 2 162 | 0,35 | 0,27 | 1 617 |
| flatten:2 | 121 328 | 2 001 | 0,40 | 0,29 | 1 482 |
| flatten:2+median:1 | 118 679 | 1 891 | 0,37 | 0,28 | 1 411 |
| clahe:2 | 136 405 | 2 555 | 0,67 | 0,93 | 1 832 |
| sharpen:1 | 137 829 | 2 647 | 0,67 | 0,81 | 1 656 |

Lecture : `flatten:2+median:1` est le meilleur réglage mesuré (−12,5 % de coupes, −5 % de points, moins de
surépaisseur, détails perdus au niveau de la référence). `clahe` et `sharpen` **dégradent** une illustration
déjà contrastée (ils fabriquent des régions parasites : +300 objets) ; à réserver aux photos pâles ou floues,
et toujours après mesure par `sweep`.

### Essai 12 — fumée en remplissage directionnel (affiche rouge, fumée cyan)

Besoin : la fumée suit sa forme, le reste en tatami. Manque trouvé : le CLI convertissait en directionnel
(`set_stitch_type`) mais seulement avec un guide droit ; l'action « Générer un guide depuis la forme » du
bureau n'avait pas d'équivalent. Ajout de l'opération `directional_guides`. Recette :
`digitize … --pre flatten:2+median:1 --min-region-mm2 1`, repérer les couleurs de la fumée
(`project inspect --json`, objets tatami cyan / bleu-gris), puis lot `directional_guides` sur ces objets
(`scripts/directional_by_color.py CLI p.osp out.osp '#1C7F8F,#295C6C,…'`). Piège : les couleurs viennent de
la quantification, les lire dans la palette du projet, ne pas les deviner.

### Essai 13 — logo vectoriel (kraken + sous-marin, 3 couleurs, fond transparent)

Premier jet « fidèle » aux réglages du navire : trop de petites erreurs (contours marine dentelés et
interrompus, facettes sur les cercles, liserés verts, traits parasites, 2,6 % de tissu visible). Cause :
les réglages du navire (illustration peinte) lissent trop un dessin vectoriel net. Balayages (`sweep`,
planche zoomée sur la tête) → réglages retenus pour un logo / dessin à plats :
`--smoothing-px 0` (le lissage de 3 px arrondissait et morcelait les contours épais : −60 % de tissu
visible à lui seul), `--curve-tolerance 0.1` (cercles sans facettes), `--fill-overlap 0.15` (ferme les
liserés sans perdre de détails ; 0,3 en perd 5 %), `--island-cleanup 6` (les pixels anti-crénelés
créent 2 fils parasites : supprimés, 5 → 3 fils), pas de `--pre` (flatten/median n'aident pas un aplat
déjà net), ni `--satin-bands` ni `--dark-lines-bars` (éventails radiaux aux coins sombres, +25–40 % de
points, ×9 de surépaisseur). Résultat : 72 827 points, 1 861 coupes, 3 fils, 0,93 % de tissu visible.
Règle : le lissage par défaut est calibré pour des peintures ; pour du vectoriel à bords nets, partir
de `--smoothing-px 0` et vérifier le nombre de fils (`project inspect`) avant tout autre réglage.

### Essai 14 — suivre le mouvement (`--flow-fill`) et les ronds en satin (`--rings-satin`)

Retour : « les cercles se mettent en satin pour un joli rond ; la broderie doit suggérer le mouvement
dans le sens de la couture ». Deux outils manquaient à `digitize` :
- `--flow-fill` : tentacules, corps, rubans (forme allongée : plus long guide² / aire ≥ 2,5, aire ≥ 10 mm²)
  passent en remplissage directionnel dont les guides suivent l'axe médian ; la couleur la plus sombre
  (contours) reste en tatami. Avant : tout en tatami à un seul angle, aspect « tapis ».
- `--rings-satin` (avec `--dark-lines`) : un trou enclos quasi elliptique entouré d'une bande sombre
  d'épaisseur régulière est un anneau ; il est cousu en auto-satin avec une ellipse exacte (rond net,
  pas un polygone facetté) et retiré des traits et du remplissage marine (sinon marine + satin + lignes =
  surépaisseur). 75 anneaux sur le kraken (ventouses, bulles, hublot, œil). Un test géométrique sur les
  trous des objets vectoriels (tentative 1) ne trouvait que 3 anneaux : les anneaux des ventouses touchent
  le contour marine et ne sont jamais des trous isolés ; la détection se fait donc sur l'image.
Piège évité : satin sur un « trou dans une grande zone » (halos en éventail autour des petits points) ;
l'anneau doit avoir un bord extérieur net et une épaisseur bornée.

## 9. Ce skill s'enrichit

## Enrichir ce journal

Quand une nouvelle image révèle un échec ou une règle (seuil, option, ordre), l'ajouter au
journal (§8) avec **la mesure qui la justifie** (chiffre avant/après, image de référence), et
l'écrire aussi dans `docs/source/auto-numerisation.md`. Ne pas y mettre d'hypothèse non vérifiée.
Toute erreur de méthode (fausse alerte, option oubliée) va dans le tableau d'erreurs : c'est ce
qui évite de la refaire.

