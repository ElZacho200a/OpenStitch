# Limitations et état des fonctionnalités

Public : tous. Ce chapitre distingue explicitement l'état de chaque
fonctionnalité, vérifié dans le code.

## Tableau des fonctionnalités

| Fonctionnalité | État | Interface | Module | Tests | Limitations |
|---|---|---|---|---|---|
| Import PNG/JPEG/BMP/TIFF | Implémenté | Fichier | image | oui | — |
| Prétraitement non destructif | Implémenté | Image | image | oui | pas de resize |
| Segmentation CIELAB + régions | Implémenté | Segmentation | segmentation | oui | 4-connexité |
| Édition de régions | Implémenté | Segmentation | segmentation | oui | — |
| Vectorisation | Implémenté | Segmentation | vectorization | oui | — |
| Formes dessinées à la main (rectangle/ellipse/polygone) | Présent · testé | palette d'outils | geometry/desktop | QTest | pas de capture d'écran manuelle possible dans cet environnement de dev |
| Édition de nœuds | Partiel | canevas | document | — | déplacement seul |
| Édition d'objets broderie | Implémenté | inspecteur/clic droit | commands | oui | changer le type et **tous les paramètres** après création ; orientation à la poignée |
| Interface (thème, panneaux, workflow) | Implémenté | desktop | desktop | (vue) | thème clair/sombre + densité, inspecteur, panneau Document, workflow, état d'accueil, persistance UI (QSettings) |
| Point droit/double/triple | Implémenté | Broderie | stitch_generation | oui | — |
| Tatami | Présent · testé · SVG | Broderie | stitch_generation | oui | scanline + routage ; **sous-couches (contour + parallèle), underpath caché, entrée** (Lot 7) ; orientation éditable |
| Satin (génération par barreaux) | Présent · testé · SVG | Broderie / inspecteur | stitch_generation | oui | `fill_satin_columns` : sections, espacement **perpendiculaire**, guides sélectionnables/ajoutables/supprimables/déplaçables avec undo/redo, points courts / split / terminaisons (Lot 3), **sous-couches (center/edge/zigzag) + compensation pull/push** (Lot 4, passes distinctes), **lock + entrée/sortie** (Lot 5), **routage multi-colonnes** (Lot 6) |
| Modèle de passes | Présent · testé | (générateur) | stitch | oui | `StitchPass` par commande (Underlay/TopStitch/Travel/Lock/Manual) ; affichage/toggle par passe dans l'UI à venir |
| Auto-satin géométrique (rails+barreaux) | Présent · testé · SVG | Auto + Broderie ▸ Convertir en satin | auto_satin | oui | formes simples, Y/T multi-sections et anneau fin en 4 sections ; **bouts ouverts étendus jusqu'au bord réel** et **bouts de jonction ancrés sur les sommets reflex du contour** (mission « auto-satin béton ») ; cercle plein/forme large refusés ; croix à 4 branches : topologie de squelette incorrecte (défaut distinct, non corrigé) |
| Classification auto des régions | **Expérimental** | Segmentation | autodigitize | oui | bandes fines → moteur topologique par défaut (`use_auto_satin`) ; refus → tatami ; moteur naïf désactivé |
| Filtres d'affichage / calques | Implémenté | Affichage | desktop | (vue) | affichage seulement (couleur, type, taille ; image/régions/vecteurs/broderie) |
| Ordre de couture | Implémenté | dock | optimization | oui | 2-opt non implémenté |
| Analyse | Implémenté | Analyse | stitch_analysis | oui | pas de carte de densité |
| Simulation | Implémenté | barre | desktop | — | pas de réglage de vitesse |
| Export/Import DST | Implémenté | Fichier/CLI | formats | oui | limites du format |
| Export SVG diagnostic | Implémenté | CLI | formats | oui | — |
| Format projet `.osp` | Implémenté | Fichier | project_io | oui | suivi « modifié » + garde à la fermeture ; pas d'autosave |
| Cadre de broderie | Implémenté | Affichage | document | oui | taille réglable et persistée ; rectangle simple (pas de profils/formes) |
| Palette de fils | Non implémenté | — | (thread_palette absent) | — | RGB par objet uniquement |
| Édition manuelle des points | Partiel (Lot 8.2) | canevas | desktop/commands | QTest | déplacement d'un point + undo/redo ; Stitch/Jump/Trim UI restent à faire |
| Remplissages courbe/radial/spirale/motif | Non implémenté | — | — | — | prévus |
| Profils machine/cadres avancés | Non implémenté | — | — | — | cadre = rectangle simple (taille réglable) |
| Compensation directionnelle | Partiel | — | — | — | satin uniquement |
| Tâches asynchrones | Non implémenté | — | — | — | traitements synchrones |

