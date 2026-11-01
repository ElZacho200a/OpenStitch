# Théorie du tirage du tissu — variables, interactions, critique

Date : 2026-11-01. Contexte : la veille (`veille-tirage-predit-2026-10.md`) ne trouve aucun modèle
publié transférable (ni dans la littérature textile ni dans les outils du marché : Hatch, Wilcom et
Ink/Stitch documentent des réglages, pas des mécanismes ; aucune source lue ne traite l'ordre de
couture). Ce document pose donc une théorie de travail, la critique contre ses propres hypothèses
et dit ce qui est implémenté, ce qui ne l'est pas et comment le mesurer. **Rien ici n'est une
mesure** : toute constante est empirique tant que la pièce-test n'a pas été brodée
(`pull-test-piece`, HP-ENG-001.a).

## 1. Modèle physique retenu

Le tissu + le stabilisateur forment une feuille élastique **cerclée** (bord fixe). Un point de
broderie est un segment de fil sous tension : il applique à la feuille deux forces égales dirigées
l'une vers l'autre, aux deux pénétrations (un dipôle de force le long du point). La réponse linéaire
d'une feuille à un dipôle est le noyau de Green de la feuille ; on l'approche par un noyau
gaussien anisotrope (portées `sigma_along`, `sigma_across`).

Trois effets en découlent, tous observés en pratique (« pull » le long du point, « push » en
travers, déformation de la forme complète) :

1. **Tirage axial** : les deux pénétrations se rapprochent (la colonne s'amincit dans le sens du
   point).
2. **Poussée latérale** : conservation approchée du volume, le tissu bombe de part et d'autre du
   point (effet Poisson), donc les bords d'un remplissage s'écartent.
3. **Ordre** : le tissu bouge pendant la couture. Une pénétration est posée sur le tissu déjà
   déformé par ce qui précède, puis son point suit le tissu. Son décalage final est donc le tirage
   des segments cousus **à partir de lui** (lui compris), pas celui de ceux d'avant. Conséquence :
   le premier point subit tout le champ, le dernier presque rien, et un contour cousu après son
   remplissage reste à sa place alors que le bord du remplissage s'est rétracté : un jour s'ouvre.

## 2. Variables et interactions

| Variable | Effet | Interagit avec | État |
|---|---|---|---|
| Longueur / orientation du point | dipôle, axe du tirage | grain, ordre | modélisé |
| Densité de points (mm de fil / mm²) | cumul du tirage ; raidit le tissu | tassement, sous-couche | modélisé (`compression`, poids de recouvrement) |
| Sens du grain (chaîne / trame) | rigidité différente selon l'angle du point | angle du point, biais | modélisé par segment (axe **et** normale) |
| Ordre et rang de couture | quelle part du tirage s'applique à un point | densité (les points déjà cousus raidissent) | modélisé (`PullSource::order`) |
| Sous-couche | tire aussi, raidit avant le dessus | ordre, densité | partiel (poids 0,55 ; raidit via le recouvrement) |
| Compensation elle-même | déplace les pénétrations donc le champ | ordre | modélisé (point fixe, 3 passes) |
| Matière, stabilisateur | échelle du gain | densité | facteurs empiriques (déjà en place) |
| Type de point (satin / tatami) | poids du segment | longueur | facteur empirique |
| Tension de fil, vitesse, aiguille | amplitude de la force | fil, vitesse | non modélisé |
| Cerclage (taille, tension, distance au bord) | conditions aux limites : borne le déplacement au bord, pré-tension | grain | non modélisé |
| Relaxation (fluage, sortie du cerceau) | le tissu ne revient pas tout à fait | temps de pose | non modélisé |
| Biais (45° du grain) | cisaillement : le tissu cède le plus | grain | non modélisé (seulement chaîne / trame) |

