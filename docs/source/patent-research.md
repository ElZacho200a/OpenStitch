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

- Sources : les fiches ont d'abord été écrites d'après des résumés automatiques de pages Google Patents, puis **les 32 brevets ont été relus sur leur texte primaire** (PDF téléchargés depuis Google Patents ; texte extrait, équations et tableaux abîmés par l'OCR relus sur rendu image ; les deux scans japonais lus visuellement). Les corrections sont dans la section « Errata après lecture des PDF » de chaque fiche et priment sur le corps. Les figures n'ont été regardées que lorsqu'une équation en dépendait. Les PDF ne sont pas versionnés dans le dépôt.
- Toute équation est étiquetée « du brevet » ou « notre proposition ». Plusieurs formules sont **ambiguës ou probablement fautives dans le brevet lui-même** : exposants de distance de recherche de la famille Goldman (rayons énormes, signe de l'exposant de fusion de bifurcation non tranché entre relectures, `c` jamais défini), règle de sélection « B>D » de US6167823B1 (choisit le plus éloigné alors que le texte dit le plus proche), Bézier de US6968255B1 (`/3` final, `m_len` non défini), seuil 20 % / 30 % de US6510360B1, `p,q,r` réutilisés dans US6390005B1. Ne rien implémenter littéralement : redériver et calibrer chez nous.
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

Décision utilisateur (2026-10) : réintroduire un **auto-satin** dans
`autodigitize`, fondé sur ces brevets, parce que l'ancien (retiré par `18de427`)
était de mauvaise qualité. Il ne revient que s'il est **mesuré meilleur que le
repli tatami/contour**, derrière un réglage explicite, avec repli diagnostiqué.

Constat de l'audit et de la revue indépendante (vérifiés dans le code) :

- Le moteur existant couvre déjà l'essentiel des « traversées » : rails et
  barreaux persistés (`SatinParams`), rééchantillonnage à espacement médian
  (`resample_by_medial_spacing`), découpe des longs points (`SatinSplit`), mode
  paramétrique, planificateur SGSD, oracle `satin_coverage`.
- Les éléments du P0 sans source brevet (échantillonnage d'axe, 0° par défaut,
  interpolation mod 180°, `Lmax`/`y`, `h`) sont notre conception et ne sont à
  construire que si un corpus prouve un manque.
- La cause probable du retrait est l'**éligibilité trop permissive** et
  l'acceptation d'une **couverture partielle** (mosaïque satin + pièces de
  tatami), pas l'absence de traversées orientées (voir RD-PAT-000).

Chaîne retenue : (1) banc de comparaison satin vs repli sur régions réelles
(RD-PAT-003) ; (2) critère d'éligibilité strict, extension de l'idée de
statistiques de transformée de distance (RD-PAT-004) ; (3) auto-satin minimal sur
rubans simples via le planificateur existant, satin entier ou repli entier
(RD-PAT-002) ; (4) moteur de traversées orientées seulement si le banc prouve un
manque (RD-PAT-001).

## Priorités (après revue du plan)

1. **P0 : RD-PAT-003**, banc de comparaison ; **RD-PAT-002**, auto-satin minimal
   protégé, derrière **RD-PAT-004** (éligibilité, P1).
2. **P1 conditionnel : RD-PAT-001**, moteur de traversées, si le manque est prouvé.
3. **P2** : critère alternatif d'angle de tatami (US8219238B2, un choix
   automatique existe déjà), analyse des points recouverts (US6633794B2),
   sous-couche en trois cas (JP3922316B2), élagage et ordre de couture par arbre.
4. **Hors objectif directeur** : appliqué (HP-SPEC-001), remplissage polaire,
   stippling.
5. **Écartés** : voir `docs/roadmap-rd-brevets.md` (tableau des raisons).

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
4. **Post-mortem de l'ancien auto-satin** (RD-PAT-000, analyse faite).
5. **Banc de comparaison satin vs repli** (RD-PAT-003), puis éligibilité
   (RD-PAT-004), puis auto-satin minimal protégé (RD-PAT-002).
6. **Moteur de traversées** (RD-PAT-001) seulement si le banc prouve un manque ;
   **P2/P3** : voir la liste de priorités ci-dessus.

## Questions ouvertes

- Statuts juridiques à confirmer hors Google Patents avant toute communication
  externe ; brevets de continuation non examinés.
- Le satin `IsoOffsetRing`, le remplissage directionnel et le futur moteur de
  traversées restent sans validation machine réelle.
- `multi_neck`, `two_holes`, `trident` et plusieurs formes multi-branches
  restent des gaps connus ; aucune famille de brevets ne justifie de masquer
  ces limites.
