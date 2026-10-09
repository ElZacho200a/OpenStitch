# Roadmap R&D complémentaire : techniques issues des brevets

Public : mainteneur, contributeurs, agent Claude. Cette roadmap **complète**
`docs/roadmap-parite-hatch.md` (qui reste la source de vérité des statuts et du
format d'entrée, §0.1). Elle porte les propositions issues de la recherche
brevets (`docs/source/patent-research.md` et ses fiches) qui n'ont pas
d'équivalent Hatch direct, ou qui précisent une entrée HP existante.

- Une entrée `RD-PAT-NNN` renvoie à l'entrée HP concernée quand il y en a une.
  Le statut `☑ Fait` exige une livraison sur `main` (règle du dépôt) : une
  branche R&D ne suffit pas.
- Les brevets sont des **sources techniques** ; toute implémentation est
  indépendante. Ce qui n'est pas dans un brevet est marqué « notre conception ».
- Statuts juridiques : préliminaires, voir la page des brevets. Aucun avis.
- Révision 2026-10 : plan corrigé après revue indépendante, vérifiée sur le code
  (commit de retrait `18de427`, `autodigitize.hpp`, `embroidery_object.hpp`,
  `satin_coverage/coverage.hpp`).

## Objectif directeur

Réintégrer un **auto-satin dans l'auto-numérisation**. L'ancien auto-satin
produisait des résultats de mauvaise qualité et a été retiré (`18de427`,
2026-10-08 ; politique actuelle : tatami/contour). Le nouveau doit être **meilleur
que le repli, mesuré**, sinon il reste désactivé.

Principe de méthode : **mesurer d'abord, coder le manque ensuite**. L'audit montre
que le moteur existant (rails + barreaux persistés, `fill_satin_columns`,
planificateur SGSD, `satin_coverage`) couvre déjà l'essentiel des traversées. Le
moteur de traversées des brevets n'est donc construit que si le corpus prouve un
manque sur une classe de formes définie.

## Innovations retenues (tri après lecture des PDF)

Critère de tri : une idée est **gardée** si elle est (a) étayée par le texte
primaire, (b) absente du code ou meilleure que lui (vérifié), (c) implémentable
indépendamment sans constante de brevet. Les seuils sont toujours calibrés chez
nous. « Existant » = vérifié dans le code.

| Innovation | Source | Existant | Décision | Entrée |
|---|---|---|---|---|
| Éligibilité satin par statistiques de distance (σ/μ, max) le long du squelette | US7016757B2 (idée), notre extension | partiel (`satinability.cpp` : largeur, variation, élongation) | **Garder** | RD-PAT-004 |
| Satin entier ou repli entier, rejet tracé (leçon du retrait) | post-mortem | absent | **Garder** | RD-PAT-002 |
| Mesure de l'espacement du côté intérieur d'une courbe (distance ⟂ au point précédent) comme **métrique** | US6390005B1 | absent comme métrique | **Garder** | RD-PAT-003 |
| Densité satin modulée par la largeur de colonne (colonnes étroites plus lâches, larges plus serrées) | US5343401A | **absent** (HP-ENG-003 « À faire ») | **Garder** | RD-PAT-013 |
| Orientation ∥ à la corde entre intersections de contour au branchement, interpolée avec ⟂ en extrémité ; découpe par bissectrices avec rotation anti-collision | EP0761860B1 | partiel (junction anchoring) | **Garder, conditionnel** | RD-PAT-001 |
| Orientation radiale pour un disque (axe réduit à un point) | EP0761860B1 [0019] | partiel (`IsoOffsetRing`) | **Garder, conditionnel** | RD-PAT-001 |
| Règle de sous-couche en trois cas (dedans / pont / exclu), liaisons par l'axe du goulot | JP3922316B2, US5823127A | à auditer | **Garder, après audit** | RD-PAT-007 |
| Ordre de couture : élagage de feuilles vers le point final ; bâti puis retour à la fourche | US5957068A, US5283747A, US8532810B2 | partiel (`route_columns`) | **Garder, après comparaison** | RD-PAT-008 |
| Critère alternatif d'angle de tatami (moins de fragments) | US8219238B2 | partiel (`auto_fill_angle`) | **Garder, optionnel** | RD-PAT-005 |
| Analyse « points entièrement recouverts » en lecture seule | US6633794B2 | absent | **Garder** | RD-PAT-006 |
| Graphes de longueur et d'angle par point (diagnostic) | US6167823B1 | absent | **Garder, P3** | RD-PAT-014 |
| Appliqué multi-passes (placement, bâti, couverture satin, arrêts, contour de coupe) | US5438520A, JP3769602B2 | absent | **Garder, hors objectif directeur** | HP-SPEC-001 |
| Remplissage concentrique / radial / elliptique avec profil de densité | US6937919B1 | absent | **Garder, P3** | RD-PAT-010 |
| Stippling par courbe de remplissage d'espace, jitter à graine fixe | US6968255B1 | absent | **Garder, P3** | HP-STI-013 |
| Import DST → objets (reconnaissance FILL/SATIN par alternance de signes) ; tableaux 1 et 2 récupérés | US6510360B1 | absent | **Différer** (hors périmètre, tableaux archivés dans la fiche) | — |

Écartées (sans valeur ajoutée démontrée) : densité sur points existants, lettrage
sur arc, numérisation manuelle, stylet, types de point rudimentaires, simplification
générique, apprentissage de jonctions, remplissage curviligne (doublon du champ de
flux), formules d'ancres de la famille Goldman (inutilisables sans plafond propre).

## RD-PAT-000 — Post-mortem de l'ancien auto-satin [P0] — ☑ Fait (analyse, à confirmer)

- État : analyse du diff de `18de427` (le message du commit est vide : **cause
  inférée du code supprimé, à confirmer par l'auteur**).
- Constat sur l'ancien flux (`classify_and_build_embroidery`) :
  - Éligibilité = largeur moyenne `2A/P ≤ satin_max_width` et aire minimale. Aucun
    critère de régularité de largeur, d'élongation, de trous ni de branches à ce
    niveau : toute forme compacte « moyennement large » était candidate.
  - La région entière partait au planificateur (SGSD), qui pouvait rendre un
    satin **partiel** (`structural_gap`) ; le reliquat était comblé par des
    pièces de tatami avec 0,4 mm de recouvrement (`kCoverageOverlap`).
  - Résultat probable : plusieurs sections satin (« section i/n ») plus des
    pièces de tatami par région, donc fragmentation, coutures satin/tatami et
    surplus de coupes/sauts. C'est ce qu'il faut éviter.
- Conséquences pour le nouveau design : (1) éligibilité **stricte** ; (2) **aucune
  couverture partielle acceptée** : satin entier ou repli entier, pas de
  mosaïque ; (3) chaque rejet porte un code de raison ; (4) comparaison
  mesurée au repli.
- Les champs `use_auto_satin` et `use_naive_satin` d'`AutoOptions` sont inertes
  (déprécié) : ne pas les ressusciter, ajouter un champ nouveau et nommé.

## Chaîne P0 : mesure puis réintégration protégée

### RD-PAT-003 — Banc de comparaison satin vs repli [P0] — ☐ À faire
- Source : exigence produit (pas un brevet). Rattaché à HP-AUTO-001/009.
- État OpenStitch : corpus synthétique de formes (`libs/auto_satin/src/shapes.cpp`),
  corpus de torture du planificateur (`tests/unit/satin_planning/
  test_torture_corpus.cpp`), planchers par forme
  (`tests/unit/auto_satin/test_coverage_regression.cpp`), goldens
  `tests/golden/auto-satin/`, métriques de séquence `stitch_analysis/
  project_metrics.cpp` (couverture raster, `uncovered_ratio`, sauts/coupes par
  type, points courts). `satin_coverage` ne sait scorer **que des colonnes
  satin** (`SatinColumnInput`) : il ne peut pas noter un tatami.
- À faire : outil CLI/test `autodigitize-compare` qui, pour chaque région (formes
  synthétiques **et régions extraites d'images réelles** par la vraie chaîne
  segmentation + vectorisation), produit la sortie satin (planificateur
  existant) et le repli, et mesure avec une métrique **neutre fondée sur la
  séquence** (`project_metrics`) : couverture, nombre de points, sauts + coupes,
  points courts, alignement fil/normale du squelette, et **espacement du côté
  intérieur d'une courbe** (distance perpendiculaire de l'extrémité de chaque point
  à la droite du précédent, mesure inspirée de US6390005B1). Troisième référence :
  l'ancien auto-satin, reconstruit dans un worktree jetable depuis `18de427^`,
  jamais sur `main`. Sortie : tableau et SVG déterministes.
- Modules : `apps/cli`, `tests/unit/`, `libs/stitch_analysis` (lecture). Pas de
  nouvelle bibliothèque. Tout appel à `generate_sequence` passe par
  `effective_sequence` (garde `tests/check_no_raw_sequence_bypass.cmake`).
- Dépend de : rien.
- Acceptation : métriques calculées de façon déterministe (deux exécutions
  identiques) sur tout le corpus ; rapport de base commité ; test unitaire de
  chaque métrique.

### RD-PAT-004 — Critère d'éligibilité satin (régularité) [P1] — ☐ À faire
- Source : famille Goldman (US7016757B2) pour l'idée de statistiques de la
  transformée de distance (μ, σ, max). **L'usage comme porte de décision est
  notre extension** : la fiche du brevet le donne comme indication, et la règle
  `2σ < μ < max/2` n'est pas vérifiée sur le PDF. Aucun seuil du brevet repris.
- État OpenStitch : `satinability.cpp` calcule déjà largeur moyenne (2A/P),
  variation de largeur `(max-min)/moyenne`, élongation, refus des trous et test
  de jonction/branche. Le manque réel est le σ de la transformée de distance le
  long du squelette.
- À faire : fonction pure `regularity_stats(region)` s'appuyant sur ces
  statistiques et le σ de la distance ; éligibilité calibrée sur le corpus
  RD-PAT-003 (méthode ci-dessous).
- Modules : `libs/auto_satin`.
- Dépend de : RD-PAT-003.
- Acceptation : zéro satin au-delà de la largeur max ; taux de bandes fines
  retenues mesuré (cf. HP-AUTO-009) ; décision invariante par rotation 90°,
  miroir, translation.

### RD-PAT-002 — Auto-satin minimal, explicite et protégé [P0] — ☐ À faire
- **Mise à jour 2026-10** : `satin_planning` et `satin_coverage` n'existent plus sur la branche
  de recherche ; la fiche ci-dessous devra être réécrite sur le moteur par squelette
  (éligibilité et verdict de couverture via `SkeletonSatinDiagnostics`) avant toute reprise.
- Source : synthèse de RD-PAT-003/004 et du post-mortem RD-PAT-000.
- À faire : nouveau champ **nommé** d'`AutoOptions` (défaut désactivé). Seuls les
  rubans simples sont éligibles (une colonne, sans trou, sans jonction), via le
  planificateur existant. Verdict `satin_coverage` plus porte de supériorité
  (ci-dessous). **Satin entier ou repli entier**, jamais de mosaïque. Repli
  tatami/contour avec code de raison dans le résultat et dans l'UI.
- Aucune persistance nouvelle : rails et barreaux sont déjà stockés dans
  `SatinParams`.
- Modules : `libs/autodigitize`, `libs/satin_planning`, `libs/satin_coverage`,
  `apps/desktop` (réglage).
- Dépend de : RD-PAT-003, RD-PAT-004.
- Acceptation : option désactivée = **zéro différence** (goldens, DST, tests
  `autodigitize`) ; option activée jamais pire que le repli sur le corpus ;
  100 % des replis tracés.

### RD-PAT-001 — Moteur de traversées orientées [P0] — ◐ Partiel (livré sur la branche de recherche, non fusionné sur `main`, aucun essai machine)
- **Livré sur `claude/openstitch-patent-research-252787`** : moteur par squelette
  (`libs/auto_satin/src/{chord,axis,axis_sampler,orientation}`, `skeleton_satin.cpp`), type
  `AutoSatinParams` (`.osp` schéma 5), génération (`generate_auto_satin`), commandes
  (`EditAutoSatinCommand`), interface (création avec aperçu et refus motivés, guides, inspecteur),
  CLI `satin-auto-debug`, documentation `docs/source/satin-squelette.md`. L'ancien moteur
  (rails/barreaux, `satin_planning`, `satin_coverage`) est supprimé de la branche.
- **Reste** : essais machine (aucun), intégration à l'auto-numérisation (RD-PAT-002/003/004),
  limite connue `deep_channel` (couverture ≈ 0,65 sur bras très larges), densité selon la
  largeur (RD-PAT-013), fusion sur `main` après autorisation du propriétaire.
- Texte d'origine de la fiche (conservé pour traçabilité) :
- **Décision du propriétaire (2026-10) : remplacement intégral de l'ancien moteur
  d'auto-satin.** Ce n'est plus conditionnel. Spécification révisée après audit
  critique : `specs/plans/satin-squelette-traversees.md` (30 problèmes, solutions,
  pipeline). Le banc RD-PAT-003 sert alors de validation et de référence, pas de
  condition d'ouverture.
- Source : EP0761860B1 (axe médian, orientation ⟂ interpolée) et US6390005B1
  (espacement ; l'objectif de pas ⟂ est déjà réalisé par
  `resample_by_medial_spacing`). **Échantillonnage de l'axe, 0° par défaut,
  interpolation d'angle mod 180°, `Lmax`/`y`, `h` adaptable : notre conception.**
- État OpenStitch (audit) : le rail A/B et les barreaux sont persistés ; les
  barreaux jouent déjà le rôle de guides par interpolation de correspondance ;
  `max_stitch_length` et `SatinSplit` existent ; le mode Paramétrique existe ;
  le champ d'angle sur angle doublé (mod 180°) existe dans
  `directional_fill.cpp`. La seule capacité réellement absente est un barreau
  **dont l'angle est indépendant de la perpendiculaire aux rails**. Une traversée
  qui coupe un trou exige plusieurs colonnes : c'est le problème du
  planificateur, pas des traversées.
- À faire si justifié : réutiliser le champ d'angle existant plutôt qu'en écrire
  un troisième ; encoder `θ(s)` en barreaux épars ; en-tête interne dans
  `libs/auto_satin/src/` (comme `medial_field.hpp`), sans nouvelle bibliothèque.
  La vérification `satin_coverage` et le repli restent dans `autodigitize` ou
  `satin_planning` (un appel depuis `auto_satin` créerait un cycle).
- Acceptation : égale ou dépasse le moteur existant sur la classe ciblée
  (RD-PAT-003) ; sortie byte-identique sur deux exécutions ; invariance par
  rotation/miroir de la décision d'acceptation.

## Calibration et portes d'acceptation

Portes dures (une région qui échoue part au repli) : couverture noyau ≥ 0,995 et
brute ≥ 0,90 ; débordement ≤ 0,05 ; trou maximal ≤ 0,5 mm ; aucun croisement de
barreaux ; aucun satin plus large que le maximum ; aucun point sous 0,5 mm ;
99,9 % des longueurs de fil dans [min, max] ; sortie identique sur deux
exécutions. **Porte de supériorité** (satin accepté seulement si tout tient) :
points ≤ repli ; sauts + coupes ≤ repli ; part de points courts ≤ repli ;
alignement fil/normale meilleur que le tatami à angle fixe ; couverture au pire
repli − ε.

Méthode de calibration des seuils :
1. Corpus d'au moins 150 régions : formes synthétiques plus régions d'images
   réelles (10 à 20 images de HP-AUTO-001). Découpage **par image**.
2. Étiquetage visuel « satin acceptable ou non » par deux relecteurs ; l'ancien
   auto-satin sert de référence négative.
3. Distributions brutes (μ, σ, max, variation de largeur, élongation, couverture,
   nombre de points). Seuils au point de **zéro faux accept** de l'entraînement,
   marge de sécurité (× 1,25 vers plus strict). Un faux accept coûte bien plus
   qu'un faux rejet : le repli est toujours acceptable.
4. Validation sur images tenues à part (faux accept ≤ 2 %), robustesse au bruit de
   contour, ± 10 % de résolution, rotation.
5. Seuils gelés dans des tests comme planchers avec marge
   (`test_coverage_regression.cpp`), recalibrés par commit documenté.

## Tatami et analyse

### RD-PAT-005 — Critère alternatif d'angle de tatami (moins de fragments) [P2] — ☐ À faire
- Source : US8219238B2 (16 angles candidats, minimum de fragments). Nombre
  d'angles et départage : notre conception.
- État OpenStitch (corrigé) : un **choix automatique d'angle existe déjà**
  (`AutoOptions::auto_fill_angle`, axe principal puis recherche d'écart avec les
  voisins par pas de 5°, `autodigitize.cpp`). Le critère du brevet est un critère
  **alternatif**, utile sur les formes concaves ; il changerait les sorties
  existantes, les goldens et l'audit « marine ».
- À faire : audit chiffré d'abord ; si le gain est démontré, critère optionnel.

### RD-PAT-006 — Règle d'analyse « points entièrement recouverts » [P2] — ☐ À faire
- Source : US6633794B2 (outil manuel). Rattaché à HP-ENG-011.
- À faire : règle de pré-export **en lecture seule** dans `stitch_analysis`,
  exemptant sous-couches et trajets structurels ; sert d'étalon à HP-ENG-011.
  Aucune suppression automatique ; aucune réécriture de `effective_sequence`.

### RD-PAT-007 — Sous-couche des formes étroites : règle en trois cas [P2] — ☐ À faire
- Source : JP3922316B2, US5823127A (traduction automatique : à confirmer).
- À faire : audit d'abord (l'inset Clipper2 produit-il des morceaux hors forme ?) ;
  cas reproduit en test avant tout code.

