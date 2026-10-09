# Auto-satin par squelette et traversées orientées : spécification révisée

Statut : **conception, aucun code**. Remplace la spécification initiale du
propriétaire après audit critique (2026-10). Le remplacement de l'ancien moteur
automatique par ce moteur est une décision prise ; ce document la précise et corrige
ce qui, dans la formulation initiale, échouerait.

Convention de sources : **[BREVET]** = présent dans le texte primaire d'un brevet
(référence donnée), **[NOTRE]** = notre conception, sans source brevet, à valider
par mesure. Aucun seuil numérique ci-dessous n'est une vérité textile : ce sont des
valeurs de départ à calibrer (voir §9).

## 0. Décisions du propriétaire (2026-10)

1. **Anneaux : cas limite traité**, pas refusé (voir §6.5).
2. **Squelette : celui qui donne la meilleure qualité.** Zhang-Suen reste pour le reste
   du logiciel (auto-numérisation, remplissage directionnel). Pour le satin, la
   qualité est décidée **par mesure** sur le corpus (§9) entre le squelette actuel et
   un axe médian calculé sur le polygone (diagramme de Voronoï de segments en
   coordonnées entières, aucun bruit de pixel). Hypothèse de travail : l'axe sur
   polygone gagne sur les problèmes 7, 8 et 16 ; à confirmer avant de l'adopter. Si
   retenu, il ajoute une dépendance (Boost.Polygon, licence BSL-1.0, compatible
   Apache-2.0, aucune GPL) à valider explicitement à ce moment.
3. **`satin_planning` est supprimé** (planificateur et SGSD, mort en production), à la
   fin de la migration seulement, après que les nouveaux tests existent.
4. Toujours ouvertes (proposées, non encore validées) : défaut perpendiculaire par
   branche ; nouvelle alternative de `StitchParams` dérivée du vecteur source avec
   montée de schéma 4 → 5.

## 1. Constat structurant

Les « deux rails » de l'ancien moteur étaient déjà les extrémités de cordes
perpendiculaires à la tangente du squelette (`cross_section`,
`libs/auto_satin/src/satin_column.cpp:92`). L'appariement (`ladder_correspondence`)
ne servait qu'à interpoler les fils entre deux barreaux. Le nouvel algorithme
**reprend donc la même primitive de mesure**.

- **Disparaissent** avec lui : appariement, rails inversés ou croisés, barreaux non
  triés, ajustement paramétrique, partition et décomposition SGSD, mosaïque
  satin + tatami.
- **Persistent** (ils viennent de la géométrie, de la topologie ou de la
  discrétisation, pas de la construction) : jonctions, coudes, pointes, bruit du
  squelette en pixels, trous, encoches. Le nouveau moteur doit donc les traiter
  explicitement ; il ne les résout pas par construction.

## 2. Registre des problèmes et solutions

Légende des colonnes « Sort » : V = disparaît avec la corde directe ; P = persiste
et doit être traité ; M = le symptôme change de forme.

