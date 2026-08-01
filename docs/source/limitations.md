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
| Édition de nœuds | Partiel | canevas | document | — | déplacement seul |
| Édition d'objets broderie | Implémenté | inspecteur/clic droit | commands | oui | changer le type et **tous les paramètres** après création ; orientation à la poignée |
| Interface (thème, panneaux, workflow) | Implémenté | desktop | desktop | (vue) | thème clair/sombre + densité, inspecteur, panneau Document, workflow, état d'accueil, persistance UI (QSettings) |
| Point droit/double/triple | Implémenté | Broderie | stitch_generation | oui | — |
| Tatami | Présent · testé · SVG | Broderie | stitch_generation | oui | scanline + routage ; **sous-couches (contour + parallèle), underpath caché, entrée** (Lot 7) ; orientation éditable |
| Satin (génération par barreaux) | Présent · testé · SVG | Broderie / inspecteur | stitch_generation | oui | `fill_satin_columns` : sections, espacement **perpendiculaire**, points courts / split / terminaisons (Lot 3), **sous-couches (center/edge/zigzag) + compensation pull/push** (Lot 4, passes distinctes), **lock + entrée/sortie** (Lot 5), **routage multi-colonnes** (Lot 6) |
| Modèle de passes | Présent · testé | (générateur) | stitch | oui | `StitchPass` par commande (Underlay/TopStitch/Travel/Lock/Manual) ; affichage/toggle par passe dans l'UI à venir |
| Auto-satin géométrique (rails+barreaux) | Présent · testé · SVG | Auto + Broderie ▸ Convertir en satin | auto_satin | oui | formes simples, Y/T multi-sections et anneau fin en 4 sections ; cercle plein/forme large refusés |
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

- Le compte CTest courant est **323** en Debug et Release ; éviter de figer ce
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
- **Correctif Lot 7** : `connector_invalid` ignorait les contacts sommet/extrémité
  et ne sondait l'intérieur d'un connecteur que si l'écart en x dépassait
  `2 × row_spacing`, laissant passer un connecteur quasi vertical traversant un
  trou de part en part en touchant exactement ses sommets. Corrigé par une
  découpe paramétrique du segment à chaque intersection (`segment_stays_in_region`
  exposé pour test). `tatami_underlay` retombait aussi silencieusement sur le
  bord **brut** si le retrait de contour échouait/disparaissait ; politique sûre
  désormais : aucune sous-couche de contour dans ce cas. `underlay_inset` et
  `underlay_spacing` sont exposés dans l'inspecteur (`PropertiesPanel`).
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
  même sens) ni les barreaux désordonnés (aucune UI d'édition de barreaux) —
  corrections préventives sur une fonction de bibliothèque publique.
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
