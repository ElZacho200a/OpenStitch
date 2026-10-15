# Recherche — moteur d'auto-satin

Références étudiées, choix d'algorithmes et licences pour la conversion
automatique d'une région en colonnes satin. Aucune ligne de code tierce n'est
copiée (voir §Licences).

> Les sections v1 ci-dessous sont **historiques** : elles précèdent le moteur
> par traversées orientées. La référence actuelle est
> [satin-squelette.md](source/satin-squelette.md). L'étude du 2026-10-14, en fin
> de page, propose plusieurs stratégies ; elle ne décrit pas des options déjà
> implémentées.

## Choix d'algorithme de squelette

| Méthode | Disponibilité ici | Décision |
|---|---|---|
| `cv::ximgproc::thinning` | **NON** — le module contrib `ximgproc` n'est pas dans notre build OpenCV (features `core,png,jpeg,tiff` uniquement) | écartée |
| Amincissement **Zhang-Suen** | réimplémentable, simple, déterministe | **RETENU** (v1) |
| Amincissement Guo-Hall | idem, alternative | réévaluable |
| Medial axis par Voronoï (CGAL) | CGAL non intégrée, licence GPL/LGPL, lourde | écartée |
| Maxima de la transformée de distance | crêtes parallèles, bruité | non seul (sert au poids) |

**Décision** : rasterisation physique (masque binaire) → **transformée de
distance** (OpenCV, `cv::distanceTransform`, disponible dans `imgproc`) pour la
demi-largeur locale et l'élagage → **amincissement Zhang-Suen** (implémenté en
interne, sans dépendance) → conversion en **graphe** (nœuds = extrémités /
jonctions ; arêtes = polylignes de pixels de degré 2 compressées). Le graphe
porte le rayon local (distance) le long de chaque arête, ce qui guide l'élagage
et l'analyse de satinabilité.

Rationale : Zhang-Suen est un standard publié (méthode classique, non
protégeable), déterministe, et suffisant pour une première version. La
transformée de distance d'OpenCV fournit une demi-largeur robuste.

## Analyse de satinabilité

Réf. : métriques de forme classiques. On calcule aire, périmètre, largeur
moyenne (`2·aire/périmètre`), largeur maximale (`2·max` de la transformée de
distance), largeur minimale (le long du squelette), élongation
(`longueur_axe / largeur_moyenne`), nombre de trous, nombre d'extrémités et de
jonctions du graphe. Le **statut** en découle (Suitable, SuitableWithWarnings,
RequiresDecomposition, Ambiguous, Unsuitable).

## Licences

| Dépendance | Licence | Usage ici |
|---|---|---|
| OpenCV (core, imgproc) | Apache-2.0 | rasterisation + transformée de distance (encapsulée dans `libs/auto_satin`) |
| Clipper2 | BSL-1.0 | nettoyage polygonal (via `libs/geometry`) |
| Ink/Stitch | **GPL-3.0** | **étude conceptuelle uniquement** du modèle rails+barreaux — **aucune copie** |

Point juridique : notre projet est **Apache-2.0**. Ink/Stitch est GPL ; intégrer
son code imposerait une redistribution copyleft. On n'en reprend donc **ni code
ni structure expressive** ; les algorithmes (Zhang-Suen, distance transform,
skeleton pruning, medial axis) sont des méthodes publiées réimplémentées
indépendamment. Toute réutilisation de code tiers ferait l'objet d'une
vérification de licence spécifique.

## Parties réutilisées de tiers

Aucune. Zhang-Suen, la conversion squelette→graphe et l'élagage sont
réimplémentés. OpenCV et Clipper2 (permissives) sont utilisées via des
interfaces internes.

## Déterminisme

Toutes les étapes sont déterministes : rasterisation à résolution fixe,
transformée de distance déterministe, Zhang-Suen déterministe, tri stable des
nœuds/arêtes (par coordonnées puis identifiant). Aucun `unordered_map` dans les
chemins de décision.

## Risques

- **Résolution** : un pixel trop grossier perd les formes fines ; trop fin
  produit des images énormes. Résolution par défaut 0,05 mm, bornée par une
  dimension maximale.