| # | Problème | Cause racine | Sort | Solution retenue | Test d'acceptation |
|---|---|---|---|---|---|
| 1 | **Angle absolu 0° par défaut sur squelette complexe** | sur une branche parallèle à `g`, `d = h·|sin(g−α)| → 0` : toutes les cordes sont la même droite, couverture latérale **nulle** ; échoue sur la moitié des glyphes (traits horizontaux de E, T, H, L) | P | défaut **perpendiculaire par branche** + harmonisation aux nœuds (§5) ; l'angle absolu n'existe que sur choix explicite de l'utilisateur et, s'il est parallèle à une branche, bascule en mode rangées (§6.4) | bande horizontale avec `g` absolu = 0° : couverture ≥ plancher ; plus aucune corde de longueur > 3× la largeur |
| 2 | **Corde qui quitte sa branche près d'un nœud** (9,2 mm mesurés sur une bande de 5 mm) | l'intervalle contenant `P_i` traverse la branche voisine | P | **fermetures** : coupe de la région par les bissectrices aux jonctions, chaque corde est écrêtée à la cellule de sa branche + marge de recouvrement `ε` (§4) | formes T, Y, X, croix, H : aucune corde ne sort de sa cellule ; recouvrement entre cellules ≤ `ε` |
| 3 | **Coude à 90° (« E », tronc en « ] »)** : une corde perpendiculaire à la colonne court le long d'un bras de 18 mm | `P` dans l'épaisseur du bras ; `Lmax`/`y` ne corrigent pas : ils empilent une dizaine de longs fils | P | un coude (nœud de degré 2, virage ≥ `θ_coude`, rayon de courbure < rayon inscrit) est traité comme **jonction de degré 2** : coupe en onglet par la bissectrice extérieure (§4.3) | forme E, `e_trunk_isolated`, L : aucune corde > `2,5·r(P)` ; couverture ≥ plancher |
| 4 | **Cordes voisines qui se croisent côté intérieur d'un virage** | condition de non-croisement `|g'|·d_côté < |sin(g−α)|` ; pour une perpendiculaire `κ·d_int < 1` | P (M pour l'ancien cas 88°/68,5°) | (i) pas piloté par le bord le plus écarté (§6.2) ; (ii) déclencheur de **points courts** existants (`SatinShortStitch`, test du rapport avance intérieure/extérieure < 0,55) ; (iii) plafond `|g'|` sur l'interpolation des guides (§7) ; (iv) diagnostic « croisement » non bloquant | courbe en S et épingle (`test_satin_pairing_metrics`) : `degenerate_interval_count == 0` hors zones signalées |
| 5 | **Virage net = éventail de droites concourantes** | l'échantillonnage par abscisse curviligne ne peut pas représenter un arc nul avec un angle non nul | P | **échantillons de pure rotation** à chaque sommet de la polyligne lissée : `k = ⌈Δα·d_ext/ρ⌉` cordes autour du pivot (≈ 6,5° pour `d_ext = 3,5 mm`, `ρ = 0,4 mm`) | coin vif : l'espacement au bord extérieur ≤ `ρ·1,1` |
| 6 | **Pas `h` sur l'axe ≠ espacement au bord** | `d(t) = h·|sin(α−g) + t·g'|` ; sur une perpendiculaire en courbe, rapport extérieur/intérieur `(1+κ·d_ext)/(1−κ·d_int)` (3× pour `R = 2·d`) | P | `h(s) = ρ / max(|σ−a·g'|, |σ+b·g'|)`, avec `σ = sin(α−g)`, `a, b` étendues de la corde de chaque côté de `P` ; borné `[h_min, h_max]` (§6.2). Mesure inspirée de l'esprit de US6390005B1 (espacement mesuré au bord) mais définie par nous | cercle de rayon `R` : espacement au bord extérieur constant à ±5 % |
| 7 | **Longueur d'arc d'un squelette pixelisé biaisée** (jusqu'à +8 %), et lissage de Chaikin qui la réduit | marches de Zhang-Suen en `1/√2` | P | reparamétrer par longueur d'arc du squelette **lissé** (fenêtre gaussienne en µm, pas en pixels) ; fenêtre de tangente `w_t ≫ h` (≥ 4 pixels, ≥ 0,5 mm) | squelette d'un rectangle incliné : longueur d'arc à ±1 % de la vraie |
| 8 | **Tangente bruitée** (±2° → 90 µm à 2,5 mm du squelette, 20 % de `ρ`) | différence centrée sur quelques pixels | P | tangente sur fenêtre `w_t`, indépendante de `h` ; angle `g` filtré, queue des stations ignorée aux pointes | squelette bruité synthétique : écart de `g` ≤ 1° |
| 9 | **Bouts ouverts raccourcis** (capsule 38,8 mm sur 45) | Zhang-Suen érode depuis le bord : le squelette finit environ `r` avant l'extrémité | P | **prolongement des échantillons le long de la tangente** tant que `P` est intérieur ; plus de `extend_tip` ni de bissection (§6.1) | capsule, rectangle : couverture des bouts ≥ plancher |
| 10 | **Pointes effilées (trident)** et plancher de pointe sous le pas DST | `tip_min_width` 50 µm < pas DST 0,1 mm : 178 colonnes sur 190 avec un barreau < 0,8 mm ; le refus des cordes étroites avait fait échouer le trident | P | **ne pas refuser** les cordes de pointe pour leur étroitesse ; plancher = pas DST ; longueur minimale de fil 0,3 mm (raccourcissement de pointe accepté) | trident à `pixel_size` 0,05 et 0,1 mm : même verdict ; aucune corde < 0,3 mm émise |
| 11 | **Corde passant près d'un bord, hors région (raster ≠ polygone vectorisé)** | squelette à 50 µm contre polygone | P | tolérance `τ` : projeter `P` à l'intérieur si l'écart ≤ `τ`, sinon abandonner l'échantillon **avec diagnostic** | région vectorisée bruitée : aucun échantillon perdu en silence |
| 12 | **Droite passant par un sommet** | parité fausse sans règle | P | règle demi-ouverte pour la parité des intersections ; tri déterministe des intersections | forme à sommets sur les cordes : nombre d'intervalles pair |
| 13 | **Plusieurs intervalles (concavité, encoche, trou)** | seul l'intervalle contenant `P` est couvert ; les autres ne rencontrent aucun échantillon | P | couverture partielle **assumée et mesurée** : les zones non couvertes sont rapportées (diagnostic), jamais reconnectées en silence | notch, pinch, deux trous : liste des zones non couvertes exacte |
| 14 | **Doublons de cordes** (deux échantillons sur la même corde quand `g` est constant) | | P | dédoublonnage (tri par niveau `c = n_g·x` puis intervalle) | `g` constant : aucune paire de cordes confondues |
| 15 | **Perte silencieuse de couverture** (le pont du H jamais essayé ; branche rejetée sans point) | rejet structurel non propagé | P | chaque arête du graphe élagué produit une cellule et des cordes **ou** un diagnostic de refus nommé ; test d'invariant « somme des aires de cellules = aire de la région » | H, comb : invariant vrai |
| 16 | **Graphe du squelette instable** : amas de pixels de jonction, croix lue en degré 2, branche perdue, escalier de 2 pixels | squelette pixelisé et tracé glouton | P | on garde `build_skeleton_graph`/`prune_graph` mais on **durcit le contrat** : contraction des nœuds de continuité, test de reconstruction d'invariants (degrés, aire) | corpus de formes à `pixel_size` ∈ {0,05 ; 0,1 ; 0,2} : mêmes classes de cellules |
| 17 | **Définition de « simple » vs « complexe » instable** | `junction_count` change avec la taille de pixel et l'élagage ; un squelette « simple » peut échouer (tronc du E, `multi_neck`) | P | **la distinction disparaît** : tout le monde reçoit la règle par branche (§5) ; plus d'interrupteur de région. L'élongation est corrigée (rectangle 4×10,89 mm lu 1,72 « quasi circulaire ») avant d'être utilisée en éligibilité | deux formes presque identiques → même régime |
| 18 | **Guides : identifiants de branche instables** | `SkeletonEdge.id` renuméroté par tri, taille de pixel, élagage, édition du contour | P | guide = **point du repère du modèle + angle**, projeté sur le squelette à la génération (§7) ; jamais d'identifiant persisté | édition de contour : un guide reste attaché à la même zone |
| 19 | **Guides : `g` non mono-valué à un nœud** | chaque branche impose son angle : éventail de cordes qui se coupent | P | **guides de nœud** partagés ; sinon la règle EP0761860 (§5) fixe `g` au nœud | T, Y : cordes des trois branches cohérentes au nœud |
| 20 | **Interpolation modulo 180° ambiguë à exactement 90° d'écart** | deux arcs équivalents | P | interpoler `δ(s) = g(s) − (α(s)+π/2)` dans le domaine de l'angle double ; égalité → rotation positive (§7) | guides à 10° et 170° : transition courte de 20° ; 0° et 90° : sens déterministe |
| 21 | **Angle absolu sur une branche courbe** : `sin(g−α)` varie sans contrôle | | P | stocker `δ` avec drapeau « absolu » ; si `|sin(g−α)| < s_min`, le mode rangées (§6.4) remplace les cordes | branche courbe avec guide absolu : pas de corde > `Lmax_dur` |
| 22 | **Couverture au nœud : carré `w×w` doublé (T), jusqu'à 4× (croix)** | cordes de deux branches se recouvrant | P | cellules disjointes + marge `ε` ; l'onglet des bissectrices répartit le carré (§4) | T, croix : recouvrement ≤ `ε·longueur` |
| 23 | **Mosaïque satin + pièces de tatami** (satin seul 21,3 % de couverture, deux régions à 46,6 % et 59,3 % sans repli) | éligibilité lâche (`2A/P ≤ max`), couverture partielle acceptée | V si un objet par région et « tout ou rien » | **un objet par région** ; en auto-numérisation : satin entier ou repli entier, rejet tracé ; en conversion explicite par l'utilisateur : zones non couvertes affichées (§8) | image réelle (`tentabrode.png`) : jamais de mosaïque |
| 24 | **Anneau, disque, deux trous** | topologie annulaire ou axe réduit à un point | P | **anneau : cas limite traité** (§6.5) ; disque et blobs compacts : tatami ou directionnel avec refus tracé ; plusieurs trous : cycles ordinaires entre jonctions (§4), cas pur réservé à la V2 | `ring` : couverture ≥ plancher, couture sans saut ; `disc_15mm`, `two_holes` : verdict explicite, pas de crash |
| 25 | **Résidus naturels aux coins et noyaux de jonction** (rectangle 0,98995 ; y/t/cross/h/trident à 85,8–88,7 %) | les cordes ne couvrent pas les coins | P | l'onglet de §4 réduit le résidu ; le reste est mesuré (planchers du corpus) et affiché | planchers de `test_coverage_regression` conservés ou relevés |
| 26 | **Coût : squelette recalculé à chaque `effective_sequence`** (aucun cache) | | P | cache par empreinte de la géométrie source (`SkeletonCacheScope` existe) ; mesure de performance dans les tests | 100 régénérations d'un même objet : un seul calcul de squelette |
| 27 | **Orientation A/B incohérente** si `g` croise `α` | l'émission suppose `A` du même côté | P | `A` = extrémité du côté `−n_ref` (normale lissée du squelette) ; plancher `|sin(g−α)| ≥ s_min` interdit l'inversion | S-courbe avec guide : jamais d'inversion de côté |
| 28 | **Fractionnement `Lmax` / `y`** | deux paramètres distincts | — | si `L ≤ Lmax` : conservé ; sinon `N = ⌈L/y⌉` segments égaux de longueur `L/N ≤ y` ; `y ≤ Lmax` imposé ; décalage déterministe des pénétrations intermédiaires (réutilise `SatinSplit` Staggered/Jitter) | `L=12, Lmax=7, y=4` → 3 segments de 4 mm |
| 29 | **Échantillonnage régulier qui rate les extrémités** (`P_i = S(i·h)`) | le dernier pas ne tombe pas sur le bout | — | `N = ⌈L_tot/h_eff⌉`, pas effectif `L_tot/N`, échantillons en `s=0` et `s=L_tot` | longueur d'arc quelconque : premier et dernier échantillons aux extrémités |
| 30 | **Latent, hors moteur : les rails satin ne suivent pas leur source** (`Translate/ScaleVectorObjectCommand` ne déplacent que les guides du directionnel) | | P | le nouvel objet est **dérivé de son vecteur source** : il suit par construction | translation/échelle : séquence identique à une régénération |

## 3. Pipeline révisé

Entrée : région `Ω` (vecteur source), paramètres `ρ, Lmax, y, mode, guides`.
Sortie : stations `{A_i, B_i, drapeaux}` par branche, vers `finish_satin_stations`.

1. **Squelette** (`analyze_region`, Zhang-Suen, `SkeletonGraph`, `prune_graph`) ;
   lissage en µm et reparamétrage par longueur d'arc (problèmes 7, 8, 16).
2. **Fermetures** : jonctions et coudes → coupes de `Ω` → une cellule par arête (§4).
3. **Orientation** `g(s)` par arête : perpendiculaire par défaut, harmonisée aux
   nœuds, modifiée par les guides (§5, §7).
4. **Échantillonnage** : pas `h(s)` piloté par le bord, échantillons de pure rotation
   aux sommets, prolongement aux bouts (§6).
5. **Cordes** : droite orientée par l'échantillon, intervalle contenant `P`, écrêté
   à la cellule + marge (§6.3).
6. **Fractionnement `Lmax/y`**, décalage déterministe (problème 28).
7. **Diagnostics** non bloquants (§8) ; **stations** par branche avec `jump_before`.
8. **Finitions existantes** (`finish_satin_stations` : sous-couches, compensation,
   points courts, verrous, routage) : aucune dépendance aux rails.

## 4. Fermetures : coupe de la région en cellules

Reprend la structure d'EP0761860B1 (lignes de division aux branchements,
revendications 3 à 5) ; les paramètres de rotation et l'onglet de coude sont
[NOTRE].

### 4.1 Jonction (degré ≥ 3)

- Au nœud `J`, ordonner les tangentes sortantes des arêtes incidentes (fenêtre `w_t`).
- Pour chaque paire consécutive, tirer la **bissectrice angulaire** [BREVET : ligne de
  division = bissectrice de l'espace entre branches, rev. 3] jusqu'au premier contact.
- Si le premier contact n'est pas le contour (autre ligne de division ou arête du
  squelette) : **pivoter** la ligne autour de `J` jusqu'à ce que le premier contact
  soit le contour [BREVET rev. 5, figure 7]. Pas de rotation 0,5°, vers la branche de
  plus petit indice ; amplitude maximale = demi-ouverture ; au-delà, diagnostic et
  coupe à la distance du bord du disque maximal [NOTRE].
- Couper `Ω` par ces segments (`geometry::cut_path_set` existe) ; chaque morceau est
  affecté à l'arête dont il contient le milieu.

### 4.2 Marge de recouvrement

Chaque cellule est étendue de `ε` (≈ 0,3 à 0,4 mm, à calibrer) au-delà des lignes de
division ([NOTRE] ; même principe que le recouvrement de jonction existant). Aucun
interstice, un léger double-point.

### 4.3 Coude

Nœud de degré 2 dont le virage lissé dépasse `θ_coude` (valeur de départ 35°) sur une
longueur < 2·rayon inscrit : traiter comme une jonction de degré 2 [NOTRE], avec une
**coupe en onglet** (bissectrice extérieure). Une corde ne peut alors plus courir le
long du bras voisin (problème 3).

### 4.4 Invariant

Somme des aires des cellules = aire de `Ω` (à la tolérance de découpe près). Tout écart
est un défaut et un diagnostic.

## 5. Fonction d'orientation `g(s)`

- **Par arête, défaut** : `g(s) = α(s) + π/2` [NOTRE]. Il n'y a plus de régime
  « squelette complexe à 0° » : voir problème 1.
- **Extrémité libre** : perpendiculaire à la tangente [BREVET : EP0761860B1 rev. 7,
  tolérance d'environ ±10° sans importance, [0022]].
- **Nœud** : `g` au nœud pour l'arête `e` = direction du segment joignant les deux
  points où les deux lignes de division bordant la cellule `e` touchent le contour
  [BREVET : rev. 8, orientation parallèle à la droite joignant les deux
  intersections près du branchement].
- **Entre les deux extrémités** de l'arête : interpolation de `δ(s)` (§7). Le brevet
  décrit « interpoler » sans préciser la forme ; la linéarité et le domaine de
  l'angle double sont [NOTRE].
- **Mode absolu** (réglage explicite) : voir §6.4.

## 6. Échantillonnage et cordes

### 6.1 Extrémités

Prolonger le squelette de chaque extrémité libre le long de la tangente tant que le
point reste intérieur à `Ω` (à `τ` près). Pas de `extend_tip` ni de bissection.

### 6.2 Pas adaptatif

Notation : `σ = sin(α−g)`, `g' = dg/ds`, `a, b` étendues de la corde de chaque côté de
`P` (`t ∈ [−a, b]` le long de `u = (cos g, sin g)`).

