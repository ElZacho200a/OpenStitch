# Analyse et validation

Public : utilisateur avancé, développeur. État : **Implémenté** (règles de base).

## But

Détecter, avant l'export, les problèmes courants de broderie sans jamais
appliquer de correction automatique silencieuse.

## Règles détectées

`analyze(sequence, options)` renvoie une liste de `Finding` triés par gravité :

| Catégorie | Condition (par défaut) | Gravité |
|---|---|---|
| `vide` | aucun point | Erreur |
| `point-court` | segment cousu < `near_zero_stitch` (0,1 mm : probable doublon), hors points d'arrêt (passe `Lock`) ; un constat par point | Avertissement |
| `points-courts` | segments cousus de 0,1 à 0,5 mm (`min_stitch`), hors points d'arrêt ; **un seul constat par objet** (nombre, plus court, premier point) | Information |
| `point-long` | segment cousu > 7 mm | Avertissement |
| `saut-long` | saut > 30 mm **dont le déplacement ne contient aucune coupe** (une coupe avant ou après le saut suffit à ne rien signaler) | Avertissement |
| `saut-sans-coupe` | déplacement > `trim_threshold` (3 mm) sans `Trim` | Avertissement |
| `hors-cadre` | point hors du cadre (si fourni) | Erreur |
| `trop-de-points` | > 100 000 points | Avertissement |
| `couches-superposees` | épaisseur de fil > `max_layer_thickness` (3,0 couches de remplissage dense) sur un bloc d'au moins 2 × 2 cases de 2,5 mm | Avertissement |
| `detail-trop-petit` | objet (hors points d'arrêt) dont la boîte englobante fait moins de `min_object_size` (1 mm) sur **les deux** axes ; un constat par objet ; 0 = désactivée | Information |
| `objet-sans-arret` | objet cousu sans aucun point d'arrêt (passe `Lock`) ; actif seulement si `check_lock_stitches` (posé par `options_from_project` quand les finitions sont activées) | Information |
| `trop-de-coupes` | plus de `max_trims` coupes ; 0 (défaut) = désactivée | Avertissement |
| `densite-excessive` | zone (grille de 1 mm) à plus de `max_penetrations_per_mm2` pénétrations/mm² ; 0 (défaut) = désactivée | Avertissement |

**Épaisseur de fil** (`couches-superposees`). Grille de `layer_cell` (2,5 mm) ;
dans chaque case, épaisseur = longueur de fil × `thread_width` (0,4 mm) /
surface de la case, en « couches de remplissage dense » : un remplissage dont
l'écart entre rangées égale la largeur du fil vaut 1, une sous-couche espacée de
2 mm vaut environ 0,2. Deux remplissages superposés avec leurs sous-couches
font ~2,4 (les bords ~2,6), trois ~3,6 ; le seuil de 3,0 les départage. Les
passes `Lock` et `Travel` ne comptent pas. Seule une zone contenant un bloc de
2 × 2 cases en excès est signalée : le bord d'un objet, ses connexions de
rangées et deux objets voisins ou empiétant d'1 mm (pratique courante contre
les interstices) ne déclenchent rien. Un seul résultat par zone, localisé sur
sa première case. Motivation : Liu et al. (CGF 2023) ne superposent que deux
couches pour limiter la déformation du tissu. `max_layer_thickness = 0`
désactive la règle. Les seuils sont des heuristiques d'ingénierie, pas des
valeurs de l'article.

Chaque problème porte une **gravité** (`Info`/`Warning`/`Error`), un **message**,
une **localisation** et l'**objet** concerné. Un plafond par catégorie évite
l'inondation ; le résultat est **déterministe**.

### Analyse unifiée : `analyze_project`

`analyze_project(project, sequence)` est le **point d'entrée unique** du panneau
*Analyse*, du pré-export, du dump de débogage et de `project quality` (CLI). Il
applique les seuils du projet (`options_from_project`, finitions comprises) et le
cadre brodable `canvas_hoop` (canevas moins la marge), puis enchaîne : règles de
séquence (`analyze_detailed`), règles de lettrage (`analyze_text_objects`),
réglages hors limites (`check_stitch_limits`). Avant ce changement, le panneau,
l'export et le CLI n'exécutaient pas les mêmes règles (UX-10).