- **Squelette bruité** : les branches parasites sont élaguées par score
  (longueur × rayon local) — implémenté en mission ultérieure ; pour l'instant le
  graphe brut est exposé et diagnostiqué.
- **Formes ambiguës** : cercle plein et formes très larges refusés proprement.
  Un anneau fin à un trou est désormais décomposé en quatre sections ouvertes
  raccordées ; un anneau non appariable reste refusé.

## Plusieurs algorithmes au choix (étude du 2026-10-14)

*État : proposition de conception, non implémentée, non mesurée sur notre corpus,
non validée sur machine.*

### Besoin et point de départ

Permettre de comparer plusieurs résultats pour **la même région**, puis de
conserver la méthode préférée dans l'objet. Il ne s'agit pas de remplacer
silencieusement le moteur actuel ni de restaurer l'ancien SGSD.

Le moteur actuel combine plusieurs décisions indépendantes :

1. rasterisation et amincissement **Zhang-Suen** pour obtenir un graphe ;
2. élagage et construction d'axes, puis cellules aux jonctions/coudes ;
3. orientations perpendiculaires aux axes, modifiables par guides ;
4. intersection de droites avec la région pour obtenir les traversées ;
5. densité adaptative et finitions satin communes.

Vérification du code au 2026-10-14 : `AutoSatinParams` n'a pas de sélecteur
de stratégie ; il porte seulement `compact_axis` pour le secours compact.
Les cellules par distance au morceau d'axe le plus proche ne constituent pas
déjà un axe médian polygonal par Voronoï. Le schéma `.osp` courant est **6**
(`libs/project_io/include/openstitch/project_io/project_io.hpp`), et le moteur
a désormais un cache LRU à clé de contenu (`skeleton_satin.cpp`, `make_key` /
`result_cache`) : les mentions schéma 5 / absence de cache dans l'ancien état
du chapitre ne décrivent plus exactement le code. Le corpus principal vérifié
comprend **27 formes** (`test_skeleton_satin.cpp`, test des planchers).

Changer seulement Zhang-Suen pour Guo-Hall est une variante de la première
étape, pas une nouvelle solution générale au placement des traversées. De même,
fractionnement, points courts et sous-couches ne sont pas des algorithmes de
décomposition de forme.

### Familles candidates

Les usages du tableau sont des **hypothèses à vérifier**, pas un classement
mesuré ni une garantie de meilleure broderie.

| Famille | Principe | Cas à comparer | Limites / priorité proposée |
|---|---|---|---|
| **Squelette raster** (actuel) | Zhang-Suen → axes → traversées orientées | rubans, lettres, réseaux branchés, anneaux | Référence conservée ; dépend de la résolution et de l'élagage. |
| **Axe médian vectoriel** | Voronoï des segments du contour → arêtes intérieures pertinentes → axes | contours vectoriels propres, rubans obliques, courbes, jonctions | Première alternative recommandée. Supprime la discrétisation de l'axe par pixels, **pas** la sensibilité de l'axe médian au bruit du contour. |
| **Balayage parallèle** | droites à angle constant → intervalles intérieurs → regroupement en bandes continues | disques, ovales, pétales, formes compactes | Deuxième alternative, indépendante du squelette. Les concavités/trous exigent de séparer les intervalles ; ne pas coudre directement entre eux. Les grandes largeurs nécessitent des points fractionnés. |
| **Triangulation / axe chordal** | triangulation contrainte de la région → graphe ou regroupement de triangles en rubans | géométries anguleuses, étranglements, formes branchées | Piste de recherche ultérieure ; dépend du maillage, de ses dégénérescences et de la politique de regroupement. Une triangulation seule ne définit pas un satin. |
| **Straight skeleton** | propagation des arêtes vers l'intérieur → squelette polygonal | formes très anguleuses | À distinguer de l'axe médian : pas toujours centré dans les formes concaves. Intérêt à démontrer avant intégration. |
| **Champ de directions** | direction lissée dans toute la région, contrainte par contour et guides | transitions d'orientation complexes | Recherche ultérieure. Les lignes de courant courbes produisent plutôt un remplissage directionnel ; elles ne deviennent pas du satin bord-à-bord sans une construction supplémentaire. |

