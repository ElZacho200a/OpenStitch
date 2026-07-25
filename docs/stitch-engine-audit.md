# Audit du moteur de génération de points

Date : 2026-07-19. Périmètre : `libs/stitch_generation`, `libs/stitch`,
`libs/geometry`, `libs/document` (paramètres), export DST (`libs/formats`).

Cet audit précède toute correction (§40 étape 0). Il documente l'existant,
répond aux questions imposées, puis liste les défauts avec, pour chacun : un
exemple minimal, la cause probable, le module, la gravité, la stratégie de
correction et un test de non-régression.

## 1. Fichiers analysés

| Fichier | Rôle actuel |
|---|---|
| `libs/stitch_generation/src/running_stitch.cpp` | `sample_path` (échantillonnage) et `apply_repeats` (double/triple) |
| `libs/stitch_generation/src/tatami.cpp` | `fill_tatami` (scanline) |
| `libs/stitch_generation/src/satin.cpp` | `fill_satin`, `rails_from_contour` |
| `libs/stitch_generation/src/generate.cpp` | assemblage de la séquence (jump/colorchange/end) |
| `libs/stitch/include/openstitch/stitch/sequence.hpp` | `StitchCommand`, `StitchSequence`, `compute_stats` |
| `libs/geometry/*` | `Path`/`PathSet`/`PathNode`, `simplify`, `clean`, `offset` |
| `libs/document/.../embroidery_object.hpp` | `RunningStitchParams`, `TatamiParams`, `SatinParams` |
| `libs/formats/src/dst.cpp` | encodeur/décodeur DST |

## 2. Réponses aux questions imposées

- **Types de points réellement implémentés** : point droit (running) ;
  double (aller-retour global) ; triple/bean ; tatami (scanline) ; satin
  (deux rails). Pas de backstitch, stem, zigzag isolé, concentrique, spirale,
  radial, guidé, motif, cross-stitch, appliqué, lock stitch dédié.
- **Algorithmes utilisés** : running = subdivision **par arête** de la
  polyligne ; tatami = scanline pair-impair + serpentin ; satin =
  ré-échantillonnage des deux rails par fraction d'abscisse curviligne.
- **Paramétrage des courbes** : les `PathNode` portent des tangentes
  optionnelles (`tan_in`/`tan_out`, type `Smooth`) mais **elles sont ignorées**
  par `sample_path`. Aucune paramétrisation par longueur d'arc d'une Bézier :
  on ne travaille que sur des polylignes déjà aplaties. → **par paramètre, pas
  par longueur d'arc** (et même : tangentes non exploitées du tout).
- **Espacements mesurés correctement ?** Non pour les courbes : la longueur de
  point effective est imposée par la **densité des sommets** de la polyligne,
  pas par la longueur cible (voir D1).
- **Trous gérés ?** Oui pour le tatami (scanline pair-impair) et pour le
  running (chaque trou est cousu comme un contour). Pas de notion de traversée
  interdite pour le running (non pertinent) ; le tatami relie les intervalles
  correctement mais la §15 impose un routage par graphe non encore présent
  (hors périmètre de cette mission).
- **Polygones concaves gérés ?** Tatami : oui (plusieurs intervalles par
  rangée). Running : suit le contour tel quel.
- **Formes invalides rejetées ou réparées ?** `clean_to_path_sets` (Clipper2)
  répare/normalise à la vectorisation. Mais `sample_path`/`fill_*` ne
  revalident pas leur entrée : une `Path` à 1 nœud renvoie un vecteur vide
  silencieux, sans avertissement.
- **Points trop longs subdivisés ?** Running : oui (`ceil(len/stitch_length)`
  par arête). Mais aucune notion de `max_length` distincte de la cible.
- **Points trop courts filtrés ?** Partiellement et **incorrectement** : les
  points intermédiaires < `min_length` sont sautés, mais les sommets de la
  polyligne sont **toujours** conservés → sur une courbe finement facettée, on
  produit une rafale de points bien plus courts que `min_length` (voir D2).
- **Déplacements classés STITCH/JUMP/TRAVEL ?** Non : seulement `Stitch` et un
  `Jump` par contour. Pas de `Travel` (déplacement cousu caché), pas de `Trim`
  généré, pas de distinction déplacement caché / saut machine.
- **Points d'entrée / sortie respectés ?** Non : aucun point d'entrée/sortie
  n'existe dans le modèle. Le running démarre au premier nœud du contour.