### RD-PAT-008 — Élagage topologique et ordre de couture par arbre [P2] — ☐ À faire
- Source : US6356648B1, US6690988B2, US8532810B2, US5283747A. Comparer à
  `SkeletonGraph` et au routage existants ; ne coder que les manques démontrés.

### RD-PAT-013 — Densité satin modulée par la largeur [P1] — ☐ À faire
- Source : US5343401A (exemple imprimé : une densité de base de 56,4 points par
  pouce devient 28,2 pour 2 mm et 84,6 pour 30 mm). La loi de modulation n'est
  pas donnée : notre conception, calibrée sur nos corpus. Rattaché à HP-ENG-003.
- État OpenStitch (vérifié) : aucune densité dépendant de la largeur dans
  `libs/stitch_generation/src/satin.cpp` ni dans `libs/satin_planning` ; la densité
  est un paramètre uniforme de `SatinParams`.
- À faire : mode « Auto » de densité (défaut pour les nouveaux objets, anciens
  projets inchangés), fonction pure `density_for_width`, chaîne complète
  (modèle, sérialisation, commande, génération, UI, tests) par la procédure
  `openstitch-stitch-param`.
- Modules : `libs/stitch_generation`, `libs/document`, `libs/project_io`,
  `libs/commands`, `apps/desktop`.
