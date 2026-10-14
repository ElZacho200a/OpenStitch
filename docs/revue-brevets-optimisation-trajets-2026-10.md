# Revue technique de brevets — optimisation des trajets (2026-10-14)

**Ce document n'est pas un avis juridique.** Il compare, élément par élément, les revendications de
deux brevets affichés « actifs » aux fonctionnalités d'OpenStitch livrées ou planifiées dans la PR
#40. Il sert à préparer la relecture par une personne compétente (conseil en propriété
industrielle), pas à la remplacer. L'interprétation d'une revendication (étendue, équivalents,
historique de procédure) est une question de droit que je ne tranche pas. Les statuts et dates
viennent de Google Patents, qui précise lui-même qu'ils ne sont pas une conclusion juridique.

## Ce qui a été lu, et ce qui ne l'a pas été

| Brevet | Lu | Statut (Google) |
|---|---|---|
| **US8851001B2** (Melco, *Method for improved stitch generation*) | **25 revendications en entier** + description | actif, échéance affichée 2032-09-23 |
| **US8271123B2** (Brother, génération de données de broderie) | **18 revendications en entier** + passages de la description | actif, échéance affichée 2031-04-22 |
| US9702070B2 (division de US8851001) | non lu | actif |
| US9200397, US6860214B1 / US20050061220A1, US10590580, US11727615 | non lus | statuts non vérifiés |

Les brevets non lus l'ont été pour une raison technique : Google Patents a répondu par une page
« Sorry… trafic inhabituel » (HTTP 503) à mes requêtes automatisées, et l'autre source essayée
(Justia) a répondu 403. **Je ne contourne pas ces blocages.** La relecture de ces documents reste à
faire par une personne qui peut les consulter.

## US8851001B2 (Melco)

### Revendications indépendantes (lues)

**Revendication 1** — un procédé qui : (a) *altère* un design de broderie pour obtenir un design
altéré, comprenant au moins un élément de design ; (b) **détermine la taille de l'élément en lui
attribuant un type de point temporaire, en générant des points temporaires, et en déterminant la
taille d'après la largeur de point des points temporaires** ; (c) **sélectionne un type de point
pour l'élément en comparant sa taille à un ensemble de valeurs**, le type étant choisi dans cet
ensemble d'après la taille ; (d) génère un motif de points pour l'élément.

**Revendication 19** — même chose pour au moins deux éléments, chacun recevant son type de point
d'après sa taille (la taille du premier étant déterminée comme en (b)).

Les revendications dépendantes précisent : l'altération est un agrandissement (2) ou un changement de
tissu (3) ; le type choisi est un type de sous-couche (4, 5 : centre, bord, zigzag, remplissage) ou de
couche supérieure (6, 7 : satin, remplissage à pas) ; la taille retenue est la largeur d'un point
représentatif, médian (8, 9) ; plages de tailles prédéfinies ou choisies par l'utilisateur (10 à 12,
15, 17, 24).

### Comparaison avec OpenStitch

| Élément de la revendication 1 | Ce que fait OpenStitch (code lu) | Écart |
|---|---|---|
| (a) altérer le design (mise à l'échelle, changement de tissu) | les points ne sont jamais stockés, ils sont régénérés à chaque changement de géométrie ou de réglage (`generate_sequence`) | analogue de fait ; le changement de tissu (`HP-FAB-001`) n'existe pas encore |
| (b) taille par **points temporaires** (type temporaire, génération, largeur de point) | la taille est mesurée **sur la géométrie** : `measure_shape` (aire, périmètre, largeur moyenne 2·A/P) pour le tatami, `satin_mean_width_mm(rail_a, rail_b)` pour le satin ; aucune génération de points pour mesurer | **élément (b) absent** de ces chemins |
| (c) comparer à un jeu de valeurs et en tirer le type | `choose_fill_underlay`, `choose_satin_underlay` : seuils fixes sur ces mesures géométriques | analogue de fait, mais sur une taille obtenue autrement qu'en (b) |
| (d) générer le motif | oui | — |

Point à regarder de plus près : le mode auto-numérisation `satin_bands` (`autodigitize.cpp`) décide
d'abord par des largeurs **géométriques** de la région (largeur moyenne ≥ 0,8 mm, largeur maximale
≤ `satin_max_width`), puis **génère un satin d'essai** et ne le garde que si sa couverture atteint
`satin_min_coverage` (0,9), sinon repli sur le tatami. C'est le seul endroit du dépôt où l'on
génère des points d'essai pour décider d'un type de point. Ce qui est mesuré est un **taux de
couverture**, pas une « largeur de point », et le résultat est un oui/non pour un seul candidat, pas
une comparaison à des plages de tailles. Ce chemin est désactivé par défaut (`satin_bands = false`).
À soumettre au conseil.

### Fonctionnalités de la PR #40 au regard de ce brevet

