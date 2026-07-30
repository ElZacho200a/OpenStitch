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
| Tatami | **Partiel** | Broderie | stitch_generation | oui | pas de sous-couche, pas d'underpath caché, entrée/sortie non gérées ; orientation éditable |
| Satin (génération par barreaux) | Présent · testé · SVG | Broderie / inspecteur | stitch_generation | oui | `fill_satin_columns` : sections, espacement **perpendiculaire**, points courts / split / terminaisons (Lot 3), **sous-couches (center/edge/zigzag) + compensation pull/push** (Lot 4, passes distinctes) ; lock (Lot 5), routage (Lot 6) à venir |
| Modèle de passes | Présent · testé | (générateur) | stitch | oui | `StitchPass` par commande (Underlay/TopStitch/Travel/Lock/Manual) ; affichage/toggle par passe dans l'UI à venir |
| Auto-satin géométrique (rails+barreaux) | Présent · testé · SVG | Broderie ▸ Convertir en satin | auto_satin | oui | `build_satin_columns` : formes simples + Y ; cercle/anneau/large refusés |
| Classification auto des régions | **Expérimental** | Segmentation | autodigitize | oui | zones remplissables → **tatami** (satin naïf désactivé, `use_naive_satin`) ; heuristique, pas une vraie auto-numérisation |
| Filtres d'affichage / calques | Implémenté | Affichage | desktop | (vue) | affichage seulement (couleur, type, taille ; image/régions/vecteurs/broderie) |
| Ordre de couture | Implémenté | dock | optimization | oui | 2-opt non implémenté |
| Analyse | Implémenté | Analyse | stitch_analysis | oui | pas de carte de densité |
| Simulation | Implémenté | barre | desktop | — | pas de réglage de vitesse |
| Export/Import DST | Implémenté | Fichier/CLI | formats | oui | limites du format |
| Export SVG diagnostic | Implémenté | CLI | formats | oui | — |
| Format projet `.osp` | Implémenté | Fichier | project_io | oui | suivi « modifié » + garde à la fermeture ; pas d'autosave |
| Cadre de broderie | Implémenté | Affichage | document | oui | taille réglable et persistée ; rectangle simple (pas de profils/formes) |
| Palette de fils | Non implémenté | — | (thread_palette absent) | — | RGB par objet uniquement |
| Édition manuelle des points | Non implémenté | — | — | — | régénération uniquement |
| Remplissages courbe/radial/spirale/motif | Non implémenté | — | — | — | prévus |
| Profils machine/cadres avancés | Non implémenté | — | — | — | cadre = rectangle simple (taille réglable) |
| Compensation directionnelle | Partiel | — | — | — | satin uniquement |
| Tâches asynchrones | Non implémenté | — | — | — | traitements synchrones |

## Dette technique connue

- `README.md` mentionne un compte de tests d'un jalon antérieur ; le compte réel
  est **190** — à resynchroniser dans le README.
- Le satin dispose désormais d'un moteur géométrique par **squelette**
  (`auto_satin::build_satin_columns`, Lot 1) et d'une **génération par barreaux**
  (`fill_satin_columns`, Lot 2), points courts / split / terminaisons (Lot 3) et
  **sous-couches (center/edge/zigzag) + compensation pull/push** (Lot 4, passes
  distinctes), **points d'entrée/sortie + points de fixation (lock)** (Lot 5).
  Reste le routage multi-colonnes (Lot 6). Le satin **auto naïf** reste désactivé.
  Le **tatami** reste une version de base (sous-couches/underpath à venir).
- Aucune validation sur machine à broder réelle.

## Niveaux de validation

« Implémenté » signifie ici *présent et testé logiciellement* — pas *prêt pour
la production*. Pour un logiciel de broderie, il faut distinguer :

| Niveau | Signification | État du projet |
|---|---|---|
| Présent | le code existe | oui (pipeline complet) |
| Testé numériquement | les invariants logiciels passent | oui (190 tests) |
| Validé visuellement | les trajectoires paraissent cohérentes | partiel (SVG de diagnostic, aperçu) |
| Validé sur simulateur | vérifié dans un visualiseur tiers | non |
| Validé physiquement | broderies réelles examinées | **non** |

Un générateur peut passer 190 tests sans produire une bonne broderie : les tests
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
