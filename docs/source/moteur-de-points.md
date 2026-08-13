# Moteur de génération de points

Public : développeur, utilisateur avancé.

> État : Présent dans le code : oui · Tests unitaires : oui (running/tatami/
> satin/routage/retouches) · Tests visuels : partiels (SVG de diagnostic,
> golden) · Import/export DST : oui · Test sur machine réelle : **non** ·
> **Statut recommandé : hétérogène** — la fondation commune (`stitch`,
> l'orchestration de `generate_sequence`, le contrat `effective_sequence`) est
> solide et testée ; le point droit est implémenté ; le tatami est partiel ; le
> satin reste expérimental (voir *Colonne satin* pour le détail). Ce chapitre
> documente le **mécanisme**, pas la qualité du résultat cousu — voir
> *Limitations* pour la synthèse par capacité.

Ce chapitre est la référence unifiée du moteur qui transforme un
`document::Project` en `std::vector<stitch::StitchCommand>` exportable en DST.
Il couvre l'intégralité de `libs/stitch_generation/` (module `stitch_generation`,
cf. *Référence des modules*) et son vocabulaire commun dans `libs/stitch/`. Les
chapitres *Génération de points — point droit et fondations*, *Remplissage
tatami* et *Colonne satin* restent les références narratives (audits,
historique des correctifs, chiffres mesurés) pour chaque type de point ; ce
chapitre les cite plutôt que de les dupliquer, et se concentre sur ce qu'aucun
des trois ne couvre en un seul endroit : le contrat d'entrée
(`generate_sequence` vs `effective_sequence`), le vocabulaire commun des
commandes/passes, l'algorithme exact qui transforme des rails + barreaux en
points cousus (Lots 2 à 6 du satin, jamais rassemblés ailleurs), et la place de
la génération dans le pipeline complet (retouches, ordre, export).

## 1. Vue d'ensemble : de l'objet à l'export

```
document::Project
  └─ optimization::optimize_order()      réordonne project.embroidery_objects
       (document, AVANT génération)       (commande annulable ReorderEmbroideryCommand)
  └─ stitch_generation::generate_sequence(project)   -- séquence BRUTE
       └─ pour chaque EmbroideryObject visible, dans l'ordre du document :
            RunningStitchParams -> generate_running (run_stitch + apply_repeats)
            TatamiParams        -> generate_tatami  (tatami_underlay + fill_tatami)
            SatinParams         -> generate_satin ou generate_satin_group (routage)
       └‑ ColorChange entre deux objets de couleurs différentes, End final
  └─ stitch_generation::apply_manual_overrides(sequence, project)  -- retouches Lot 8
  = stitch_generation::effective_sequence(project)   -- SEUL point d'entrée de production
       └─ apps/desktop : aperçu (renderStitches), simulation, statistiques (stitch::compute_stats)
       └─ formats::write_dst_file  -- export DST (voir *Format DST*)
```

Deux fonctions pures cohabitent, et la distinction est le contrat le plus
important du module :

- **`generate_sequence(project)`** (`generate.hpp`/`generate.cpp`) parcourt le
  document et produit la séquence **brute** : uniquement ce que la géométrie
  actuelle des objets dicte, sans aucune retouche manuelle.
- **`apply_manual_overrides(sequence, project)`** (`overrides.hpp`/`overrides.cpp`)
  patche **en place** une séquence déjà générée avec les retouches
  (`EmbroideryObject::overrides`, Lot 8/ADR-014 — déplacement de point,
  Stitch↔Jump, coupe de fil) des objets dans l'état `ManuallyEdited`.
- **`effective_sequence(project)`** enchaîne les deux, dans cet ordre, et rien
  d'autre — sa signature est volontairement identique à `generate_sequence`
  pour qu'un appelant existant n'ait qu'à substituer l'appel.

### Le contrat : personne ne contourne les retouches

CLAUDE.md et le code sont explicites : **tout consommateur de production**
(aperçu desktop, export DST, simulation, statistiques, CLI) doit appeler
`effective_sequence`, jamais `generate_sequence` seul — sans quoi les retouches
manuelles d'un utilisateur disparaîtraient silencieusement de l'aperçu ou de
l'export. Ce contrat n'est pas qu'une convention documentée : il est **imposé
structurellement** par `tests/check_no_raw_sequence_bypass.cmake`, un script
CMake exécuté comme test CTest qui :

1. parcourt tous les `.cpp`/`.hpp` de `apps/` et `libs/` ;
2. ignore `libs/stitch_generation/` (implémentation interne légitime :
   `effective_sequence` doit bien appeler `generate_sequence`) et `tests/`
   (fixtures synthétiques, jamais un projet utilisateur réel) ;
3. échoue avec le fichier:ligne exact si un site d'appel restant invoque
   `generate_sequence(` sans qu'un commentaire `raw-sequence-ok: <raison>`
   apparaisse sur la même ligne ou la ligne précédente.