- Acceptation : une colonne étroite et une large ont des densités différentes
  selon la loi documentée ; un ancien `.osp` produit la même séquence ; déterminisme.

### RD-PAT-014 — Graphes de longueur et d'angle par point (diagnostic) [P3] — ☐ À faire
- Source : US6167823B1 (angle entre vecteurs consécutifs ramené à [0, 180] ;
  signatures : course au plancher, zigzag vers 90°, satin vers 180°). L'accrochage
  « B>D » du brevet est inversé par rapport à son texte : ne pas le reprendre.
- À faire : sortie CLI (CSV/SVG) de longueur et d'angle par point depuis
  `effective_sequence`, utilisable par le banc RD-PAT-003.
- Modules : `libs/stitch_analysis`, `apps/cli`.

## Hors objectif directeur (suivis ailleurs, non prioritaires pour cette mission)

- **RD-PAT-009 Appliqué** (US5438520A, JP3769602B2) : suivi dans HP-SPEC-001.
- **RD-PAT-010 Remplissage polaire** (US6937919B1) et **RD-PAT-011 Stippling**
  (US6968255B1, HP-STI-013) : P3.
- Abandonné : remplissage curviligne par grille (u,t) (US6587745B1) : doublon du
  champ de flux de `directional_fill.cpp`.

## Écartés ou différés (avec raison)

| Brevet | Raison |
|---|---|
| US6253695B1 | les points sont dérivés, jamais stockés : la densité se règle en amont |
| US5343401A, US4849902A, US7386361B2 | lettrage sur arc, numérisation manuelle, stylet : hors périmètre |
| US5740056A, US5576968A | types de point rudimentaires, contrainte machine à deux sens |
| US9200397B2 | simplification générique déjà couverte par `geometry` |
| US6397120B1 | base d'apprentissage de jonctions : hors périmètre ; UI après stabilisation du moteur |
| US6167823B1 | panneau d'analyse longueur/angle : confort UI, faible priorité |
| US6510360B1, US6247420B1 | import DST → objets : fonction absente et hors périmètre, seuils à calibrer |
| US5934209A | Clipper2 fait déjà mieux que la découpe O(n²) du brevet |