- Espacement perpendiculaire à distance `t` : `d(t) = h·|σ + t·g'|`. [Dérivé]
- Intersection de deux droites voisines : `t* = sin(g−α)/g'` (= `1/κ` pour la
  perpendiculaire). Non-croisement dans la région : `|g'|·d_côté < |sin(g−α)|`.
  [Dérivé]
- **Pas retenu** : `h(s) = ρ / max(|σ−a·g'|, |σ+b·g'|)`, borné à
  `[h_min, h_max]` (`h_min` = 0,1 mm, pas DST ; `h_max` = 3·ρ) : l'espacement au bord
  **le plus écarté** vaut `ρ` (aucun trou), le côté intérieur est plus dense et traité
  par les points courts. Intégration explicite d'Euler, déterministe. [NOTRE]
- Pas de pure rotation : `k = ⌈Δα·d_ext/ρ⌉` cordes par sommet (problème 5).
- Trois grandeurs distinctes, jamais confondues : abscisse du squelette `s`,
  espacement entre traversées `ρ`, longueur d'un point `L`.

### 6.3 Corde

1. Droite `D(t) = P + t·u(g(s))`.
2. Intersections avec la **cellule étendue** (§4.2) ; règle demi-ouverte aux sommets.
3. Intervalle contenant `P` ; si `P` est hors cellule de moins de `τ`, projeter.
4. `A` = extrémité du côté `−n_ref`, `B` l'autre ; plancher `|sin(g−α)| ≥ s_min`.
5. Longueur minimale de fil 0,3 mm ; autre cas → diagnostic.
6. Garde de rayon (diagnostic non bloquant) : `max(a,b) > 2,5·r(P)`.