Deux sites — et seulement deux, à ce jour — portent cette annotation, tous
deux dans `apps/cli/main.cpp` : `run_stitchdebug` (ligne 181, "Projet
synthétique construit ici même, jamais chargé/sauvegardé, aucune retouche
manuelle possible") et le générateur de debug du routage satin (ligne 534,
même justification). Les deux construisent un `document::Project` synthétique
en mémoire, uniquement pour visualiser un algorithme sur une forme de
référence — un projet qui, par construction, ne peut porter aucun
`StitchOverride`. C'est la seule catégorie d'exception prévue par le
mécanisme : un vrai projet utilisateur (chargé d'un `.osp`, édité dans
l'interface) ne doit jamais transiter par `generate_sequence` seul.

## 2. Vocabulaire commun (`libs/stitch/`)

`stitch::StitchCommand` (`libs/stitch/include/openstitch/stitch/sequence.hpp`)
est l'unité atomique de toute séquence, qu'elle soit générée, importée d'un DST
ou retouchée :

```cpp
struct StitchCommand {
    Vec2um pos;
    CommandType type;   // Stitch, Jump, Trim, ColorChange, Stop, End
    ObjectId source;     // 0 = manuel/importé
    StitchPass pass;      // Underlay, TopStitch, Travel, Lock, Manual
};
```

`CommandType` est la sémantique **machine** (ce qui finit dans le DST, cf.
*Format DST*) ; `StitchPass` est une métadonnée **logique**, ajoutée par ce
moteur et **jamais sérialisée** (la séquence est toujours régénérée, ADR-014 —
le DST l'ignore complètement). Elle permet d'afficher, filtrer et analyser les
sous-couches indépendamment de la couche supérieure :

| Passe | Origine | Exemples |
|---|---|---|
| `Underlay` | sous-couches, émises **avant** la couche supérieure | `tatami_underlay`, sous-couches satin (center/edge/zigzag) |
| `TopStitch` | couche visible finale | running stitch, `fill_tatami` hors sauts, zigzag satin principal |
| `Travel` | déplacement — cousu et caché, ou saut | pénétrations intermédiaires d'un underpath (tatami §15, satin routé Lot 6), et le `Jump` de tête de chaque polyligne (`emit_polyline`) |
| `Lock` | point de fixation | `lock_stitches` (satin, Lot 5) |
| `Manual` | modifié par une retouche | `apply_manual_overrides` : position déplacée ou type forcé |

Point précis souvent mal lu : `Travel` couvre **deux réalités opposées** —
un `Jump` (aiguille levée, aucune couture) *et* une pénétration `Stitch`
cousue et cachée sous la couche supérieure (underpath). Distinguer les deux
exige de regarder `CommandType`, pas seulement `StitchPass` : un point
`{Travel, Jump}` est un vrai saut, un point `{Travel, Stitch}` est cousu.
L'outil de diagnostic desktop (`MainWindow::buildDebugDump`, menu contextuel
*Diagnostic*) imprime justement `pass=<Passe> type=<Type> pos=<x,y>` pour
chaque commande d'un objet, ainsi qu'un histogramme par passe
(`Underlay=… TopStitch=… Travel=… Lock=… Manual=…`) — c'est la source du
vocabulaire `pass=Underlay`/`pass=TopStitch`/… utilisé informellement ailleurs
dans le projet.

### `stitch::compute_stats`

`libs/stitch/src/stats.cpp` dérive `StitchStats` (points, sauts, coupes,
changements de couleur, longueur de fil, boîte englobante) en un seul passage
séquentiel :

- `stitches`/`jumps`/`trims`/`color_changes` : compteurs directs par
  `CommandType`.
- `thread_length_um` : somme de `length_um(cmd.pos - prev)`, ajoutée
  **uniquement** sur une commande `Stitch`, où `prev` est la position de la
  commande précédente **quel que soit son type** (`Stitch` ou `Jump` — `prev`
  est mis à jour dans les deux cas). Un saut ne contribue donc jamais
  directement à `thread_length_um`, mais la première `Stitch` qui le suit
  ajoute la distance depuis le point d'atterrissage du saut. En pratique cette
  distance est **nulle** : `emit_polyline` saute vers `points.front()` puis
  coud `points[0]`, qui **est** `points.front()` — le saut et la première
  couture partagent exactement la même position. `thread_length_um` mesure
  donc bien la longueur de fil réellement cousu, jamais la distance des sauts
  eux-mêmes, mais ce n'est vrai que parce que chaque émetteur respecte cette
  convention (aucune garantie structurelle au niveau de `compute_stats` lui-même).
- `bounds` : boîte englobante des positions `Stitch` **uniquement** — un saut
  loin du motif (retour au point d'origine avant un `Trim`, par exemple)
  n'élargit pas le cadre rapporté.

## 3. Orchestration : `generate_sequence`

`generate_sequence` (`libs/stitch_generation/src/generate.cpp`) itère sur
`project.embroidery_objects` dans l'ordre du document (l'ordre qu'*Ordre de
couture* — §7 ci-dessous — a éventuellement déjà réarrangé en amont, au niveau
du document, pas ici) :

1. les objets `!visible` sont ignorés (index avancé, rien émis) ;
2. pour un type non-satin, l'objet vectoriel source (`source_vector`) est
   recherché dans `project.vector_objects` ; absent, `generate_sequence`
   renvoie une erreur `ErrorCategory::Internal` nommant l'objet — la seule
   voie d'échec de la génération en dehors d'un document totalement vide ;
3. un `ColorChange` est inséré si l'objet précédent existait et avait une
   couleur RGB différente ;
4. **routage satin (§6.5)** : si l'objet est un satin *routable*
   (`is_routable_satin` — satin dont `SatinParams::rungs.size() >= 2`, donc
   issu de l'auto-satin ou d'un satin manuel avec barreaux par défaut), le
   moteur cherche le plus long groupe **contigu** d'objets satin routables
   suivants qui partagent **exactement** la même couleur (`rgb`) et le même
   `source_vector`. Un groupe de taille ≥ 2 part dans
   `generate_satin_group` (§6.5) ; un groupe de taille 1 (satin routable
   isolé, ou dernier de sa série) suit le chemin normal `generate_satin`. Un
   satin manuel/legacy sans barreaux n'est jamais groupé, quelle que soit sa
   couleur ;
5. sinon, `std::visit` sur `object.params` dispatch vers `generate_running`,
   `generate_tatami` ou `generate_satin` (§4, §5, §6).

Une fois tous les objets traités, si la séquence reste vide (aucun objet
visible n'a rien produit — cas trivial : document vide, ou uniquement des
objets masqués), l'erreur `ErrorCategory::UserInput` "Aucun objet de broderie
visible : rien à générer" est renvoyée. Sinon, une commande `End` est ajoutée
à la position de la dernière commande.

### Les trois émetteurs de polylignes (vocabulaire interne de `generate.cpp`)

Trois fonctions statiques traduisent une géométrie déjà calculée en commandes,
partagées par les trois générateurs :

- **`emit_polyline(seq, points, source, pass)`** : un `Jump` vers
  `points.front()`, puis chaque point en `Stitch`. Le saut est **omis** si la
  toute dernière commande est déjà un `Stitch` à la même position (enchaînement
  direct depuis une passe précédente du même objet — sous-couche → lock →
  satin) ; un saut est toujours émis après un `ColorChange`, un autre `Jump`,
  ou entre deux objets différents, même à position identique.
- **`emit_polyline_with_breaks(seq, points, jump_before, source, pass)`** :
  identique, mais certains points internes (indices listés dans
  `jump_before`, triés) sont eux aussi atteints par un `Jump` plutôt
  qu'enchaînés — c'est le mécanisme qui matérialise
  `SatinResult::jump_before` (§6.4) dans la séquence machine.
- **`emit_fill(seq, fill, source)`** : consomme un `std::vector<FillStitch>`
  (tatami, §5) — `fs.jump` devient `Jump`/`Travel`, `fs.travel` devient
  `Stitch`/`Travel` (underpath caché), sinon `Stitch`/`TopStitch`. Le tout
  premier point est toujours un `Jump`, même si `fs.jump` vaut `false`
  (`!started` force la branche `Jump`).

## 4. Point droit (running stitch) — synthèse

Couvert en détail par *Génération de points — point droit et fondations* ;
voici la synthèse tendue nécessaire pour situer ce point dans le moteur, avec
deux corrections vérifiées contre le code actuel.

`stitch_generation::run_stitch(path, RunningConfig)`
(`libs/stitch_generation/src/running_stitch.cpp`) : aplatit la Bézier
(`geometry::flatten`, tolérance `flatten_tolerance` = 100 µm par défaut) →
détecte les coins vifs (angle de rotation > `corner_threshold`, ≈ 0,6108652 rad
= 35° par défaut) → découpe en tronçons entre coins → ré-échantillonne
**chaque tronçon** par longueur d'arc (`geometry::resample_run`, dans
`libs/geometry/src/polyline.cpp`). `generate_running` (`generate.cpp`) appelle
`sample_path` (délègue à `run_stitch`, un seul passage) puis `apply_repeats`
pour chaque contour extérieur et chaque trou de l'objet vectoriel source.

