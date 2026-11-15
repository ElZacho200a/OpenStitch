# Crosstela : inventaire des points et transposition machine (STCH-CROS-01, L0)

Source : [catalogue Crosstela](https://crosstela.com/fr/learn/stitches/), relevé le 2026-11-15.
Le plan du site (`sitemap.xml`) et l'index listent **362 fiches** (l'estimation de cadrage de
« ~363 » comptait l'index). Matrice complète : [`crosstela-stitch-matrix.csv`](crosstela-stitch-matrix.csv),
régénérée par `python scripts/crosstela_matrix.py`.

**Droits.** La matrice ne garde que des métadonnées factuelles de l'index (identifiant d'URL,
noms, famille, usages, structure, niveau) et l'URL de chaque fiche. Aucun texte, schéma, photo
ou vidéo n'est copié. Classement, correspondance et justification sont une analyse d'OpenStitch.

## Classes

| Classe | Sens | Fiches |
| --- | --- | --- |
| A | couverte par un type existant, tel quel | 13 |
| B | réglage (recette) de types existants | 27 |
| C | primitive ou extension de générateur nécessaire | 140 |
| D | simulation visuelle seulement | 110 |
| E | inadaptée au point noué | 72 |

**Règle de fidélité.** La machine à point noué ne fait que pénétrer le tissu en des points
successifs. Une boucle (chaînette, feston, plume), un nœud (nœud français, bullion), un fil
glissé sous un point sans traverser le tissu (points lacés, enroulés, roues tissées), un fil
métallique couché par un second fil ou un intervalle visible entre deux points (sashiko) ne sont
pas reproductibles mécaniquement : ces fiches sont D, et toute recette qui s'en inspire est
marquée `Approximation`. Les techniques fondées sur un matériau ou une manipulation du tissu
(perles, ruban, smocks, jours tirés, Hardanger, cannetille) sont E.

## Par famille Crosstela

| Famille | A | B | C | D | E | Remarque |
| --- | --- | --- | --- | --- | --- | --- |
| canvas (70) | | 4 | 60 | 4 | 2 | points comptés sur canevas : tuiles de motif sur grille |
| cross (27) | 2 | 1 | 24 | | | variantes de croix : grille à cellules non carrées, croix doubles |
| pulled (26) | | | | | 26 | l'effet vient de la tension |
| chain (22) | | | | 22 | | boucles |
| buttonhole (20) | 1 | | | 19 | | seul le feston simple est la silhouette machine |
| blackwork (20) | | 5 | 15 | | | motifs géométriques : tuiles de motif + routage aller-retour |
| line (17) | 6 | 1 | 4 | 6 | | contours de base couverts |
| satin (15) | 3 | 5 | 5 | 2 | | passé empiétant couvert ; briques et épine à créer |
| couching (15) | | 1 | 12 | 1 | 1 | primitive d'attache |
| autres (130) | 1 | 10 | 20 | 56 | 43 | détail dans la matrice |

## Correspondance avec le moteur (main, 94768773)

Types disponibles (`StitchParams`, `libs/document/include/openstitch/document/embroidery_object.hpp`) :
point droit (Run, Bean, Backstitch, Stem), tatami, satin, remplissage directionnel (guides,
ruptures, dégradés de densité, textures Feathered/Stippled/LongShort, aspect fait main),
auto-satin, feston/zigzag, concentrique, motif le long d'un tracé et en remplissage (Wave,
Zigzag, Leaf, Heart, Diamond, Scallop), radial, spirale, croisé (2 ou 3 couches), point de croix,
appliqué, fondu multicolore (Directional, SatinInterlaced, LongAndShort, ControlledScatter,
CrossHatching). Les préréglages nommés (`document::StitchPreset`) et
`commands::ApplyStitchPresetCommand` existent déjà.

Constats :
- Le croisé (`CrosshatchFillParams`) superpose des couches de rangées sans attache aux
  croisements : un treillis Crosstela n'est donc **pas** couvert (C).
- Le passé empiétant (`long-and-short-stitch`) est couvert par le directionnel + texture
  LongShort et par le fondu `LongAndShort` (A) ; les améliorations visées relèvent d'E19
  (PR #355 en cours, non touchée ici).
- Les intervalles visibles d'un point droit (sashiko, kantha) demanderaient des sauts : D.

## Primitives C, par nombre de fiches débloquées

| Primitive | Fiches typiques | Lot |
| --- | --- | --- |
| Attache aux intersections d'un croisé | treillis (remplissage, couché, jacobéen), damier, créneaux | L2 |
| Phase en briques (quinconce de longueurs) | brick-shading, brick-stitch, gobelins empiétants | L2 |
| Couchage : rangée longue + attaches | Bayeux, Boukhara, roumain, couching | L2 |
| Semis déterministe à exclusion | seeding, dot, granito | L3 |
| Satin à croisement central | fishbone, flat, leaf | L3 |
| Tuiles de motif sur grille (+ aller-retour) | canevas, blackwork, Holbein en remplissage | L3 |
| Rayons vers un centre | œillets algériens, étoile, Rhodes | L3 |

## Recettes pilotes

L1 (types existants, recettes B/A) : running, backstitch, stem, holbein, satin, cross, half-cross,
upright-cross, blanket, darning, split, long-and-short, diamond-lattice.
L2 (primitives) : trellis-filling, bayeux, brick-shading. L3 : seeding, or-nue (simulation).

Architecture : [ADR 0007](../adr/0007-recettes-de-points.md) — catalogue intégré compilé en
`StitchParams`, appliqué par la commande de préréglage existante, sans champ nouveau dans l'OSP.

## Limites de cet inventaire

- Classement par règles (famille, structure) puis exceptions fiche par fiche : les 140 fiches C
  et 110 D non pilotes n'ont pas été examinées une à une ; leur classe est une estimation à
  confirmer quand une primitive les concerne.
- Aucun essai textile. Les recettes « Faithful » le sont au sens de la séquence de pénétrations,
  pas d'un rendu physique vérifié.
- Mesures de référence (temps, mémoire, points, coupes) : prises par recette pilote dans les lots
  L1-L2 avec `openstitch-cli`, pas dans cet inventaire.
