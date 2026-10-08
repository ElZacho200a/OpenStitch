# Recherche brevets broderie

Public : developpeur, mainteneur. Etat : **R&D preliminaire**.

Cette page catalogue les brevets consultes pendant la mission R&D de 2026-10.
Les brevets sont traites comme sources techniques historiques, jamais comme
code, licence, ni certification de liberte d'exploitation. Les statuts ci-dessous
sont des verifications preliminaires issues des pages publiques Google Patents ;
ils doivent etre confirmes dans les registres officiels avant toute decision
juridique.

## Index priorise

| Brevet | Sujet | Statut preliminaire | Interet OpenStitch |
|---|---|---|---|
| [EP0761860B1](https://patents.google.com/patent/EP0761860B1/en) | lignes caracteristiques, centre-ligne, branches, orientations interpolees | expire selon Google Patents | P0 : reference conceptuelle pour satin guide squelette |
| [US6390005B1](https://patents.google.com/patent/US6390005B1/en) | satin tournant a espacement inter-point constant | expire selon Google Patents | P0 : qualite/densite dans les virages |
| [US6690988B2](https://patents.google.com/patent/US6690988B2/en) | bitmap vers objets broderie par squelette/noeuds/chemins | expire fee-related selon Google Patents | P1 : auto-numerisation et reseau de traits |
| [US6397120B1](https://patents.google.com/patent/US6397120B1/en) | UI de singularites et alternatives de jonction | expire selon Google Patents | P1 : aide utilisateur sur jonctions ambigues |
| [US6253695B1](https://patents.google.com/patent/US6253695B1/en) | changement de densite d'un groupe de points existants | expire fee-related selon Google Patents | P1 : edition/import de points existants |
| [US6587745B1](https://patents.google.com/patent/US6587745B1/en) | remplissage courbe par transformation vers espace parametrique | expire fee-related selon Google Patents | P2 : fill courbe/decoratif, hors coeur satin |
| [US8219238B2](https://patents.google.com/patent/US8219238B2/en) | generation automatique depuis image scannee | expire selon Google Patents | P2 : optimisation et classification auto |
| [US6633794B2](https://patents.google.com/patent/US6633794B2/en) | suppression de points sous-jacents recouverts | expire selon Google Patents | P3 : optimisation prudente des points masques |

## EP0761860B1 - lignes caracteristiques

Signal technique : une region est analysee par lignes caracteristiques internes
souvent proches de la ligne mediane. Les points importants sont les extremites,
les branches et certains coudes. Autour d'une branche, le document decrit des
lignes de division vers le contour et une orientation de broderie interpolee
entre des points de reference. L'orientation peut etre essentiellement
perpendiculaire a la ligne caracteristique sur une extremite et se raccorder a
une orientation definie pres de la branche.

Adaptation OpenStitch : ne pas recopier le decoupage revendique comme recette
unique. Reutiliser plutot les briques existantes :

- `auto_satin::analyze_region` pour rasterisation, distance field,
  `thin_zhang_suen` et `SkeletonGraph`.
- `auto_satin::build_satin_columns` comme point d'entree des colonnes.
- `satin_coverage::analyze_satin_coverage` comme oracle de validation
  geometrique.
- `satin_planning::build_satin_sections` comme contrat applicatif :
  sections, statut, reliquat explicite.

Etat du code : OpenStitch possede deja une grande partie du pipeline squelette
et SGSD. Le risque de duplication est eleve ; toute nouvelle approche doit se
brancher dans `libs/auto_satin` ou comme voisin direct, pas dans le desktop.

Cas limites : cercles et disques au squelette degenere, T/Y/X, trous, boucles,
branches proches, largeur depassant le satin stable.

## US6390005B1 - espacement constant en satin tournant

Signal technique : le brevet cherche a maintenir un espacement inter-point
predetermine lorsque les points satin tournent avec la forme. L'ajustement est
lie a la longueur et a l'angle du point suivant.

Adaptation OpenStitch : le generateur `fill_satin_columns` gere deja
l'espacement a partir de rails et barreaux ; le travail utile est de lui fournir
des rails/barreaux mieux classes, puis de mesurer la regularite par tests
geometriques. Le satin tournant par anneaux (`IsoOffsetRing`) couvre deja les
formes rondes/compactes de HP-STI-018, mais reste experimental et sans validation
machine.

Lot implemente dans cette mission : la provenance `RailConstructionMethod` est
desormais reportee par les colonnes `IsoOffsetRing` afin que les consommateurs
puissent distinguer une bande d'anneau d'une colonne issue de l'axe median.

## US6690988B2 - bitmap vers objets

Signal technique : le document decrit une suite image -> contours/squelette ->
noeuds/chemins -> objets de broderie. Cette famille ressemble davantage au mode
Contours d'OpenStitch qu'au moteur satin lui-meme.

Etat du code : `libs/autodigitize` possede deja la segmentation, l'analyse de
traits, un detail monotone, des metriques et un repli prudent. Depuis la
politique "auto-broderie = tatami/contour, plus d'auto-satin", le code ne doit
pas reintroduire de satin automatique silencieux.

Lot futur : enrichir les diagnostics du mode Contours plutot que reconstruire
un second pipeline bitmap.

## US6397120B1 - singularites editables

Signal technique : l'utilisateur peut choisir ou ajuster l'interpretation d'une
singularite, avec stockage de corrections reutilisables sur des singularites
similaires.

Etat du code : OpenStitch a deja l'edition de guides satin, les guides
directionnels et des diagnostics SGSD, mais pas une UI dediee "choisir une
interpretation de jonction".

Lot futur : inspecteur de jonction uniquement apres stabilisation du moteur de
colonnes. Ne pas construire cette UI au-dessus d'une decomposition SGSD que la
roadmap considere deja fragile.

## US6253695B1 - densite sur points existants

Signal technique : reconnaitre des groupes de points, mesurer leur densite et
ajouter/supprimer/deplacer des lignes proches pour modifier la densite.

Etat du code : OpenStitch genere les points a partir des objets source. Pour un
projet `.osp`, la source de verite reste l'objet, pas les points derives. Cette
idee est donc surtout pertinente pour l'import ou l'analyse de fichiers de
points existants.

Lot futur : post-traitement d'import, jamais mutation silencieuse des sequences
derivees d'un objet OpenStitch.

## US6587745B1 - fill courbe parametrique

Signal technique : mapper une zone courbe vers un espace rectiligne, calculer
un fill standard, puis remapper en espace original.

Etat du code : le remplissage directionnel suit deja un champ de directions avec
guides et ruptures. Ce besoin est plus proche d'un futur "fill courbe/motif" que
du satin squelette.

Lot futur : eventuel remplissage decoratif, separe de HP-STI-018.

## US8219238B2 - generation automatique depuis image

Signal technique : segmentation, fragments, angles et generation automatique
d'objets depuis une image. Le brevet confirme l'importance des metriques de
qualite, de l'elimination du bruit et de la classification des objets.

Etat du code : `autodigitize` et le mode Contours couvrent deja une grande
partie du terrain, avec un curseur `detail`. La roadmap doit surtout garder le
cap : pas de segmentation qui subdivise inutilement les regions, pas de satin
automatique silencieux.

## US6633794B2 - points sous-jacents

Signal technique : detecter des points partiellement ou totalement couverts par
des couches superieures, puis supprimer/remplacer les points redondants.

Garde-fou OpenStitch : ne jamais supprimer automatiquement une sous-couche, un
underpath cache ou une structure de verrouillage seulement parce qu'elle est
visuellement recouverte. Une optimisation acceptable doit distinguer point
decoratif redondant, trajet de structure et sous-couche volontaire.

## Plan de lots retenu

1. **Documentation et tracabilite brevets** : cette page, plus liens roadmap.
2. **Provenance des rails** : reporter `AxisStation` vs `IsoOffsetRing` dans
   les vues de colonnes et tests. Lot code court, sans changement projet.
3. **Suite HP-STI-018** : traiter `extend_tip` / Phase B.5b seulement avec une
   reproduction minimale et le corpus `test_auto_satin`; ne pas faire de
   cutover Phase F avant validation.
4. **Optimisations P2/P3** : uniquement apres un audit dedie des imports et de
   `effective_sequence`.

## Questions ouvertes

- Les statuts juridiques doivent etre verifies hors Google Patents avant toute
  communication externe.
- Le satin `IsoOffsetRing` et le remplissage directionnel restent sans validation
  machine reelle.
- `multi_neck`, `two_holes` et plusieurs formes multi-branches restent des gaps
  connus ; aucune des familles de brevets ne justifie de masquer ces limites.