**Correction apportée à *Génération de points…*** : ce chapitre présente le
comportement "cible traitée comme un maximum" (`ceil(L/cible)` plutôt que
`round`) comme *"un point d'ambiguïté à clarifier dans une future version
(distinguer `target_length` de `maximum_length`)"*. À la lecture du code
actuel, cette clarification a déjà eu lieu **partiellement** : le champ
s'appelle désormais `RunningConfig::target_length` (plus `stitch_length`), et
des champs `min_length`/`max_length` distincts existent bel et bien. Mais le
choix `ceil` reste, et le commentaire à côté de `geometry::resample_run`
(`libs/geometry/src/polyline.cpp`) le qualifie explicitement d'**assumé**,
pas d'ouvert : *"Écart assumé (...) ; `ceil` garantit en plus le respect
strict de la longueur maximale."* Le comportement observable — les points ne
dépassent jamais `target_length` mais peuvent être plus courts — est donc un
choix de conception délibéré et documenté dans le code, pas une ambiguïté
résiduelle ; seul le nom du champ dans la doc narrative (`stitch_length`) est
à mettre à jour vers `target_length`.

`apply_repeat_mode` (`RepeatMode::SinglePass`/`BackAndForth`/`BeanStitch`/
`Backstitch`) et son alias de compatibilité `apply_repeats(points, int)`
restent inchangés par rapport à la description existante.

## 5. Remplissage tatami — synthèse

Couvert en détail par *Remplissage tatami* ; synthèse tendue, vérifiée sans
divergence notable contre `libs/stitch_generation/src/tatami.cpp`.

`fill_tatami(region, TatamiParams)` : rotation virtuelle de `-angle` → balayage
de rangées horizontales espacées de `row_spacing` (400 µm par défaut) →
intersections pair-impair par rangée → pénétrations sur une grille de pas
`stitch_length` (3 mm par défaut), déphasée d'une rangée à l'autre selon
`stagger` (2 par défaut) → les segments de rangées voisines qui se chevauchent
en x sont les arêtes d'un graphe, parcouru par un algorithme glouton
(`current` → voisin non visité le plus proche, sinon saut vers le segment non
visité le plus proche) → rotation inverse. Chaque liaison cousue (dans une
rangée ou entre rangées) est validée géométriquement par
`connector_invalid` : le segment est découpé à **chaque** intersection
paramétrique avec une arête du contour ou d'un trou (y compris un contact
dégénéré par un sommet), et le milieu de chaque sous-segment doit rester dans
la région (`in_region`, qui traite un point exactement sur une frontière comme
intérieur, tolérance ~0,01 µm) — sans quoi la liaison devient un saut. Ce
mécanisme est ce qui garantit qu'aucune couture ne traverse un trou.

Tatami avancé (Lot 7, tout désactivé par défaut) : `tatami_underlay` produit
un contour rentré (`underlay_edge`, retrait `underlay_inset` = 600 µm — jamais
sur les bords de trous, politique sûre si le retrait échoue) et des rangées
perpendiculaires espacées (`underlay_parallel`, pas `underlay_spacing` =
2 mm) ; `hidden_underpath` remplace un saut par un trajet cousu caché (passe
`Travel`) — direct s'il reste court (`seglen(prev, rp) <= underpathCap`, avec
`underpathCap = max(6·row_spacing, 8 mm)`), sinon le long du contour extérieur
rentré (`routeHighway`, qui essaie les deux sens de parcours du contour et
garde le plus court sous le même plafond), toujours revalidé par
`connector_invalid` segment par segment.

Aucune divergence factuelle trouvée entre *Remplissage tatami* et le code
actuel — la chaîne d'implémentation `fill_tatami`/`tatami_underlay`/
`connector_invalid`/`in_region` correspond exactement à ce que ce chapitre
décrit, chiffres compris.

## 6. Colonne satin : des rails aux points cousus (Lots 2 à 6)

