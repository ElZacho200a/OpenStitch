# Recherche brevets broderie

Public : développeur, mainteneur. État : **R&D préliminaire** (mission 2026-10).

Cette page est l'index et la synthèse de la recherche sur 32 références de
brevets de broderie. Les brevets sont traités comme des **sources techniques
historiques**, jamais comme du code, une licence, ni une certification de
liberté d'exploitation. Les fiches détaillées sont dans quatre chapitres :

- [Satin, squelette et jonctions](patent-sheets-satin.md) (EP0761860B1, US6390005B1, US6690988B2, US6397120B1, US6253695B1, US6587745B1)
- [Auto-numérisation](patent-sheets-autodigitize.md) (famille Goldman/SoftSight, Brother, Pulse, Shima Seiki)
- [Sous-couches, contours auto-intersectés, points cachés](patent-sheets-underlay-geometry.md)
- [Stippling, analyse de points, appliqué](patent-sheets-decorative-applique.md)

## Limites de la recherche (à lire d'abord)

- Source unique : pages `patents.google.com/patent/<ID>/en`, lues par un outil
  qui renvoie un **résumé automatique** du texte, pas le texte brut ni les PDF.
  Les figures ne sont connues que par leurs légendes. Les formules rendues en
  image sont absentes ; aucune n'a été reconstituée.
- Toute équation est étiquetée « du brevet » (à revérifier sur le PDF) ou
  « notre proposition ». Plusieurs formules rapportées sont **corrompues ou
  contradictoires** (exposants de distance de recherche d'ancre dans la famille
  Goldman, Bézier de US6968255B1, `p,q,r` réutilisés dans US6390005B1) : ne pas
  les implémenter avant lecture du PDF.