## Dette technique connue

- Le compte CTest courant est **383** en Debug et Release ; éviter de figer ce
  nombre dans les pages d'introduction sans le mettre à jour avec la CI.
- Le satin dispose désormais d'un moteur géométrique par **squelette**
  (`auto_satin::build_satin_columns`, Lot 1) et d'une **génération par barreaux**
  (`fill_satin_columns`, Lot 2), points courts / split / terminaisons (Lot 3) et
  **sous-couches (center/edge/zigzag) + compensation pull/push** (Lot 4, passes
  distinctes), **points d'entrée/sortie + points de fixation (lock)** (Lot 5) et
  **routage multi-colonnes** (Lot 6 : ordre/orientation + trajets cachés). Le
  **tatami** reçoit ses **sous-couches (contour + parallèle), l'underpath caché et
  le point d'entrée** (Lot 7). Le satin **auto naïf** reste désactivé ; le vrai
  moteur topologique est désormais celui essayé par défaut dans `autodigitize`.
- Les sections d'un réseau satin portent maintenant des indices et identifiants
  de jonction explicites, déterministes et persistés. Le routage privilégie ces
  jonctions quand l'écart reste compatible avec un trajet caché. La collecte
  déterministe des extrémités, la transaction multi-section atomique et l'ajout
  UI d'un guide interne dans chaque branche incidente sont présents. Les guides
  ainsi liés (`link_id`, scopé par `(source_vector, link_id)`) se déplacent et se
  suppriment désormais en **groupe atomique** : Maj+glisser une extrémité déplace
  toute l'identité logique (delta normalisé partagé, jamais de coordonnée/angle
  recopié entre sections) et supprimer un guide lié supprime le groupe entier,
  chacun via une seule commande undo/redo tout-ou-rien (`move_satin_guide_group`,
  `RemoveSatinGuidesCommand`). Un glisser SANS Maj reste un geste local (édition
  d'angle propre à une section, toujours possible et volontairement non propagé).
  Un groupe incomplet, associé à des identifiants de jonction différents ou dont
  la progression se croise entre les deux rails est refusé en bloc. Un Maj+clic
  sans déplacement ne crée aucune commande d'historique.
  Les guides terminaux de jonction restent verrouillés (ni déplaçables ni
  supprimables). Un guide lié qui perdrait son adjacence à sa jonction (un autre
  guide inséré entre les deux) refuse le geste de groupe plutôt que de deviner un
  intervalle. De plus, un retour textile vers une jonction reste rectiligne au
  lieu de retracer le centre d'une branche.
- **Correctif Lot 7** : `connector_invalid` ignorait les contacts sommet/extrémité
  et ne sondait l'intérieur d'un connecteur que si l'écart en x dépassait
  `2 × row_spacing`, laissant passer un connecteur quasi vertical traversant un
  trou de part en part en touchant exactement ses sommets. Corrigé par une
  découpe paramétrique du segment à chaque intersection (`segment_stays_in_region`
  exposé pour test). `tatami_underlay` retombait aussi silencieusement sur le
  bord **brut** si le retrait de contour échouait/disparaissait ; politique sûre
  désormais : aucune sous-couche de contour dans ce cas. `underlay_inset` et
  `underlay_spacing` sont exposés dans l'inspecteur (`PropertiesPanel`).
- **Correctif « auto-satin béton » — extension des bouts ouverts** : un bout de
  colonne sans jonction s'arrêtait, par artefact générique de l'amincissement
  (Zhang-Suen), sensiblement avant le bord réel — un embout arrondi restait
  entièrement hors couture (~11 % de la longueur d'une capsule de test), un bout
  carré retractait aussi (~2,55 mm). Corrigé (`extend_tip`, activé par défaut) :
  marche le long de la tangente sortante en ré-évaluant des sections
  transversales réelles tant qu'elles rétrécissent, fermeture par bissection
  (largeur plancher non nulle). Voir `docs/source/satin.md` § *Extension des
  bouts ouverts*, sources (brevet Pulse Microsystems, littérature d'élagage de
  squelette) citées dans ce même paragraphe.
- **Correctif « auto-satin béton » — ancrage des jonctions** : le défaut
  inverse du précédent. La section transversale d'une branche, calculée depuis
  sa seule tangente, dérive nettement en approchant d'une jonction (jusqu'à
  ~9,2 mm mesuré sur "y" contre ~5,0 mm nominal) car le rayon balaie le
  bourrelet de la confluence, pas la ceinture réelle de la branche — les
  sections d'un Y ne se rejoignaient pas au même point (jusqu'à ~5 mm d'écart).
  Corrigé (`trim_and_anchor_junction_end`, activé par défaut) : amputation de
  la queue instable (croissance > 10 % par rapport à la station voisine) puis
  ancrage indépendant de chaque rail sur le sommet reflex (concave) du contour
  le plus proche, avec exclusion mutuelle si les deux rails d'une branche
  convoitent le même sommet (défaut réel trouvé sur "T", corrigé avant commit).
  Voir `docs/source/satin.md` § *Ancrage des jonctions sur les sommets reflex du
  contour*. Limite connue non corrigée : la forme "croix" (4 branches) a une
  topologie de squelette incorrecte (nœud de degré 2 au lieu de degré 4),
  défaut distinct situé dans `skeleton_graph.cpp`.
- **Correctif « auto-satin béton » (suite) — trajet caché non validé** :
  `route_columns` décidait un trajet caché (`ConnectorKind::Underpath`, fil
  caché sous la broderie, sans coupe) sur la seule distance (`gap ≤
  underpath_max`, 8 mm), sans jamais regarder si `step.junction` — déjà
  calculé juste au-dessus pour ce même pas — avait une valeur : deux colonnes
  satin géométriquement proches mais **sans aucun lien topologique réel**
  (deux lettres rapprochées, une forme en C dont les deux bouts se frôlent
  sans être reliés) pouvaient donc être cousues en ligne droite à travers un
  espace dont rien ne garantissait qu'il était couvert de tissu. Corrigé par
  un seuil à deux paliers : `underpath_max` (8 mm, inchangé) ne s'applique
  plus que si `step.junction` a une valeur — désormais fiable grâce à
  l'ancrage exact des jonctions ci-dessus — sinon un nouveau seuil, bien plus
  strict, `underpath_max_without_junction` (1,5 mm) ne tolère qu'un
  quasi-contact (arrondi de rastérisation, extrémités coïncidentes) ; au-delà,
  la liaison redevient un saut. Voir `docs/source/satin.md` § *Routage
  multi-colonnes*.
- **Formes dessinées à la main (rectangle/ellipse/polygone)** : demande
  utilisateur en cours de mission « auto-satin béton » — jusqu'ici, la seule
  façon d'obtenir un `VectorObject` était de vectoriser une région depuis une
  image importée, comme dans un logiciel de digitalisation classique (Hatch et
  équivalents) qui permet aussi de dessiner directement une forme de base.
  Trois outils dans la palette (rectangle glissé, ellipse glissée avec Maj =
  cercle, polygone à clics successifs fermé par double-clic) créent un
  `VectorObject` sans région source, immédiatement sélectionné : l'utilisateur
  enchaîne avec les actions **Créer un…** existantes, sans nouveau chemin côté
  broderie. Voir `docs/source/vectorization.md` § *Formes dessinées à la main*.
  Testé par QTest (création, undo/redo, cadre dégénéré, tracé de polygone,
  annulation) ; **pas de validation visuelle manuelle** dans cet environnement
  de développement (capture d'écran de l'application non disponible).
- **Correction de l'appariement rail gauche/rail droit (2026-08-01)** :
  l'ancien appariement (`fill_satin` sans barreaux, et l'interpolation
  intra-intervalle de `fill_satin_columns`) associait les deux rails par la
  même fraction d'abscisse curviligne appliquée indépendamment à chacun —
  faux dès que les rails divergent en longueur/courbure (virage, coude,
  largeur variable), causant éventails, quasi-croisements et densité
  irrégulière sur un ruban courbe ou anguleux. Remplacé par une
  correspondance locale monotone (« ladder », diagonale la plus courte,
  garde-fou anti-croisement, O(n) par intervalle) — voir
  `docs/source/satin.md` § *Correction de l'appariement*. Limite assumée : au
  sommet d'un coude C0 franc (sans congé), un seul fil absorbe nécessairement
  une déviation angulaire importante (mitre/congé = `ShortStitchMode`, hors
  périmètre). Fixtures et métriques dédiées :
  `tests/unit/stitch/test_satin_pairing_metrics.cpp` (13 tests).