*Colonne satin* documente en détail l'**appariement géométrique**
(`ladder_correspondence`, l'algorithme "ladder" qui remplace l'ancienne
correspondance par fraction d'abscisse) et tout le moteur `auto_satin` qui
produit des rails et des barreaux à partir d'un squelette. Ce chapitre-ci ne
reprend pas cette histoire — il documente précisément ce qui se passe **une
fois que `SatinParams` (rails + barreaux) est connu**, jusqu'aux commandes
machine : c'est le contenu des Lots 2 à 6 de `libs/stitch_generation/src/satin.cpp`,
`lock.cpp` et `routing.cpp`, jamais rassemblé en un seul endroit ailleurs dans
la documentation.

### 6.1. Deux chemins de génération, un seul point d'entrée

`generate_satin` (`generate.cpp`) construit un `SatinConfig` à partir de
`document::SatinParams` (copie champ à champ des Lots 3/4, cf. tableau §6.6)
puis choisit :

- **`fill_satin_columns(rail_a, rail_b, rungs, config)`** si
  `params.rungs.size() >= 2` — c'est le chemin qui implémente **l'intégralité**
  de `SatinConfig` (terminaisons, split, sous-couches de bord/zigzag,
  compensation push/pull asymétrique). Depuis `default_rungs` (voir *Colonne
  satin* §*Barreaux par défaut*), c'est le chemin emprunté par la quasi-
  totalité des satins de l'application, y compris manuels.
- **`fill_satin(rail_a, rail_b, config)`** sinon (satin legacy sans barreaux,
  compatibilité ascendante uniquement) — n'implémente qu'un **sous-ensemble** :
  densité, compensation pull **symétrique**, sous-couche centrale. Tout autre
  réglage exposé par l'inspecteur (terminaisons, split, sous-couches de bord/
  zigzag, push, compensation asymétrique) reste silencieusement **sans effet**
  sur ce chemin — limitation déjà documentée dans *Colonne satin*, reconfirmée
  ici en lisant `fill_satin` : sa boucle d'émission ne référence ni
  `cap_start`/`cap_end`, ni `split_stitch`, ni `underlay_edge`/`underlay_zigzag`,
  ni `pull_left`/`pull_right`/`push_start`/`push_end`.

### 6.2. `satin_stations` : la correspondance structurelle