### 6.4 Mode absolu et rangées

Si l'utilisateur impose un angle absolu et que `|sin(g−α)| < s_min` sur une branche, les
cordes à travers le squelette ne couvrent pas la branche (problème 1). Dans ce cas la
branche est couverte en **rangées** : niveaux `c = n_g·x` espacés de `ρ`, découpés par
la cellule. Pour `g ∥ α` on retrouve un remplissage en rangées le long de la branche ;
pour `g ⟂ α` on retrouve §6.3. [NOTRE ; proposé par l'analyse]

### 6.5 Anneau (cas limite)

Squelette réduit à un **cycle sans nœud** (région avec un trou, sans branche) :

- Point de départ canonique et déterministe : sommet du cycle de plus petit `x`, puis
  plus petit `y`. Sens de parcours fixé (anti-horaire).
- `s ∈ [0, L_tot)` périodique ; `g = α + π/2` est périodique, donc continu à la couture.
- Dernière corde = première corde (aucun doublon), avec le recouvrement `ε` à la
  couture ; pas de saut ni de coupe de fil sur le tour.
- Chaque droite coupe la région en deux intervalles (bande extérieure et opposée) :
  l'intervalle **contenant `P`** est retenu, donc la bande locale.
- Cycles entre jonctions (anneau avec branches, plusieurs trous) : arêtes ordinaires
  traitées par §4 ; seule l'arête sans nœud demande ce traitement.
- Le pas §6.2 s'applique tel quel (`κ` constante = `1/R`).

## 7. Guides

- Modèle : `Guide = { p ∈ ℝ²_µm (repère du modèle), θ, relatif | absolu }`.
- À la génération : projeter `p` sur l'arête la plus proche (distance maximale
  `τ_g` ; au-delà, guide **orphelin**, rapporté, ignoré). Dans le rayon du nœud, le
  guide est un **guide de nœud** partagé par toutes les arêtes incidentes.