- **Compensation de tirage ?** Satin uniquement (écarte les rails). Running :
  aucune (normal). Tatami : `inset` (offset) mais pas de compensation
  directionnelle.
- **Sous-couches réellement générées ?** Satin : sous-couche centrale (point
  droit sur l'axe). Tatami : non (le champ existe côté paramètres mais aucune
  sous-couche n'est produite). Contour/zigzag underlay : absents.
- **Résultats déterministes ?** Oui pour running/satin/tatami (aucun aléa ;
  k-means de segmentation à graine fixe, hors moteur de points).
- **Cohérence des mm après redimensionnement ?** Les paramètres sont en µm
  absolus ; un redimensionnement de la géométrie **ne** rescale **pas** la
  longueur de point (elle reste 3 mm), ce qui est correct. Mais comme
  l'espacement réel dépend des sommets (D1), le rendu change de façon non
  intuitive après une simplification différente.
- **Une régénération redonne-t-elle le même résultat ?** Oui (fonctions
  pures du document).
- **Des modifications de points cassent-elles les objets sources ?** Non : les
  points ne sont pas stockés dans l'objet (séparation intention/points
  respectée, ADR-014). Il n'existe pas encore d'édition manuelle de points.
- **L'export DST modifie-t-il silencieusement les trajectoires ?** Il quantifie
  au pas de 0,1 mm et subdivise les déplacements > 12,1 mm en `Jump`. C'est une
  normalisation légitime, mais elle vit **dans l'encodeur** au lieu d'une passe
  de normalisation séparée (§3.4 non respecté).

## 3. Défauts identifiés

### D1 — Espacement des courbes gouverné par les sommets, pas par la longueur d'arc **(critique)**

- **Exemple minimal** : un cercle de rayon 10 mm aplati en 64 segments (arête
  ≈ 0,98 mm), `stitch_length = 3 mm`. Attendu : ~21 points espacés de ~3 mm.
  Obtenu : **64 points** espacés de ~0,98 mm, car chaque arête < 3 mm devient
  exactement un point (`ceil(0,98/3)=1`).
- **Cause** : `sample_path` boucle **par arête** et force un point à chaque
  sommet ; il n'y a pas de paramétrisation par longueur d'arc globale.
- **Module** : `running_stitch.cpp::sample_path`.
- **Gravité** : critique (rend le running inutilisable sur toute courbe).
- **Correction** : aplatir en polyligne fine, puis ré-échantillonner par
  **longueur d'arc globale** entre points caractéristiques (coins), avec
  `n = round(L/d)` par tronçon.
- **Test de non-régression** : cercle r=10 mm, `d=3 mm` → nombre de points ∈
  [20,22] et écarts consécutifs ∈ [2,7 ; 3,3] mm.

### D2 — Le filtre de points courts épargne les sommets → micro-points sur les courbes **(élevé)**

- **Exemple minimal** : polyligne de 200 sommets espacés de 0,1 mm,
  `min_length = 0,5 mm`. Attendu : points ≥ 0,5 mm. Obtenu : 200 points de
  0,1 mm (chaque sommet est « isNode » donc conservé).
- **Cause** : la condition `!isNode && fromLast < min_length` n'écarte que les
  points **intermédiaires**, jamais les sommets.
- **Module** : `running_stitch.cpp::sample_path`.
- **Gravité** : élevé (sur-densité, casse de fil, perforation).
- **Correction** : après ré-échantillonnage par longueur d'arc, ne conserver
  comme points imposés que les **vrais coins** (angle > seuil), pas tous les
  sommets d'aplatissement ; nettoyage de séquence conforme §6.3 (ne pas
  déplacer les sommets importants).
- **Test** : polyligne quasi-droite de 200 micro-segments, `d=3, min=0,5` →
  écarts consécutifs ≥ 0,5 mm partout.

### D3 — Tangentes de Bézier ignorées **(élevé)**

- **Exemple minimal** : un `Path` de 2 nœuds `Smooth` avec `tan_out`/`tan_in`
  décrivant une demi-boucle. Attendu : couture suivant la courbe. Obtenu :
  segment droit entre les deux nœuds (tangentes ignorées).
- **Cause** : `sample_path` copie `node.pos` uniquement.
- **Module** : `running_stitch.cpp` (et absence d'un aplatisseur de Bézier
  dans `geometry`).