`fill_satin_columns` délègue d'abord à `satin_stations(rail_a, rail_b, rungs,
density)` (exposée publiquement dans `satin.hpp`, réutilisée par
`satin_coverage`) : aplatit les deux rails (`geometry::flatten`, tolérance
`kRailFlattenTolerance` = 30 µm — sous la résolution DST de 100 µm, donc sans
perte perceptible), réoriente `rail_b` si les rails sont fournis tête-bêche
(`opposite_orientation`), projette chaque barreau sur les deux rails
(abscisses curvilignes), **trie** les barreaux par abscisse projetée cumulée
(`sa + sb`) — l'ordre du vecteur d'entrée n'est jamais signifiant — puis
**fusionne** deux barreaux consécutifs après tri dont la progression sur les
deux rails est inférieure à la moitié du pas de densité (`anchorMinGap =
density / 2`), pour éliminer tout intervalle dégénéré.

Moins de deux barreaux après fusion → liste vide (l'appelant retombe sur
`fill_satin`). Sinon, pour chaque paire de barreaux consécutifs :

- si `non_ribbon_interval(a0, a1)` est vrai (§6.4), aucune station
  intermédiaire n'est produite : seul le barreau d'arrivée est ajouté, marqué
  `jump_before = true` ;
- sinon, `ladder_correspondence` puis `resample_by_medial_spacing` (voir
  *Colonne satin* pour l'algorithme complet) produisent les stations
  intermédiaires à l'espacement `density` (400 µm par défaut), mesuré sur la
  **ligne médiane** — donc approximativement perpendiculaire au fil, y compris
  en section inclinée ou courbe, jamais mesuré le long d'un rail.

### 6.3. `fill_satin_columns` : ordre exact des transformations

Une fois les stations connues, chacune devient un `Thread{a, b, anchor,
jump_before}`. Les transformations s'enchaînent **dans cet ordre précis** —
ordre qui a des conséquences directes sur le résultat (voir la note sur les
sous-couches ci-dessous) :

1. **Terminaisons** (`cap_start`/`cap_end`, `SatinCapType`, §6.6) : pour
   chaque fil `i`, `f = min(cap_factor(cap_start, i, cap_length),
   cap_factor(cap_end, nThreads-1-i, cap_length))` — le **minimum** des deux
   facteurs, pour qu'une colonne trop courte pour loger les deux terminaisons
   sans chevauchement reste cohérente (jamais deux rétrécissements
   indépendants qui se contrediraient). Si `f < 1`, `a` et `b` sont chacun
   ramenés vers le milieu du fil par interpolation (`lerp(milieu, point, f)`).
   `cap_factor` : `Flat` **et `Automatic`** renvoient tous deux `1.0` (aucun
   rétrécissement) — la branche `default:` du `switch` couvre les deux ; en
   l'état actuel du code, **`SatinCapType::Automatic` ne se comporte pas
   différemment de `Flat`**, malgré son nom qui suggère un choix adaptatif.
   C'est une lacune non documentée ailleurs : l'inspecteur propose les deux
   options sans qu'aucune différence de résultat ne soit produite. `Tapered`
   suit une rampe linéaire de `kMin` (0,18 — jamais 0, pour ne pas empiler sur
   une coordonnée unique) à 1 ; `Rounded` suit un quart de sinusoïde.
2. **Points courts** (`short_stitch`, §6.6), si activé : pour chaque fil
   `i ≥ 1` (jamais sur un barreau, `t.anchor` protégé), compare l'avance de
   chaque rail depuis le fil précédent (`advA`, `advB`) ; si le ratio
   `min/max < short_stitch_curvature` (0,55 par défaut) et que l'avance
   minimale est sous `short_stitch_min_gap` (250 µm), le virage est jugé
   "serré". `RemoveAndRedistribute` marque le fil `dropped` un fil sur deux
   (jamais deux d'affilée) ; `SingleInset`/`MultiLevelInset` rentrent le point
   du rail intérieur vers le milieu d'une fraction `short_stitch_inset`
   (0,35 par défaut), triangulaire sur `short_stitch_levels` (2 par défaut)
   niveaux en mode `MultiLevelInset`.
3. **Push** (`push_start`/`push_end`, §6.6) : `shiftEnd` étend/rétracte le
   premier et le dernier fil le long de l'axe vers leur voisin immédiat. Une
   rétraction (`amount < 0`) est bornée à `-(n-1)` (`n` = distance au voisin) :
   elle ne peut jamais dépasser la station voisine, ce qui éviterait
   l'auto-croisement documenté dans *Colonne satin* §*Sous-couches et
   compensation*. Une extension (`amount > 0`) reste **non bornée**.
4. **Milieux** (`mids`) et longueur cumulée médiane (`cumMid`) sont calculés
   **après** les trois étapes précédentes — donc sur la géométrie déjà
   terminée/rentrée/étendue.
5. **Sous-couches** (`center_underlay`, `underlay_edge`, `underlay_zigzag`,
   §6.6), dans l'ordre center → edge → zigzag, chacune une passe `Underlay`
   distincte : la sous-couche centrale est ré-échantillonnée à
   `underlay_spacing` (2 mm par défaut, **pas** à `density`, correctif détaillé
   dans *Colonne satin* §*Défaut trouvé en usage réel*) via `point_at` sur
   `mids`/`cumMid`. L'edge walk décale chaque point vers l'intérieur de
   `min(underlay_edge_inset, demi-largeur × 0,9)`. Le zigzag prend un fil sur
   `stride = round(underlay_zigzag_spacing / density)` (indices, pas longueur
   d'arc — approximation valable car les fils sont déjà espacés
   régulièrement par construction, sauf immédiatement après un `jump_before`).
6. **Couche supérieure** : pour chaque fil non `dropped`, si `jump_before`,
   l'indice courant est ajouté à `result.jump_before` (§6.4) ; **la
   compensation pull latérale est calculée ici, pas avant** — donc les
   sous-couches (étape 5) voient la largeur **avant** compensation, la couche
   supérieure après. `split_stitch` (§6.6) fractionne toute traversée
   `a→b` plus longue que `max_stitch_length` (7 mm par défaut).

### 6.4. Rupture de ruban : `non_ribbon_interval` et `jump_before`

Entre deux barreaux consécutifs, `non_ribbon_interval(a0, a1, b0, b1)`
(`libs/stitch_generation/src/satin.cpp`) compare la **direction d'avance** de
chaque rail (`a1.pa - a0.pa` vs `a1.pb - a0.pb`), pas leur largeur : si les
deux avances dépassent `kJumpMinSpan` (800 µm — sous ce plancher, la variation
angulaire n'est pas significative) et que l'angle entre les deux directions
dépasse `kJumpDegPerMm` (10°/mm) **multiplié par la distance moyenne
parcourue en mm**, la colonne cesse de se comporter comme un ruban sur cet
intervalle. Le seuil est un **rapport angle/distance**, pas un angle brut — le
détail de calibration (deux défauts réels distincts, un seuil brut se
révélant incapable de distinguer un virage légitime étalé sur ~14 mm d'un
défaut concentré sur 1,5-3,7 mm) est documenté dans *Colonne satin* §*Seuil
angle/distance plutôt qu'angle brut*.

Quand c'est le cas, `satin_stations` n'interpole **aucune** station
intermédiaire sur cet intervalle et marque le barreau d'arrivée
`jump_before = true`. `fill_satin_columns` reporte cet indice dans
`SatinResult::jump_before`. `generate_satin` transmet ce vecteur à
`emit_polyline_with_breaks` (§3), qui insère un `Jump` avant ces points
précis plutôt que d'enchaîner un `Stitch` continu — le fil est levé, pas
forcé à traverser la forme en diagonale.

**Réorientation et `jump_before`** : quand `generate_satin` inverse la
colonne (§6.6, entrée/sortie), les indices de `jump_before` doivent être
recalculés — un index `i` dans la séquence d'origine correspond à `n - i`
après inversion (`n` = taille totale), car c'est l'**arête** (pas le point)
qui doit rester marquée comme sautée, et l'arête `(i-1, i)` devient `(n-i,
n-i-1)` après inversion. Le code recalcule chaque indice puis retrie le
vecteur (`generate.cpp`, fonction `generate_satin`) — sans ce retri, l'ordre
croissant qu'`emit_polyline_with_breaks` suppose serait rompu.

### 6.5. Points de fixation (`lock_stitches`, Lot 5)

`lock_stitches(anchor, toward, type, length, passes)`
(`libs/stitch_generation/src/lock.cpp`) construit une polyligne courte ancrée
en `anchor`, orientée vers `toward` (le point voisin — pour un lock de
colonne satin, c'est le point du **rail opposé**, donc la largeur locale de la
colonne à cet endroit, pas un point lointain le long de la couture). La
longueur effective `L` est bornée : `L = min(length, n)` où `n = |toward -
anchor|`, sauf dans le cas dégénéré `anchor == toward` où `L = length` sans
autre référence géométrique — correctif détaillé dans *Colonne satin*
§*Entrée/sortie et points de fixation*, qui évitait qu'un lock pique hors de
la matière déjà cousue sur une colonne plus fine que `lock_length` (défaut
0,8 mm). Trois types, chacun répété `passes` fois (2 par défaut) :

- **`BackAndForth`** : `anchor → P(L,0) → anchor`, répété.
- **`Triangle`** : `anchor → P(L,0) → P(L·0,5, L·0,6) → anchor`, répété — un
  petit triangle d'ancrage.
- **`MicroZigzag`** : `anchor` puis `2·passes` points alternant de côté
  (`P(L·0,4·⌈k/2⌉, ±L·0,5)`), progressant le long de l'axe, puis retour à
  `anchor`.

`generate_satin` émet, dans cet ordre : sous-couches → lock de début
(passe `Lock`, entre `result.satin.front()` et `result.satin[1]`, si
`lock_start != None`) → couche supérieure (avec ses éventuels `jump_before`)
→ lock de fin (entre `result.satin[n-1]` et `result.satin[n-2]`, si
`lock_end != None`). Un seul lock par extrémité, jamais par sous-passe.

### 6.6. Entrée/sortie, et tableau des paramètres du satin

Si `entry_point` et/ou `exit_point` sont définis, `generate_satin` compare le
coût "sens normal" (`|entry - satin.front()| + |exit - satin.back()|`) au coût
"sens inversé" (`|entry - satin.back()| + |exit - satin.front()|`) ; le sens
inversé l'emporte strictement en cas d'égalité non stricte du côté normal
(`reversed < normal`). Inverser la séquence exige de recalculer
`jump_before` (§6.4). Sans point d'entrée ni de sortie, l'orientation issue du
générateur (celle des rails tels qu'orientés dans le document) est conservée
telle quelle.

| Champ (`SatinConfig`) | Défaut | Rôle |
|---|---|---|
| `density` | 400 µm | pas d'avancement le long de la colonne, mesuré sur la ligne médiane |
| `pull_compensation` | 0 | compensation pull symétrique (les deux chemins) |
| `center_underlay` | `false` (`SatinConfig`) / `true` (`document::SatinParams`, §9) | sous-couche centrale |
| `underlay_spacing` | 2 mm | espacement de la sous-couche centrale (§6.3 point 5) |
| `short_stitch` | `Disabled` | mode de traitement des virages serrés |
| `short_stitch_curvature` | 0,55 | ratio avance intérieure/extérieure en dessous duquel un virage est "serré" |
| `short_stitch_min_gap` | 250 µm | avance intérieure minimale avant d'agir |
| `short_stitch_inset` | 0,35 | profondeur d'inset (fraction de demi-largeur) |
| `short_stitch_levels` | 2 | nombre de niveaux en mode `MultiLevelInset` |
| `split_stitch` | `Disabled` | fractionnement des traversées longues |
| `max_stitch_length` | 7 mm | seuil de fractionnement |
| `cap_start`/`cap_end` | `Flat` | terminaisons (voir §6.3 pt 1 — `Automatic` ≡ `Flat` actuellement) |
| `cap_length` | 4 | nombre de fils sur lequel s'étale la terminaison |
| `underlay_edge`/`underlay_zigzag` | `false` | sous-couches additionnelles |
| `underlay_edge_inset` | 600 µm | retrait de l'edge walk par rapport aux rails |
| `underlay_end_retract` | 600 µm | retrait aux extrémités (center/edge) |
| `underlay_zigzag_spacing` | 1,5 mm | espacement (approximatif, en indices) du zigzag de sous-couche |
| `underlay_zigzag_width` | 0,65 | fraction de la largeur locale pour le zigzag de sous-couche |
| `pull_left`/`pull_right` | 0 | compensation latérale fixe, par côté (asymétrique) |
| `pull_left_prop`/`pull_right_prop` | 0,0 | compensation proportionnelle à la largeur locale |
| `pull_max` | 5 mm | borne de l'offset latéral total par côté |
| `push_start`/`push_end` | 0 | extension/rétraction longitudinale aux extrémités (rétraction bornée, §6.3 pt 3) |
| `lock_start`/`lock_end` | `None` | type de point de fixation |
| `lock_length` | 800 µm | longueur nominale (bornée à la distance réelle, §6.5) |
| `lock_passes` | 2 | répétitions du motif de lock |

### 6.7. Routage multi-colonnes (`route_columns`, Lot 6)

Quand `generate_sequence` regroupe plusieurs colonnes satin routables
contiguës de même couleur/source (§3 point 4), `generate_satin_group`
(`generate.cpp`) construit un `RouteColumn{id, start, end, start_junction,
end_junction}` par objet — `start`/`end` viennent de `column_endpoints(params)`
(milieux des barreaux d'about, **avec le même décalage `push_start`/`push_end`
que celui réellement appliqué par `fill_satin_columns`**, même formule de
borne : reproduire un point différent de celui effectivement cousu fausserait
la décision de routage, cf. le correctif détaillé dans *Colonne satin*
§*Routage multi-colonnes*) — et `start_junction`/`end_junction` viennent de
`SatinParams::topology` (`SatinSectionTopology::start_junction`/`end_junction`,
un identifiant de jonction **local au réseau porté par `source_vector`**,
absent pour une extrémité libre ou une colonne indépendante/legacy).

`route_columns(columns, origin, RoutingConfig)`
(`libs/stitch_generation/src/routing.cpp`) :

1. **Ordre glouton** (`greedy_order`) : depuis la position courante, choisit
   l'extrémité libre la plus proche — mais une extrémité qui **partage une
   jonction** avec la sortie courante (même id, distance ≤ `underpath_max`)
   l'emporte systématiquement sur une simple proximité, même plus courte.
2. **Orientation optimale** (`best_orientation`) : pour l'ordre retenu,
   programmation dynamique sur les deux orientations possibles de chaque
   colonne (`std::array<OrientationCost, 2>`) — maximise d'abord le nombre de
   liaisons validées par une jonction commune, minimise ensuite la distance
   totale parcourue. Déterministe, résolu **exactement** (pas une heuristique).
3. **Amélioration 2-opt** (si `config.two_opt`, activé par défaut) : inversion
   de sous-segments de l'ordre tant que le coût diminue, avec une marge
   anti-oscillation de 1 µm, bornée à 64 itérations de balayage complet.
4. Pour chaque étape, le **type de liaison** (`ConnectorKind`) avec la colonne
   précédente est décidé par la distance entre le point de sortie précédent et
   le point d'entrée courant :
   - une **jonction commune validée** (même id des deux côtés, ET distance ≤
     `underpath_max` — 8 mm par défaut) autorise un `Underpath` jusqu'à cette
     même borne de 8 mm ;
   - en son **absence**, seul un quasi-contact sous `underpath_max_without_junction`
     (1,5 mm par défaut, nettement plus strict) autorise un `Underpath` ;
   - au-delà de la borne applicable, la liaison devient un `Jump` (avec coupe
     implicite — `ConnectorKind::Jump` porte déjà cette sémantique dans sa
     propre définition, il n'existe pas de "trim" distinct à ce niveau).

`ConnectorKind::Start` marque uniquement la toute première colonne du groupe
(simple pose du fil, jamais de décision Underpath/Jump). Le vocabulaire
diffère légèrement de celui employé par la spécification SGSD
(`libs/satin_planning/`, voir *Colonne satin* §*Phase 9*) — "travel run" y
désigne ce que ce module appelle `Underpath` — mais recouvre exactement le
même mécanisme ; SGSD ne réimplémente aucune logique de routage, il construit
des `RouteColumn` et délègue à ce même `route_columns`.

**Émission** (`generate_satin_group`) : pour chaque étape du plan, la colonne
est générée normalement (`generate_satin` dans une séquence temporaire, avec
`entry_point`/`exit_point` imposés par `step.reversed`) puis :

- si `step.connector == Underpath` et qu'une commande précédente existe déjà,
  un segment `[position courante → première pénétration de la colonne]` est
  échantillonné par `sample_path` (cible 2,5 mm, minimum 500 µm), et
  **seuls les points intermédiaires** (ni le premier ni le dernier, déjà
  couverts respectivement par la position courante et la colonne elle-même)
  sont ajoutés en `Stitch`/`Travel` — puis la colonne est enchaînée **sans**
  son `Jump` de tête (`tmp.commands[1..]`) ;
- sinon (début de groupe, ou liaison trop longue), la colonne est ajoutée
  telle quelle, `Jump` de tête compris.

## 7. Ordre de couture et export — où ce moteur s'arrête

`optimization::optimize_order` (`libs/optimization/`, voir *Algorithmes*
§*Ordre de couture*) n'est **pas** un post-traitement de la séquence de
points : il réordonne la **liste d'objets du document**
(`project.embroidery_objects`, via la commande annulable
`ReorderEmbroideryCommand`, `MainWindow::applyOrderStrategy`), les objets
verrouillés (`EmbroideryObject::locked`) restant à leur position. La
génération de points est ensuite **relancée** sur ce nouvel ordre — il n'existe
aucun mécanisme qui réordonnerait un `stitch::StitchSequence` déjà produit.
Concrètement, dans le desktop, `applyOrderStrategy` exécute la commande de
réordonnancement puis appelle `refreshImage()`, qui appelle
`stitch_generation::refresh_context` (donc `effective_sequence`,
§1) sur le projet déjà réordonné.

L'export DST (`formats::write_dst_file`, voir *Format DST*) consomme
directement la séquence effective ainsi obtenue (`MainWindow::exportDst`,
membre `sequence_` rempli par `refreshImage`) — la génération de points est
donc strictement **en amont** de l'ordre optimisé (qui agit sur le document
avant régénération) et **en amont** de l'export (qui ne transforme plus la
séquence).

## 8. Retouches manuelles : ce que `apply_manual_overrides` touche exactement

Détaillé pour l'utilisateur dans *Retouche des points et de la géométrie* ;
ce qui suit est le contrat exact côté moteur, nécessaire pour comprendre
pourquoi `effective_sequence` est incontournable (§1).

Une retouche (`document::StitchOverride{base_index, moved_to, forced_type,
trim_after}`) cible un index dans la **vue brute** d'un objet
(`raw_slice(sequence, object.id)` — toutes les commandes de la séquence brute
dont `source == object.id`, dans l'ordre de production, **toutes passes
confondues**, qu'elles soient contiguës ou entrelacées comme dans un satin
routé). Seules les entrées de passe `TopStitch` sont éligibles
(`is_topstitch_entry`) — les sous-couches, trajets cachés et locks restent
entièrement régénérés, jamais retouchables :

- `moved_to` exige en plus une cible `Stitch` (`is_movable_point`) — déplacer
  un saut n'a pas de sens, sa position est déterminée par ses deux voisins de
  trajet ;
- `forced_type` (bascule `Stitch`↔`Jump`, bidirectionnelle) et `trim_after`
  acceptent une cible `Stitch` **ou** `Jump`.

Chaque champ d'un `StitchOverride` est validé **indépendamment** : un champ
invalide pour sa cible est ignoré silencieusement, sans rejeter les autres
champs du même override. Une commande modifiée passe en passe `Manual`. Les
insertions de `Trim` sont différées et appliquées après coup, triées par
index décroissant, pour qu'aucune insertion ne décale un index restant à
traiter.

`classify_edit_state(object, raw)` compare la taille et l'empreinte FNV-1a
64 bits (`fingerprint`, sérialisation little-endian explicite de position/
type/passe/source de chaque commande — reproductible bit à bit,
indépendante de l'architecture hôte) de la vue brute **actuelle** contre
`edited_point_count`/`edited_fingerprint` enregistrés lors de la dernière
retouche réussie : identiques → `ManuallyEdited` (les retouches s'appliquent) ;
différents → `Dirty` (la géométrie source a changé depuis, les retouches ne
sont **plus jamais réappliquées** tant que l'utilisateur ne les abandonne pas
explicitement) ; `overrides` vide → `Clean`. Rien n'est mis en cache : cet
état est recalculé à chaque appel depuis le document et la séquence brute
actuelle.

`edit_view`, `classify_all_edit_states` et `refresh_context` sont des
compositions de ces mêmes primitives pour l'UI desktop (indicateurs
Clean/ManuallyEdited/Dirty, poignées d'édition) — `refresh_context` en
particulier consolide un **seul** appel interne à `generate_sequence` pour
produire à la fois la séquence effective, les états par objet et la vue
d'édition d'une cible, plutôt que jusqu'à trois régénérations indépendantes
par rafraîchissement (`MainWindow::refreshImage`).

## 9. Limites connues (au niveau du moteur, au-delà de ce que documentent déjà satin.md/tatami.md)

- **`SatinCapType::Automatic` ne fait rien de différent de `Flat`** dans
  `cap_factor` (§6.3 pt 1) — la terminaison "automatique" promise par
  l'inspecteur n'a aucune implémentation propre à ce jour.
- **`center_underlay` a deux défauts différents** selon le niveau regardé :
  `false` dans `SatinConfig` (le type interne au moteur), `true` dans
  `document::SatinParams` (le type exposé par l'inspecteur) — sans
  conséquence pratique puisque le document fournit toujours une valeur
  explicite, mais un piège pour quiconque lit `SatinConfig` isolément en
  pensant y trouver le comportement par défaut réel d'un satin créé dans
  l'application.
- **Le zigzag de sous-couche satin** (`underlay_zigzag`) espace ses points par
  un **stride en nombre de fils** (`round(underlay_zigzag_spacing / density)`),
  pas par une distance réellement mesurée le long de la médiane — une
  approximation raisonnable tant que les fils restent régulièrement espacés,
  mais fausse localement juste après un `jump_before` (§6.4), où l'espacement
  réel entre deux fils consécutifs peut s'écarter fortement de `density`.
- **Le trajet caché d'un routage satin (`Underpath`) reste un segment direct
  échantillonné**, jamais un suivi du centre d'une colonne déjà cousue — la
  garantie de rester "sous la broderie" vient uniquement de la borne de
  distance (jonction validée ou quasi-contact), pas d'un vrai calcul de
  trajet le long de la matière (limite déjà posée dans *Colonne satin* §*Routage
  multi-colonnes*, confirmée ici au niveau du code d'émission).
- **Le contrat `raw-sequence-ok`** ne protège que les appels textuellement
  détectables par le script CMake (`generate_sequence(` sur une ligne) : un
  appel indirect via un alias de fonction ou un pointeur de fonction ne
  serait pas détecté. Aucun cas de ce genre n'existe actuellement dans le
  dépôt (vérifié par lecture des deux seuls sites annotés, §1), mais ce n'est
  pas une garantie statique complète, seulement une convention outillée.

## Implémentation associée

- `libs/stitch/include/openstitch/stitch/sequence.hpp` — `StitchCommand`,
  `CommandType`, `StitchPass`, `StitchSequence`, `StitchStats`.
- `libs/stitch/src/stats.cpp` — `compute_stats`.
- `libs/stitch_generation/include/openstitch/stitch_generation/generate.hpp` +
  `src/generate.cpp` — `generate_sequence` (orchestration, dispatch,
  détection de groupe satin routable), `emit_polyline`,
  `emit_polyline_with_breaks`, `emit_fill`, `generate_running`,
  `generate_tatami`, `generate_satin`, `generate_satin_group`,
  `column_endpoints`, `is_routable_satin`.
- `libs/stitch_generation/include/openstitch/stitch_generation/overrides.hpp` +
  `src/overrides.cpp` — `effective_sequence`, `apply_manual_overrides`,
  `raw_slice`, `fingerprint`, `classify_edit_state`, `is_movable_point`,
  `edit_view`, `classify_all_edit_states`, `refresh_context` (Lot 8/ADR-014).
- `libs/stitch_generation/include/openstitch/stitch_generation/running_stitch.hpp`
  + `src/running_stitch.cpp` — `run_stitch`, `sample_path`,
  `apply_repeat_mode`, `apply_repeats`, `RunningConfig`, `RunningResult`.
- `libs/stitch_generation/include/openstitch/stitch_generation/tatami.hpp` +
  `src/tatami.cpp` — `fill_tatami`, `tatami_underlay`,
  `segment_stays_in_region`, `connector_invalid`, `in_region`.
- `libs/stitch_generation/include/openstitch/stitch_generation/satin.hpp` +
  `src/satin.cpp` — `SatinConfig`, `SatinResult`, `SatinStation`,
  `satin_stations`, `fill_satin`, `fill_satin_columns`, `default_rungs`,
  `rails_from_contour`, `non_ribbon_interval`, `cap_factor`,
  `ladder_correspondence`, `resample_by_medial_spacing` (voir *Colonne satin*
  pour l'algorithme d'appariement).
- `libs/stitch_generation/include/openstitch/stitch_generation/lock.hpp` +
  `src/lock.cpp` — `lock_stitches`, `LockType`.
- `libs/stitch_generation/include/openstitch/stitch_generation/routing.hpp` +
  `src/routing.cpp` — `route_columns`, `RoutePlan`, `RouteColumn`,
  `RouteStep`, `ConnectorKind`, `RoutingConfig`, `greedy_order`,
  `best_orientation`.
- `libs/stitch_generation/src/satin_guides.cpp` — édition interactive des
  guides/barreaux (`satin_guide_junctions`, `move_satin_guide_group`, etc.),
  hors périmètre de la génération elle-même.
- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `RunningStitchParams`, `TatamiParams`, `SatinParams`, `SatinRung`,
  `SatinSectionTopology`, `SatinLock`, `StitchOverride`, `StitchPointType`,
  `EmbroideryObject`.
- `libs/optimization/include/openstitch/optimization/order.hpp` +
  `src/order.cpp` — `optimize_order`, `OrderStrategy`, `compute_cost`
  (réordonnancement des objets du document, en amont de la génération).
- `apps/desktop/main_window.cpp` — `refreshImage` (appel à
  `refresh_context`), `applyOrderStrategy` (réordonnancement puis
  régénération), `exportDst`, `buildDebugDump`/`showDebugDump` (dump
  `pass=…  type=…  pos=…` par commande, histogramme par passe).
- `apps/cli/main.cpp` — `run_stitchdebug`, générateur de debug du routage
  satin : les deux seuls sites annotés `raw-sequence-ok:` du dépôt.
- `tests/check_no_raw_sequence_bypass.cmake` — garde structurelle CTest du
  contrat `effective_sequence`.
- Tests : `tests/unit/stitch/test_generate.cpp`, `test_overrides.cpp`,
  `test_running_stitch.cpp`, `test_tatami.cpp`, `test_satin.cpp`,
  `test_satin_pairing_metrics.cpp`, `test_satin_guides.cpp`,
  `test_routing.cpp`, `test_stats.cpp`.
- Chapitres associés : *Génération de points — point droit et fondations*,
  *Remplissage tatami*, *Colonne satin*, *Retouche des points et de la
  géométrie*, *Format DST*, *Algorithmes*, *Objets de broderie*.