- Ponts cachés, réordonnancement de la sous-couche, réordonnancement des régions, connecteur
  d'objet, optimisation à l'export : **aucune ne choisit un type de point d'après une taille** ; la
  preuve de recouvrement des ponts rastérise les points **finals** de la séquence et ne mesure pas la
  taille d'un élément. Elles ne reprennent donc pas les éléments (b) et (c).
- Le brevet décrit aussi (description, hors revendications lues ici) une **compensation « push »**
  par table largeur du bout d'élément → réduction selon le tissu, et une **sous-couche continue** par
  parcours de graphe (tour d'Euler, parcours en largeur). OpenStitch n'implémente **ni l'une ni
  l'autre** sous cette forme. Les revendications de la division US9702070B2 (active) et des deux
  divisions abandonnées (US20140366788, US20140366790) **n'ont pas pu être lues** : c'est
  précisément là que ces sujets pourraient être revendiqués.

### Garde-fous retenus (déjà dans la roadmap)

1. Ne pas faire dépendre le type de point d'une mesure obtenue en générant des points temporaires ;
   mesurer sur la géométrie (`HP-FAB-001`, `HP-AUTO-009`, `RD-PAT-004`).
2. Le futur profil de tissu (`HP-FAB-001`) règle des **facteurs** (densité, compensation), il ne
   change pas le **type** de point.
3. Ne pas ajouter de table de compensation « push » par largeur de bout et par tissu ; la
   compensation calibrée prévue (`HP-ENG-001.a`) est une table largeur → compensation issue de
   mesures d'une pièce-test, sans le critère « bout d'élément ».
4. Ne pas implémenter de sous-couche continue par tour d'Euler sur un graphe de lignes dupliquées
   sans relecture préalable.

## US8271123B2 (Brother)

### Revendications (lues, les 18)

La revendication 1 (appareil) et la revendication 10 (support) portent sur la **génération de
données de broderie à partir d'une image** : acquisition des couleurs de fil disponibles, premières
données de segments, **données d'angle associées aux pixels** qui recouvrent un segment, seconds
segments obtenus en suivant la **direction d'extension** donnée par l'angle, **attribution à chaque
segment d'une couleur** parmi les couleurs disponibles, **segments de liaison entre segments de
même couleur**, puis données de broderie (ordre de couture, couleurs, points). Les dépendantes (2 à
9, 11 à 18) précisent la division en zones, les pixels « non définis » et leur remplissage par les
angles voisins, les pixels d'intersection, la détection de zones de basse fréquence.

**Aucune revendication ne mentionne un trajet de liaison recouvert par des points cousus plus
tard.** Ce procédé figure seulement dans la description (« si les points d'un segment de liaison
doivent être recouverts par d'autres points cousus plus tard, des points droits sont cousus sur le
segment de liaison »).

### Comparaison avec OpenStitch

| Élément revendiqué | OpenStitch | Écart |
|---|---|---|
| données d'angle **par pixel**, extension de segments le long de la direction d'angle | non : aucun code ne construit de segments de couture à partir d'angles par pixel ; le mode « lignes cohérentes » (`libs/image/src/coherent_lines.cpp`, Kang, Lee et Chui, NPAR 2007) produit une **image** de traits (FDoG sur champ de tangentes) avant segmentation, pas des segments de couture orientés | présent à l'état de voisin, pas identique : à signaler |
| attribution de couleur de fil par segment | non sous cette forme (couleurs par région segmentée) | — |
| segments de liaison entre segments de même couleur | les ponts cachés relient des **objets** ou des passes d'un même objet, pas des segments issus d'un champ d'angles | écart |
| ensemble image → segments → liaisons → données | non | — |

Conclusion technique : les fonctionnalités de la PR #40 ne reprennent ni la chaîne image → angles
par pixel → segments, ni ses dépendantes. Le mécanisme de liaison recouverte n'apparaît pas dans les
revendications lues. **À confirmer par le conseil**, notamment sur la portée de la description et
sur d'éventuelles continuations non listées.

## Ce qui reste à faire (par une personne compétente)

1. Lire les revendications de **US9702070B2** (active, famille de US8851001) : push et sous-couche
   continue y sont probablement revendiquées.
2. Lire US9200397, US6860214B1, US20050061220A1, US10590580, US11727615 (liaisons recouvertes,
   édition de chemin d'aiguille, broderie vectorielle) et vérifier leurs statuts auprès de l'USPTO.
3. Faire examiner par un conseil : le chemin `satin_bands` (essai de génération puis couverture) au
   regard de US8851001 ; la proximité du mode « lignes cohérentes » avec US8271123.
4. Vérifier les dates d'expiration réelles (frais de maintenance, ajustements) : les dates affichées
   par Google sont des hypothèses.

Rien dans la PR #40 n'est conditionné à cette relecture pour fonctionner ; elle conditionne en
revanche `HP-FAB-001` (profil de tissu), toute compensation « push », et une éventuelle réactivation
de `satin_bands` par défaut.