- Interpolation dans le domaine de l'angle double (`2δ`), plus court arc ; égalité
  exacte → rotation positive.
- Avant le premier guide / après le dernier : valeur du guide le plus proche. Un seul
  guide : constant sur l'arête.
- Plafond `|g'| ≤ |sin(g−α)|/d_côté_max` : si dépassé, la transition est étalée sur
  l'arc minimal nécessaire, avec diagnostic (problème 4).
- Aucun identifiant de branche n'est persisté (problème 18).

## 8. Diagnostics (non bloquants)

Longueurs de fils (min, max, hors `[0,3 ; Lmax_dur]`), espacement au bord extérieur et
côté intérieur (rapport), variation d'angle, concentration de pénétrations, croisements
de cordes voisines, zones non couvertes (liste de polygones), superpositions,
déplacements et sauts, gardes de rayon, guides orphelins, refus de nœud. Aucun seuil
textile n'est imposé avant essai machine. Les diagnostics sont calculés à partir des
**fils** (le `satin_coverage` actuel ne note que des colonnes à rails : à étendre
ou à doubler d'un analyseur sur stations).

Politique d'usage :

- **Conversion explicite par l'utilisateur** : le résultat est toujours produit, les
  zones non couvertes sont affichées.
- **Auto-numérisation** : satin entier ou repli entier ; un rejet porte un code de
  raison ; jamais de mosaïque satin + tatami.

## 9. Calibration et validation

Les seuils de départ (`ε`, `τ`, `θ_coude`, `s_min`, `h_max`, rayon de garde, longueur
minimale de fil) sont des hypothèses. Méthode : corpus de formes synthétiques
(`make_shape`, ≈ 33) et de géométries réelles rejouées telles quelles (lettre T du logo
GISTRE, cas de fils réels à 88° et 68,5°, bordure du logo circulaire, région à boucle,
`tentabrode.png`), découpage par image pour la validation, planchers de couverture
gelés avec marge, référence « ancien moteur » reconstruite dans un worktree jetable
depuis `18de427^`. La validation physique reste à faire sur machine ; une inspection
SVG ou un test numérique ne la remplace pas.

## 10. Hors périmètre de la première version, et questions ouvertes

- Disques et formes compactes : refus explicite, tatami ou directionnel (problème 24).
  Le satin tournant par anneaux de l'ancien code disparaît avec lui ; si un besoin de
  satin tournant subsiste, il sera traité séparément (HP-STI-018). Plusieurs trous avec
  cycles sans nœud : V2.
- Axe médian sur polygone (décision §0.2) : lot dédié avec comparaison chiffrée
  (stabilité aux tailles de pixel, erreur de tangente, nombre de branches parasites)
  face au squelette actuel, avant toute adoption.
- Valeur de `θ_coude`, de `ε` et du pas de rotation des lignes de division : à calibrer.
- Nombre de cordes de pure rotation aux sommets très aigus : plafond à fixer.

## 11. Sources

- EP0761860B1 (texte complet lu) : lignes de division bissectrices, rotation en cas de
  collision (rev. 3 à 5, fig. 7), orientation perpendiculaire en extrémité (rev. 7),
  parallèle à la corde des intersections au branchement (rev. 8), interpolation décrite
  en mots seulement.
- US6390005B1 (texte complet lu) : espacement mesuré par la distance perpendiculaire du
  nouveau point à la droite du précédent ; aucune formule d'inset générale.
- Audit d'échecs du code : `docs/source/satin.md` (références `S:ligne` dans le rapport
  d'audit), `git show 18de427`, corpus de formes et de fixtures cités en §9.