Le balayage proposé doit être **direct**, même lorsqu'un squelette existe. Il
ne serait donc pas un renommage de `compact_axis`, qui synthétise aujourd'hui
un axe droit seulement lorsqu'aucune chaîne n'existe. Il doit maintenir le
zigzag bord-à-bord ; transformer systématiquement les traversées en points
courants courts donnerait un autre type de remplissage.

### Réutilisation et licences

- **Boost.Polygon Voronoi** : entrées points/segments en coordonnées entières
  (int32 supporté par défaut), BSL-1.0, dépendance envisageable avec les règles
  Apache-2.0 du dépôt. Ce n'est **pas** un générateur de satin ni un extracteur
  d'axe médian prêt à brancher : filtrage intérieur, élimination des arêtes non
  médiales, approximation des arcs paraboliques, rayon au bord et conversion
  déterministe en µm restent à écrire. Les contours doivent respecter ses
  préconditions (segments sans intersection hors extrémités communes).
- **CGAL** : les packages Straight Skeleton et Segment Delaunay consultés sont
  GPL dans leur distribution libre ; ne pas les intégrer dans ce projet. La
  famille algorithmique n'est pas pour autant exclue.
- **Ink/Stitch** : son outil documenté `Fill to Satin` est **semi-automatique**,
  avec barreaux et bridges fournis par l'utilisateur. `Auto-Route Satin` route
  des colonnes existantes ; il ne résout pas la conversion d'une région
  quelconque. Documentation conceptuelle uniquement, aucune copie GPL.
- Toute autre bibliothèque de triangulation ou de résolution de champ doit
  faire l'objet d'une vérification de licence propre avant intégration.

### Proposition d'interface et d'architecture

Première cible : trois libellés compréhensibles dans l'inspecteur de l'objet
auto-satin : **« Squelette (actuel) »**, **« Axe médian vectoriel »**,
**« Balayage parallèle »**. Ne montrer que les méthodes effectivement livrées.
Les noms des primitives (Zhang-Suen, Voronoï) peuvent rester dans les infobulles.

- Un choix de stratégie persistant dans `AutoSatinParams`, avec **le moteur
  actuel comme défaut**, y compris son comportement `compact_axis` et la
  lecture d'un `.osp` sans la nouvelle clé.
  Le contrat de lecture d'une stratégie inconnue doit être explicite : pas de
  substitution silencieuse qui ferait apparaître un rendu différent à l'export.
  Évaluer une hausse du schéma depuis v6 : un ancien lecteur ignorant la clé
  ne doit pas régénérer un autre satin à l'insu de l'utilisateur.
- Une façade dans `libs/auto_satin`, pas un dispatch métier dans les widgets.
  Les méthodes d'axe peuvent partager la génération de traversées ; le balayage
  fournit ses propres bandes. Toutes rejoignent les finitions satin communes.
- Changement via `SetStitchParamsCommand` : annulation/rétablissement,
  sauvegarde, aperçu et export doivent utiliser la même stratégie.
- Faire choisir/essayer la stratégie **avant** le refus d'une forme sans colonne.
  `MainWindow::createAutoSatin` refuse actuellement avec les paramètres par
  défaut avant d'ouvrir le dialogue : ajouter seulement une liste dans ce
  dialogue laisserait inaccessibles les alternatives pour les formes refusées.
  Vérifier aussi la sélection multiple.
- Inclure stratégie et réglages spécifiques dans **toutes** les clés de cache,
  moteur et résumé UI (`summary_key`), avec tests d'invalidation. Ce dernier
  n'inclut actuellement pas `compact_axis` ; ne pas reproduire cette omission.
- Transporter la stratégie dans génération, aperçu/overlay et essai de
  `autodigitize::choose_satin`, puis dans les objets créés. Préciser la portée
  « par forme » : au premier lot, choix par `EmbroideryObject`, appliqué à tous
  les morceaux de son vecteur source. La classification par `largest_piece`
  ne suffit pas à valider les autres morceaux.
- Les guides sont conservés lors des essais. Si une méthode ne sait pas les
  appliquer, l'interface le signale ; elle ne les ignore pas silencieusement.
  Le balayage a son propre angle global et ne prétend pas respecter des guides
  locaux contradictoires.
- Un bouton **« Comparer les méthodes »** calcule les variantes sur un snapshot
  hors du fil d'interface, sans modifier le document. Appliquer un candidat
  crée une seule commande annulable ; fermer l'aperçu ne change rien.