- **Revue corrective ladder_correspondence, audit adverse (2026-08-01)** :
  fixtures délibérément défavorables (rails tête-bêche, échantillonnage très
  asymétrique/segments nuls, longueurs très différentes + largeur quasi
  nulle, épingle à cheveux ~170°, barreaux désordonnés/dupliqués). Deux
  défauts réels corrigés : rails fournis tête-bêche (précondition non
  vérifiée, nœud papillon en O(n²)) → détection + ré-orientation interne
  automatique ; barreaux dépendants de l'ordre du vecteur d'entrée (un
  barreau en tête mais loin le long de la colonne faisait rejeter
  silencieusement tous les suivants, repli sans barreaux) → tri par position
  projetée avant filtrage, plus fusion des barreaux quasi-dupliqués (source
  d'un croisement isolé près d'un virage serré, faute de garde-fou entre deux
  intervalles voisins). Voir `docs/source/satin.md` § *Revue corrective
  (audit adverse)*. Aucun chemin de production actuel n'atteint le cas
  tête-bêche (`rails_from_contour`/`build_satin_columns` garantissent déjà le
  même sens). L'UI édite désormais les barreaux, mais maintient leur progression
  monotone et leur écart minimal ; les imports désordonnés restent acceptés et
  triés à la génération. Ces gardes défensives restent nécessaires à la
  fonction de bibliothèque publique.