`hors-cadre` est **regroupé par objet** (un constat par objet, ordre d'identifiant,
plafond par catégorie conservé) ; les points sans objet donnent un constat global.

### Corrections proposées : `Finding::fix`

Un constat peut porter un `Fix` : donnée pure (type, valeur, objet), jamais
appliquée par le moteur. Le bureau la traduit en **une** commande annulable
(`ICommand`) derrière le bouton *Corriger* du panneau.

| Constat | `FixKind` | Effet |
|---|---|---|
| `hors-cadre` | `MoveIntoHoop` | translation minimale (µm entiers) qui fait rentrer la boîte englobante de l'objet dans le cadre ; aucune correction si la boîte dépasse le cadre sur un axe |
| `texte-trop-petit` | `EnlargeText` | hauteur de capitale portée à 5 mm |
| `parametre-hors-limites` | `ClampStitchLength` | longueur ramenée dans la plage du projet |
| `point-long` | `SplitLongStitches` | `finishing.split_long_stitches = true`, proposé seulement si cela résout le constat |
| `saut-sans-coupe` | `EnableFinishing` | `finishing.enabled = true`, proposé seulement si les finitions sont désactivées **et** si leur simulation supprime le constat |

Il n'existe pas d'interrupteur « coupes seules » : activer les finitions ajoute
aussi les points d'arrêt, les coupes avant changement de fil et le filtre des
points courts ; le libellé du bouton le dit. `trait-trop-fin`, `saut-long`,
`couches-superposees`, `point-court`, `points-courts`, `trop-de-points` et `vide`
n'ont pas de correction automatique (pistes textuelles seulement). `fix` n'est jamais
sérialisé dans le JSON du CLI.

### Mesure sur corpus et élagage (2026-10-17)