- Pas de « meilleur automatiquement » au premier lot : le choix esthétique et
  textile ne se réduit pas à la couverture. Une recommandation automatique
  viendra seulement après mesures et essais machine ; la méthode réellement
  retenue restera identifiable et reproductible.

### Preuve attendue et découpage

1. **Lot A — référence et comparaison CLI** : même contour et mêmes finitions,
   SVG/rapport par stratégie ; conserver les résultats actuels comme référence.
   *Livré le 2026-10-15 (façade `SatinStrategy`, `SatinMetrics`, commande
   `satin-compare`) ; mesures sur séquence (sauts, coupes, points) restantes.*
2. **Lot B — Voronoï** : extraction vectorielle et déterminisme, sans changement
   du défaut ; comparaison chiffrée avant de l'exposer comme amélioration.
3. **Lot C — choix utilisateur** : paramètre, `.osp`, commande, inspecteur,
   tests du chemin UI et de l'export, documentation utilisateur.
4. **Lot D — balayage direct** : bandes, trous et concavités, angle global,
   réutilisation des finitions et comparaison au squelette sur formes compactes.
   *Livré le 2026-10-15 (`SatinStrategy::ParallelSweep`) ; voir `satin-squelette.md`.*
5. **Lot E — aperçus comparatifs** : calculs sans mutation, application annulable.

Comparer couverture **et localisation des manques**, recouvrement et pics de
densité, croisements, longueur maximale des points de couture (y compris les
retours du zigzag), sauts/coupes, nombre de points, temps Release et mémoire.
Séparer les traversées géométriques de la séquence réellement exportée : une
bonne couverture avant finitions ne prouve pas que le DST reste correct.

Corpus : rectangle/capsule/ruban/S, Y/T/croix/H/E/trident, anneau/deux trous,
`deep_channel`, disque/pétale ; ajouter contours bruités, rotations, translations
et tailles limites. Tests d'égalité pour le défaut, de déterminisme, de round-trip
avec/sans clé, d'undo/redo et du vrai chemin inspecteur → aperçu → export.
Les nouveaux algorithmes ont leurs propres résultats : ne pas les obliger à
produire les mêmes points que Zhang-Suen, ni abaisser les attentes de celui-ci
pour les faire passer. Terminer par des essais sur machine.

### Sources et portée de la recherche

Consultées le 2026-10-14 ; ces sources décrivent des briques ou des outils,
**pas** un moteur de remplacement déjà validé dans OpenStitch :

- [Boost.Polygon — Voronoi](https://www.boost.org/doc/libs/latest/libs/polygon/doc/voronoi_main.htm) :
  segments, préconditions, précision et licence BSL-1.0.
- [CGAL — Straight Skeleton](https://doc.cgal.org/latest/Straight_skeleton_2/index.html) et
  [licences des packages](https://doc.cgal.org/latest/Manual/packages.html) :
  distinction straight skeleton/axe médian, trous, licences spécifiques.
- [Ink/Stitch — outils satin](https://inkstitch.org/docs/satin-tools/) :
  conversion guidée et routage, à ne pas confondre.
- Prasad, *Rectification of the chordal axis transform and a new criterion for
  shape decomposition* (conférence 2005), [notice OSTI](https://www.osti.gov/biblio/977914) :
  piste d'axe chordal ; résumé de la notice consulté, article non relu intégralement.
- *Directionality-Aware Design of Embroidery Patterns* (2023),
  [DOI 10.1111/cgf.14770](https://doi.org/10.1111/cgf.14770) :
  champ de directions, lignes de courant et chemin fabricable ; résultat de
  recherche consulté, texte intégral inaccessible ici (HTTP 403). Ne pas le
  présenter comme un algorithme de satin prêt à implémenter.

Références locales : [moteur actuel](source/satin-squelette.md),
[fiches satin](source/patent-sheets-satin.md), et
[blocs à direction constante](source/patent-sheets-autodigitize.md), §US4849902A.
Une publication ou une mention dans un brevet ne vaut ni validation textile
ni autorisation juridique ; ne pas déduire le statut de tous les brevets d'une
famille depuis une seule fiche.