- Aucune validation sur machine à broder réelle.

## Niveaux de validation

« Implémenté » signifie ici *présent et testé logiciellement* — pas *prêt pour
la production*. Pour un logiciel de broderie, il faut distinguer :

| Niveau | Signification | État du projet |
|---|---|---|
| Présent | le code existe | oui (pipeline complet) |
| Testé numériquement | les invariants logiciels passent | oui (217 tests) |
| Validé visuellement | les trajectoires paraissent cohérentes | partiel (SVG de diagnostic, aperçu) |
| Validé sur simulateur | vérifié dans un visualiseur tiers | non |
| Validé physiquement | broderies réelles examinées | **non** |

Un générateur peut passer 217 tests sans produire une bonne broderie : les tests
vérifient des **invariants** (pas de couture dans un trou, espacement par
longueur d'arc, aller-retour DST exact…), pas la **qualité textile**.

## Statut de validation

Le logiciel constitue un **prototype fonctionnel** couvrant l'ensemble du
pipeline principal. Ses composants sont **testés logiciellement** et vérifiés
**visuellement** (SVG de diagnostic), mais la **qualité de broderie produite
n'est pas encore validée sur machine réelle**. Ne pas considérer les résultats
comme prêts pour la production sans essais machine.

## Implémentation associée

Voir chaque chapitre thématique et l'annexe *Audit du dépôt*.