- Les statuts juridiques sont ceux affichés par Google (« hypothèse, pas une
  conclusion juridique »). Des anomalies sont signalées (US8219238B2 :
  taxes payées après l'expiration affichée ; US8532810B2 : deux dates de
  délivrance ; US7386361B2 : deux dates d'expiration). **Aucun avis juridique.**
- Un brevet expiré sert de référence technique ; son expiration n'est pas une
  preuve de liberté d'exploitation mondiale. Les continuations et brevets
  ultérieurs cités (US7457683B2, US6502006B1, US5740055A, US6804573B1,
  US7016756B2, US7587256B2) n'ont **pas** été examinés.
- On ne reproduit ni texte ni figures des brevets : descriptions originales et
  liens seulement.

## Index des 32 références (catégories corrigées)

Les catégories de départ du catalogue étaient souvent inexactes. La colonne
« Réel » donne ce que le brevet traite vraiment. Pertinence : H = haute,
M = moyenne, B = basse pour OpenStitch.

| Brevet | Réel | Pertinence | Famille / remarque |
|---|---|---|---|
| [EP0761860B1](https://patents.google.com/patent/EP0761860B1/en) | axe médian, lignes de division aux branches, orientation perpendiculaire interpolée | M | Shima Seiki ; US6010238A même famille |
| [US6390005B1](https://patents.google.com/patent/US6390005B1/en) | **pas de squelette** : espacement inter-point constant en satin tournant | M (qualité des courbes) | Pulse ; cité par US6587745B1 |
| [US6690988B2](https://patents.google.com/patent/US6690988B2/en) | bitmap → Zhang-Suen → noeuds/chemins ; filtres despike/debow/defork ; double parcours | M | VSM ; apparenté GB2379454 |
| [US6397120B1](https://patents.google.com/patent/US6397120B1/en) | UI d'interprétation des jonctions + base d'apprentissage | B-M | Goldman/Soft Sight, mère 09/134,981 |
| [US6253695B1](https://patents.google.com/patent/US6253695B1/en) | densité d'un groupe de points **existants** | B | même inventeurs que US6247420B1 |
| [US6587745B1](https://patents.google.com/patent/US6587745B1/en) | remplissage à lignes courbes (transformation bilinéaire XY↔UT) | M-H (directionnel) | Wilcom |
| [US6836695B1](https://patents.google.com/patent/US6836695B1/en) | racine de la famille Goldman : pipeline image → points | M | famille G (description commune) |
| [US6947808B2](https://patents.google.com/patent/US6947808B2/en) | classification de points de contour près des noeuds (ancres satin) | M | famille G |
| [US7016757B2](https://patents.google.com/patent/US7016757B2/en) | mince/régulier par statistiques de la transformée de distance | M | famille G |
| [US8219238B2](https://patents.google.com/patent/US8219238B2/en) | **angle de remplissage = moins de fragments** parmi 16 angles | **H** | famille G |
| [US8532810B2](https://patents.google.com/patent/US8532810B2/en) | arbre de fragments récursif et ordre de couture | M-H | famille G ; statut anormal |
| [US9200397B2](https://patents.google.com/patent/US9200397B2/en) | simplification de contour par triangle (générique) | B | famille G ; lapsed |
| [US5740056A](https://patents.google.com/patent/US5740056A/en) | type de point selon l'épaisseur (amincissement) | B | Brother, 1994 |
| [US5576968A](https://patents.google.com/patent/US5576968A/en) | direction/départ d'une région pour machine à 2 sens | B | Brother |
| [US5839380A](https://patents.google.com/patent/US5839380A/en) | découpe interactive d'un bitmap en zones ; fill ou ligne médiane | B-M | Brother |
| [US6356648B1](https://patents.google.com/patent/US6356648B1/en) | élagage de branches et boucles à col sur squelette 1 px | M | Brother |
| [US5283747A](https://patents.google.com/patent/US5283747A/en) | **routage de bâti** sur graphe de sections (pas de numérisation) | M | Brother |
| [US5343401A](https://patents.google.com/patent/US5343401A/en) | lettrage « bridge » (texte sur arc) | B | Pulse |
| [US4849902A](https://patents.google.com/patent/US4849902A/en) | numérisation manuelle assistée (1986), satin par blocs | B | Brother ; lecture partielle |
| [US7386361B2](https://patents.google.com/patent/US7386361B2/en) | dessin au stylet, compensation de courbure, éclairage | B (M si pinceau) | Shima Seiki ; statut contradictoire |
| [US6937919B1](https://patents.google.com/patent/US6937919B1/en) | remplissages **concentrique/radial/elliptique** | M-H | Brother |
| [US6510360B1](https://patents.google.com/patent/US6510360B1/en) | **fichier de points → objets** (rétro-ingénierie) | B-M | VSM ; seuil 20 %/30 % incohérent |
| [US5934209A](https://patents.google.com/patent/US5934209A/en) | contour auto-intersecté → zones fermées + attributs | B | famille Brother « auto-intersecté » |
| [US6247420B1](https://patents.google.com/patent/US6247420B1/en) | points → bitmap → contour (rétro-ingénierie) | B-M | expiré pour taxes |
| [US5957068A](https://patents.google.com/patent/US5957068A/en) | sous-couche par zone (CIP des deux suivants) | B-M | **même famille que US5823127A, US5934209A** |
| [US5823127A](https://patents.google.com/patent/US5823127A/en) | sous-couche et liaisons contenues dans le contour | B-M | équivalent JP3629854B2 |
| [JP3922316B2](https://patents.google.com/patent/JP3922316B2/en) | sous-couche : décision dedans / pont / exclu | M | famille Brother ; traduction automatique |
| [US6633794B2](https://patents.google.com/patent/US6633794B2/en) | suppression de points **cachés** (outil de nettoyage manuel) | B (suppression) / M (analyse) | Bailie ; suite US7457683B2 non vue |
| [US6167823B1](https://patents.google.com/patent/US6167823B1/en) | **analyse de points** par graphes longueur/angle (pas de remplissage) | B-M | Buzz Tools ; continuation US6502006B1 |
| [US6968255B1](https://patents.google.com/patent/US6968255B1/en) | **stippling** : courbe fractale clippée + jitter + Bézier | M | Pulse/Tajima ; texte corrompu |
| [US5438520A](https://patents.google.com/patent/US5438520A/en) | appliqué : coupe, positionnement, bâti, satin | **H** (HP-SPEC-001) | Barudan |
| [JP3769602B2](https://patents.google.com/patent/JP3769602B2/en) | appliqué à une pièce, une saisie → coupe + broderie | **H** | Barudan ; famille US5740055A non lue |

### Familles et doublons

- EP0761860B1 = US6010238A (Shima Seiki).
- Famille Goldman/SoftSight : **six brevets de la liste partagent la même
  description** (priorité 1998-08-17) et ne diffèrent que par leurs
  revendications : US6836695B1, US6947808B2, US7016757B2, US8219238B2,
  US8532810B2, US9200397B2 (plus US6804573, US7016756, US7587256 hors liste).
- Famille Brother « contour auto-intersecté » : US5823127A, US5934209A,
  US5957068A (CIP), JP3922316B2 (équivalents JP3629854B2, JP3760541B2).
- US6510360B1 cite US6247420B1 ; US6253695B1 et US6247420B1 ont les mêmes
  inventeurs (Chan, Leung, Wang).
- 32 références ≈ **19 familles distinctes** : le catalogue surestime la
  diversité.

## Ce que les brevets soutiennent, et ne soutiennent pas

Le moteur « satin guidé par squelette » du brief P0 (axe échantillonné, traversées
orientées, angle par défaut, guides interpolés, `Lmax`, pas `h`) **n'est
contenu dans aucun brevet du corpus** :

| Élément du P0 | Source |
|---|---|
| Squelette/axe médian comme référence | EP0761860B1 rev. 1-3 |
| Orientation ⟂ à l'axe aux extrémités, interpolée entre points de consigne | EP0761860B1 rev. 6-8 (interpolation décrite en mots, forme non précisée) |
| Orientation ∥ à la corde entre intersections de contour au branchement | EP0761860B1 rev. 8 |
| Découpe aux branchements par bissectrices (« closures »), rotation anti-collision | EP0761860B1 rev. 3-5 |
| Contrôle de l'espacement inter-point en courbe | US6390005B1 (mécanisme différent : inset de densité, distance ⟂ au point précédent) |
| Échantillonnage régulier de l'axe, 0° par défaut en squelette complexe, interpolation d'angle mod 180° par guides, `Lmax`/`y`, `h` adaptable | **aucun : notre proposition** |

Conséquence juridique et technique : ces éléments sont notre conception
propre. C'est favorable à la liberté d'exploitation, mais ils ne s'appuient sur
aucune source externe et doivent être validés empiriquement sur machine.

## État réel du code (audit 2026-10, HEAD 8dec3f7)

Voir `docs/roadmap-parite-hatch.md` (HP-STI-018) pour le détail. Résumé factuel
tiré du code, pas de la doc :

- **Existe** : squelette Zhang-Suen + `SkeletonGraph` (`auto_satin`) ;
  échantillonnage de l'axe (`compute_column_stations`, 500 µm) ; intersection
  perpendiculaire à un seul intervalle (`cross_section`) ; découpe des longs
  points (`SatinSplit`, défaut désactivé) ; points courts ; satin tournant par
  anneaux `IsoOffsetRing` (expérimental) ; champ d'angle à guides
  (`directional_fill.cpp`, pondération sur l'angle **doublé**, donc périodique
  mod 180°) ; oracle de couverture (`satin_coverage`).
- **Absent** : aucune fonction angle(s) par clés le long d'un axe ; aucune
  intersection droite/région **multi-intervalle** orientée ; aucun pas `h(s)`
  adaptatif ; aucun lien entre satin et remplissage directionnel ; pas
  d'appliqué ; pas de suppression/analyse de points cachés ; pas de
  modification de densité sur points existants ; pas de remplissage radial,
  concentrique ou spiral.

## Objectif directeur : réintégrer l'auto-satin dans l'auto-numérisation

Décision utilisateur (2026-10) : le but de la mission est de **réintroduire un
auto-satin dans `autodigitize`, fondé sur ces brevets**. La politique actuelle
(« auto-broderie = tatami/contour, pas d'auto-satin silencieux ») ne disparaît
pas : elle est remplacée par un auto-satin **explicite, diagnostiqué et protégé**.
Chaîne cible, avec la source de chaque maillon :

1. **Décider** satin ou remplissage : statistiques de la transformée de distance
   le long du squelette, μ/σ/max (famille Goldman, US7016757B2) ; seuils à
   calibrer sur nos corpus, pas ceux du brevet.
2. **Extraire** squelette et jonctions : déjà en place (`auto_satin`), à
   comparer aux filtres despike/debow/defork (US6690988B2) et aux ancres
   (US6947808B2).
3. **Orienter et générer** : moteur de traversées P0 (EP0761860B1 pour le
   concept, US6390005B1 pour l'espacement, le reste est notre conception).
4. **Prouver** : `satin_coverage` rend un verdict mesurable (couverture, reliquats).
5. **Se replier proprement** : si la couverture est insuffisante, retomber sur
   tatami/contour avec un diagnostic visible (jamais en silence) ; réglage
   utilisateur explicite pour activer l'auto-satin.

## Priorités (hypothèse de travail après recherche, à confirmer en revue)

1. **P0 : moteur de traversées guidé par squelette**, porté par une conception
   propre appuyée sur EP0761860B1 (concept) et US6390005B1 (espacement).
   Sortie matérialisée en rails + barreaux pour réutiliser `fill_satin_columns`,
   finitions, sous-couches, routage ; aucun changement de format `.osp` tant que
   l'API pure n'est pas validée. Le moteur historique reste le défaut. Il
   alimente ensuite l'auto-satin d'`autodigitize` décrit ci-dessus (lot final
   du P0, derrière un réglage explicite et un repli diagnostiqué).
2. **P1 : angle de tatami automatique par minimisation des fragments**
   (US8219238B2). Fonction pure, déterministe, gain net et faible risque.
3. **P1 : règle d'analyse « points entièrement recouverts »** (US6633794B2),
   en lecture seule dans `stitch_analysis`, exemptant sous-couches et trajets
   structurels. Aucune suppression automatique.
4. **P2 : règle de sous-couche en trois cas** (JP3922316B2) et liaisons
   contenues dans le contour (US5823127A), après audit des cas où l'inset
   Clipper2 produit réellement des morceaux sortant de la forme.
5. **P2 : élagage topologique et boucles à col** (US6356648B1, US6690988B2) et
   **ordre de couture par arbre de fragments** (US8532810B2, US5283747A),
   uniquement si l'audit montre un manque par rapport à `SkeletonGraph` et au
   routage existants.
6. **P3 : nouveaux types de remplissage** : appliqué multi-passes (US5438520A,
   JP3769602B2 ; HP-SPEC-001), remplissage polaire concentrique/radial
   (US6937919B1), stippling (US6968255B1 ; HP-STI-013), remplissage curviligne
   (US6587745B1). Utiles produit, indépendants du moteur satin.
7. **Écartés ou différés** : modification de densité sur points existants
   (US6253695B1, les points sont dérivés), lettrage sur arc (US5343401A),
   numérisation manuelle (US4849902A), stylet (US7386361B2), types de point
   rudimentaires (US5740056A, US5576968A), simplification générique
   (US9200397B2), base d'apprentissage de jonctions (US6397120B1), panneau
   d'analyse de points (US6167823B1, faible), import DST → objets
   (US6510360B1, US6247420B1 : hors périmètre, seuils à calibrer par nous).

## Garde-fous

- Satin automatique : jamais silencieux. Activé par un réglage explicite,
  protégé par un verdict de couverture, avec repli tatami/contour diagnostiqué
  (voir « Objectif directeur »).
- Ne jamais supprimer automatiquement une sous-couche ou un trajet structurel
  parce qu'il est visuellement recouvert (US6633794B2).
- Toute implémentation est indépendante, calibrée sur nos corpus ; aucune
  constante de brevet n'est reprise sans vérification et justification.

## Plan de lots

L'ordre définitif est dans `docs/roadmap-parite-hatch.md` (entrées citant ces
brevets) et dans la section R&D complémentaire de cette roadmap.

1. **Documentation et traçabilité brevets** : cette page et ses quatre fiches.
2. **Provenance des rails** : `RailConstructionMethod` (livré).
3. **HP-STI-018 Phase B.5b** : `extend_tip` direction-aware (livré).
4. **P0 moteur de traversées** : lots L1 à L4 (géométrie pure, échantillonnage
   de l'axe et pas `h`, passerelle vers `SatinColumn`, persistance et UI).
5. **P1/P2/P3** : voir la liste de priorités ci-dessus.

## Questions ouvertes

- Statuts juridiques à confirmer hors Google Patents avant toute communication
  externe ; brevets de continuation non examinés.
- Le satin `IsoOffsetRing`, le remplissage directionnel et le futur moteur de
  traversées restent sans validation machine réelle.
- `multi_neck`, `two_holes`, `trident` et plusieurs formes multi-branches
  restent des gaps connus ; aucune famille de brevets ne justifie de masquer
  ces limites.