Interactions qui comptent le plus : **densité × ordre** (un remplissage dense déjà cousu raidit la
zone, donc réduit le tirage des couches suivantes), **grain × angle du point** (le même point tire
différemment selon son orientation) et **compensation × champ** (corriger un point change le tirage
qu'il impose aux voisins).

## 3. Critique du modèle

Hypothèses faibles, par impact estimé décroissant :

1. **Linéarité** : un tissu réel se raidit sous compression et sature. Corrigé en partie : tassement
   `1 / (1 + c · recouvrement)` et plafond doux `tanh`. Reste empirique ; la forme de la saturation
   n'est pas validée.
2. **Raidissement par l'historique** : on utilise le recouvrement *final* de tous les segments, pas
   le raidissement au moment de chaque couture. Approximation : surestime le raidissement des
   premières couches, le sous-estime pour les dernières.
3. **Bords du cerceau** : le noyau est infini, le vrai champ s'annule au cerceau. Négligeable pour
   un motif petit et central, faux pour un grand motif.
4. **Identifiabilité** : `gain`, `compression`, `sigma`, `poisson` et `compensation_strength` sont
   couplés ; une seule mesure ne les sépare pas. La pièce-test isole une variable par élément.
5. **Compensation partielle** : `compensation_strength × confiance` n'est pas 1. Le point fixe
   corrige la géométrie mais pas cette inexactitude ; à recaler sur mesure.
6. **Biais, relaxation, tension** : absents ; ils agiraient sur l'amplitude plus que sur la forme.
7. **Convention de signe de la simulation** : l'ancienne `simulate_fabric_pull` décalait les blocs
   suivants dans le même sens que le tissu. Le bon sens est l'inverse (le remplissage se rétracte,
   le contour cousu après reste en place : jour). Corrigé ; à confirmer par la pièce-test
   (`order_*_gap`).

Prédictions réfutables (si la mesure les contredit, le modèle est faux) :
- Le rétrécissement relatif d'une colonne de satin est de l'ordre du gain (quelques %) et ne
  croît pas plus vite que sa largeur (saturation) ; un rétrécissement relatif nettement plus fort
  sur les colonnes larges infirmerait le tassement.
- Les colonnes à 0° et 90° du grain se déforment différemment à anisotropie ≠ 1 ; à 45°, entre
  les deux.
- Le jour entre remplissage et contour est positif quand le contour est cousu après, nettement
  plus petit quand il est cousu avant.
- Les croix de repérage (cousues en premier) mesurent le tirage global : la portée transversale
  (trame) diffère de la portée en chaîne si le tissu est anisotrope.

## 4. Ce que cette PR implémente

- `PullField` : souplesse selon l'angle de chaque segment (axe et normale), tassement, plafond doux,
  rang de couture (`PullSource::order`, `sample/compensation_at(p, min_order)`), raidissement par
  tous les segments voisins.
- `apply_predicted_pull` : compensation à point fixe (`compensation_passes`, 3 par défaut) qui
  décale chaque zone du tirage subi au moment de sa couture, recalculé sur la géométrie compensée.
- Tous les remplissages (contour, motifs, radial, spirale, hachures, point de croix) suivent la
  prédiction dès qu'un objet du projet l'utilise ; point droit, bourdon et motif en ligne exclus.
- Sens du tissu réglable (`project.fabric.grain_angle`, `weft_softness`), `.osp`
  (`grainAngleRad`, `weftSoftness`), dialogue « Coloris et tissu », inspection.
- `simulate_fabric_pull` : même modèle que la compensation (inverse l'un de l'autre) ; correction
  du signe.
- Pièce-test et métriques : `openstitch-cli pull-test-piece`.

## 5. Protocole de calibration (pièce-test)

```text
openstitch-cli pull-test-piece -o piece.dst --metrics mesures.csv
```

1. Broder `piece.dst` sur le tissu réel, X du dessin dans le sens de la chaîne, stabilisateur
   habituel, même cerclage.
2. Mesurer chaque distance du CSV au pied à coulisse (mm), avant de défaire le cerceau puis, si
   possible, après ; remplir `measured_mm`.
3. `pull-test-piece --measured mesures.csv --fit` : écart quadratique moyen prédit/mesuré et
   meilleur triplet (gain, tassement, souplesse de trame). Au moins 6 mesures ; l'ajustement est
   affiché, jamais enregistré, et ne passe pas `calibrated` à vrai.
4. Consigner les mesures et le tissu dans la documentation (HP-PHYS-001) avant d'en tirer une table.

Éléments de la pièce : croix de repérage (tirage global, chaîne / trame), satins de 3 / 5 / 8 /
12 mm, colonnes de 4 mm à 0° / 45° / 90°, tatamis à 3 densités, remplissage + contour dans les deux
ordres. Portées, Poisson et cerceau ne sont pas ajustés : il faudra des pièces plus grandes.