- **Gravité** : élevé pour l'édition de nœuds ; faible tant que la
  vectorisation ne produit que des coins (cas actuel), mais bloquant pour la
  suite.
- **Correction** : aplatisseur adaptatif de Bézier cubique (tolérance de
  platitude) dans `libs/geometry`.
- **Test** : quart de cercle en Bézier vs. échantillonnage analytique →
  erreur max < tolérance ; longueur cohérente à 1 %.

### D4 — Pas de résultat structuré ni d'avertissements **(élevé)**

- **Exemple minimal** : `sample_path` sur une `Path` d'un seul nœud → renvoie
  `{}` sans dire pourquoi.
- **Cause** : signature retournant un simple `std::vector<Vec2um>`.
- **Module** : toute l'API `stitch_generation`.
- **Gravité** : élevé (§5 impose warnings/statistics/debug).
- **Correction** : type `RunningResult { points, warnings, stats }` ; jamais de
  crash, toujours un diagnostic (chemin trop court, forme vide…).
- **Test** : chaque cas d'erreur produit le code d'avertissement attendu.

### D5 — Chemins fermés : phase, sens et départ non contrôlés **(moyen)**

- **Exemple minimal** : deux régénérations d'un cercle fermé démarrent
  toujours au nœud 0 ; impossible d'imposer un point de départ ou une phase.
- **Cause** : `sample_path` part toujours de `poly.front()`.
- **Module** : `running_stitch.cpp`.
- **Gravité** : moyen (routage/qualité).
- **Correction** : options `direction`, `phase`, `start_offset` (projection
  d'un point de départ sur le chemin).
- **Test** : cercle avec `start` imposé au milieu d'un segment → premier point
  à la projection ; inversion du sens → séquence renversée.

### D6 — Double/triple : modes non distingués, non paramétrables **(moyen)**

- **Exemple minimal** : `repeats=2` ne fait qu'un aller-retour global ; aucun
  mode « double local » ni « retrace exact » ; `repeats` autre que 2/3 traité
  comme 3.
- **Cause** : `apply_repeats` code en dur deux comportements.
- **Module** : `running_stitch.cpp::apply_repeats`.
- **Gravité** : moyen.
- **Correction** : énum de mode explicite (SinglePass, BackAndForth,
  BeanStitch(n), Backstitch) ; suppression des mouvements nuls ; politique sur
  chemin fermé ; test du **nombre exact de traversées** par segment.
- **Test** : bean n=3 sur 3 points → chaque intervalle traversé exactement 3
  fois, aucun mouvement nul.

### D7 — Normalisation machine mélangée à l'encodeur DST **(moyen)**

- **Exemple minimal** : la subdivision des grands déplacements et la politique
  de trim vivent dans `dst.cpp`, pas dans une passe indépendante.
- **Cause** : absence du niveau §3.4 (normalisation machine).
- **Module** : `formats/dst.cpp` + absence de `libs/stitch` normalisation.
- **Gravité** : moyen (empêche PES/JEF de partager la même logique).
- **Correction** : passe `normalize(sequence, MachineProfile)` en amont de tout
  encodeur ; l'encodeur n'encode qu'une séquence déjà normalisée.
- **Test** : une séquence avec déplacement 50 mm normalisée → sauts ≤ 12,1 mm
  **avant** encodage ; l'encodeur ne subdivise plus rien.

### D8 — `min_length`/`max_length` incomplets, pas de types forts sémantiques **(faible)**

- **Cause** : `RunningStitchParams` n'a pas de `max_length` ; les longueurs
  sont des `Micrometers` sans distinction `StitchLength`/`StitchSpacing`.
- **Module** : `document/embroidery_object.hpp`.
- **Gravité** : faible mais structurant (§4, §30 : ne pas nommer « density »
  deux choses différentes).
- **Correction** : alias forts `StitchLength`, `StitchSpacing`,
  `MinStitchLength`, `MaxStitchLength` ; champ `max_length`.

## 4. Périmètre de correction de CETTE mission (§47)

Seuls **D1, D2, D3, D4, D5, D6** (running stitch + fondations longueur d'arc)
sont traités maintenant. D7 (normalisation machine) et la refonte satin/tatami
sont planifiés mais **hors périmètre** tant que le running stitch et les
fondations ne sont pas stables et validés numériquement.

Voir le rapport avant/après en fin de `docs/stitch-engine-research.md` et les
SVG de diagnostic sous `tests/golden/stitch-generation/`.