Mesure avec `openstitch-cli project check` sur les trois images du dépôt
(`tentabrode`, `sample`, `logo_small`) × cinq variantes de numérisation (défaut,
`--satin-bands`, `--dark-lines`, `--flow-fill`, `--satin-bands --dark-lines`) ×
trois états de finitions (activées, désactivées, filtre des points courts retiré),
soit 45 projets, avec un `--dpi` qui fait tenir le dessin dans le cadre. Limite : ni
texte ni réglage de longueur fait à la main, donc les règles correspondantes ne sont
**pas exercées** (zéro constat ne prouve pas qu'une règle est inutile).

Avec les finitions par défaut (15 projets), avant la correction :

| Règle | Constats | Projets | Constat |
|---|---|---|---|
| `point-court` | 1 835 | 15/15 | le filtre des points courts retire ~90 % des cas mais épargne le premier et le dernier point d'un tracé, les voisins d'un point d'arrêt et toute fusion qui sortirait de la région (`finish.cpp`) |
| `saut-long` | 312 | 15/15 | `long_moves_without_trim` vaut 0 partout : chaque grand déplacement a déjà sa coupe, donc « envisagez une coupe » était faux dans 100 % des cas |
| `couches-superposees` | 6 | 4/15 | seulement avec `--satin-bands` (3,3 à 4,3 couches) ; règle conservée |
| `saut-sans-coupe` | 0 | 0/15 | 2 072 constats avec les finitions désactivées : c'est un détecteur « finitions éteintes » |
| `point-long` | 0 | 0/15 | y compris sur un auto-satin de 12 mm : le moteur fractionne ses traversées ; règle conservée (longueurs réglées à la main) |

`trop-de-points` : maximum observé 17 052 points pour un seuil de 100 000 ; aucune
estimation de durée n'existe dans le code (il faudrait une vitesse machine).

Décisions appliquées : `saut-long` ne signale plus un déplacement qui contient une
coupe (avant ou après le saut) ; `point-court` est scindé en `point-court`
(< `near_zero_stitch`, 0,1 mm, un avertissement par point) et `points-courts` (de
0,1 à 0,5 mm, un constat d'information par objet). Après correction, mêmes 15 projets :
`saut-long` 312 → 0 ; `point-court` 1 835 → 414 (10/15 projets) ; `points-courts` 336
constats d'information (un par objet) ; `couches-superposees` inchangé (6). Finitions
désactivées : `saut-sans-coupe` (2 072) et `saut-long` (312) inchangés, ce qui est
voulu (aucune coupe dans ces séquences).

Reste ouvert : les 414 points sous 0,1 mm ne sont pas expliqués. Ils pourraient être
des doublons de la génération (cause non confirmée) ; à instruire avant toute
décision sur cette règle. Le seuil de 0,1 mm vient de cette mesure, pas d'une limite
physique.

### Carte de densité et règles supplémentaires (HP-VIEW-005, HP-PROD-007)

`density_map.hpp` calcule une **carte de densité** sur la séquence effective : grille de
`DensityOptions::cell` (1 mm), alignée sur des multiples de la case, qui compte par case les
**pénétrations** (commandes `Stitch`, toutes passes) et la **longueur de fil** (épaisseur en
couches de remplissage dense, même unité que `couches-superposees`). `find_density_hotspots`
renvoie les zones 4-connexes au-dessus de `max_penetrations_per_mm2` (4,0 par défaut), avec boîte,
aire, densité maximale et objet dominant, dans l'ordre de balayage de la grille. Le nombre de
cases est borné (`max_cells`, 1 000 000) : au-delà, le côté de case est doublé. Le seuil de 4,0 est
une heuristique (un remplissage dense fait ~0,8 /mm², deux couches avec sous-couches ~2,5), **non
mesurée sur un corpus** : la règle `densite-excessive` est donc désactivée par défaut
(`AnalysisOptions::max_penetrations_per_mm2 = 0`). L'affichage de la surimpression colorée reste à
faire dans l'interface.

Les autres règles ajoutées (`detail-trop-petit`, `objet-sans-arret`, `trop-de-coupes`) sont
décrites dans le tableau ci-dessus. Seul `detail-trop-petit` (1 mm) est actif par défaut ; il n'a
pas été mesuré sur le corpus du dépôt. Restent non faites : satin trop large, fil trop proche de la
couleur du tissu, nombre de couleurs supérieur au nombre d'aiguilles (il manque le profil machine,
HP-HOOP-002, et la couleur du tissu, HP-THR-008).

### Consommation de fil et canettes (HP-PROD-005)

`thread_consumption.hpp` dérive de `thread_usage` (longueur cousue par fil) la consommation **par
couleur**, en mètres : `top_m = cousu × top_path_factor (1,15) + passages × waste_per_block_mm
(50 mm)`, `bobbin_m = top_m × bobbin_per_top_ratio (0,35)`. Le rapport donne les totaux et le
nombre de canettes (`ceil(total canette / bobbin_capacity_m)`, 100 m par défaut) avec le
remplissage de la dernière. Ce sont des **ordres de grandeur réglables**, jamais des mesures : la
consommation réelle dépend du fil, de la tension et du tissu. Non affiché dans la fiche de
production ni dans l'interface pour l'instant.

## Mesures de qualité (Lot G, audit marine plein cadre)

En plus des règles, deux fonctions **mesurent** une séquence
(`openstitch-cli stats` et `digitize` les impriment) :

- `sequence_metrics(sequence, options)` (`metrics.hpp`), calculable sur un DST
  relu : nombre de points, de déplacements (une suite de sauts = un
  déplacement) et de coupes ; **déplacements plus longs que `trim_threshold`
  sans coupe** ; **points de moins de 0,5 mm hors points d'arrêt**, les points
  d'arrêt courts étant comptés à part. Sur un DST relu, les passes sont
  perdues : avec `infer_locks`, un point qui revient exactement sur la
  position d'il y a deux piqûres (aller-retour) est reconnu comme point
  d'arrêt. S'y ajoute l'**histogramme des directions** des points d'au moins
  1 mm, modulo 180°, par tranches de 5°. Une piqûre de longueur nulle à
  l'arrivée d'un saut compte comme point court : c'est un enregistrement de
  0 mm dans le fichier machine. `length_tolerance` évite de compter comme
  « long » un déplacement que la quantification DST (pas de 0,1 mm, erreur
  jusqu'à ~0,07 mm) a fait passer d'un cheveu au-dessus du seuil ;
  `openstitch-cli stats` relit un DST avec une tolérance d'une unité DST
  (0,1 mm).
- `project_metrics(project, sequence, options)` (`project_metrics.hpp`), qui
  a besoin du projet : **objets brodés de moins de 3 mm²** (objet vectoriel
  suivi, compté une fois), **angles de remplissage** des tatami, **part de
  l'image non couverte** par les points et une ventilation des déplacements et
  des points courts par type d'objet et passe.

  La couverture se calcule sur les pixels de la segmentation, hors fond
  transparent et hors couleur du fond ignoré (`excluded_rgb`). Un pixel est
  couvert si son centre est à moins de `coverage_width / 2` (0,25 mm) d'un
  segment cousu. Le repère est le repère vectoriel centré sur l'image, comme à
  la vectorisation. Cette mesure n'est pas calculable depuis un DST seul
  (origine décalée, pas d'image).

## `point-long` fantôme juste après un saut (2026-08-12)

**Défaut trouvé en usage réel** (export debug utilisateur, objet satin) : la
règle `point-long` comparait chaque point cousu à `prevStitch`, la position du
DERNIER point cousu — sans jamais tenir compte d'un `Jump` intercalé entre les
deux. Un `Jump` lève l'aiguille : le fil n'est plus continu, donc la distance
entre le point cousu juste avant le saut et celui juste après n'a AUCUN sens en
tant que « longueur de point cousu ». Or `emit_polyline` (§ *Génération de
points*) fait systématiquement suivre un `Jump` d'un `Stitch` à la position
d'atterrissage (point de « pinning », distance nulle) : la comparaison portait
donc en réalité sur la longueur du SAUT lui-même, rejouée comme un second
avertissement `point-long` trompeur en plus du `saut-long` déjà émis pour le
même saut — quasi systématique dès qu'un saut dépassait 7 mm (le seuil
`point-long`), qui est bien plus bas que le seuil `saut-long` (30 mm).

Corrigé en réinitialisant `hasPrevStitch` sur tout `Jump` : la continuité du
fil (et donc la comparaison `point-court`/`point-long`) ne traverse plus un
saut. Vérifié : `tests/unit/stitch_analysis/test_analyze.cpp` (absence de
`point-long`/`point-court` fantôme après un saut, y compris à distance
d'atterrissage nulle).

## Dans l'interface

**Analyse → Analyser le motif** (F5) remplit le dock *Analyse* ; un double-clic
sur un problème centre la vue sur sa localisation. Le cadre utilisé est le cadre
courant (100 × 100 mm par défaut).

Le bouton **Corriger** (sous la piste de correction) applique la correction du
problème sélectionné quand il en porte une ; il est désactivé tant que le
résultat est périmé. Annulable d'un Ctrl+Z (une seule entrée d'annulation).

Limitation : l'analyse **spatiale de densité** (carte de chaleur, satin trop
large/étroit dédié, alignement excessif des pénétrations) est **prévue**, non
implémentée ; les corrections automatiques ne couvrent que les constats du tableau
ci-dessus.

## Implémentation associée

- `libs/stitch_analysis/include/openstitch/stitch_analysis/analyze.hpp` —
  `Finding`, `Fix`, `FixKind`, `Severity`, `AnalysisOptions`, `canvas_hoop`.
- `libs/stitch_analysis/src/analyze.cpp` — `analyze`.
- `libs/stitch_analysis/src/project_analysis.cpp` — `analyze_project`, `canvas_hoop`.
- `apps/desktop/main_window_analysis.cpp` — panneau, bouton *Corriger*.
- `apps/desktop/main_window.cpp` — pré-export et dump de débogage.
- Tests : `tests/unit/stitch_analysis/test_analyze.cpp`,
  `test_analyze_project.cpp`, `tests/unit/desktop/test_analysis_fix.cpp`.
