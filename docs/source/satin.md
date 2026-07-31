# Colonne satin

Public : utilisateur avancé, développeur.

> État : Présent dans le code : oui · Tests unitaires : oui · Tests visuels :
> partiels · Import/export DST : oui · Test sur machine réelle : **non** ·
> **Statut recommandé : expérimental** — génération satin simple à deux rails.
> Plusieurs propriétés d'un générateur satin correct manquent (voir
> *Limitations* ci-dessous). Vérifiez impérativement le résultat avant tout
> passage sur machine.

## Définition

Une colonne satin est un zigzag serré entre **deux rails** (bords gauche et
droit) qui remplit une bande. Elle donne un effet lisse et brillant, adapté aux
lettrages et aux bordures étroites.

## Modèle

`SatinParams` porte `rail_a` et `rail_b` (deux `geometry::Path`), la densité
(`density`), la compensation de tirage (`pull_compensation`), et l'activation
d'une sous-couche centrale (`center_underlay`).

## Génération

`fill_satin(rail_a, rail_b, config)` :

1. ré-échantillonne **les deux rails** par fraction d'abscisse curviligne (les
   rails peuvent avoir des longueurs et courbures différentes) ;
2. produit un **zigzag alterné** d'un bord à l'autre (L0, R0, L1, R1, …) ;
3. la **densité** fixe le pas d'avancement le long de la colonne (mesuré **le
   long du rail**, par fraction d'abscisse curviligne) ;
4. la **compensation de tirage** écarte les deux rails le long de leur
   médiatrice, pour compenser le resserrement du fil ;
5. la **sous-couche centrale** (optionnelle) est un point droit grossier sur
   l'axe, cousu avant le zigzag.

Avertissement : la densité est mesurée **le long du rail**, pas
perpendiculairement aux fils. Dans les sections **inclinées ou courbes**,
l'espacement visuel réel entre fils diffère alors de la consigne (les fils se
resserrent ou s'écartent selon l'angle). Une mesure correcte projetterait
l'avancement sur la normale aux fils ; ce n'est pas encore le cas.

Le résultat (`SatinResult`) fournit les points de sous-couche, du satin, et la
**largeur maximale** rencontrée (pour l'avertissement).

## Paramètres

Valeurs par défaut lues dans `SatinParams`.

| Paramètre | Unité | Défaut | Effet |
|---|---|---|---|
| `density` | mm | 0,4 | pas d'avancement le long de la colonne (plus petit = plus dense) |
| `pull_compensation` | mm | 0 | élargit la colonne (compense la traction du fil) |
| `center_underlay` | bool | vrai | ajoute une sous-couche centrale (point droit sur l'axe) |
| `max_width` | mm | 9 | seuil d'avertissement de largeur excessive |

> Validation physique : non effectuée. La densité satin devrait idéalement se
> mesurer perpendiculairement au fil (voir l'avertissement ci-dessus).

## Colonne trop large

À la création, si la largeur dépasse le seuil recommandé, l'interface **prévient**
et propose de préférer un remplissage tatami — la limite physique n'est jamais
masquée.

## Rails automatiques

`rails_from_contour(contour)` découpe un contour fermé en deux rails, coupé aux
**deux sommets les plus éloignés** (les « bouts » de la colonne).

**Débordement — désactivé par défaut.** Cette heuristique ne connaît pas l'axe
de la forme : sur un contour **concave ou branchu**, les deux rails traversent
l'intérieur et les barreaux (rungs) enjambent les creux, ce qui fait **sortir
les points de la région**. Mesuré sur un projet réel, jusqu'à **57 % des
barreaux d'une colonne hors de sa région**. En conséquence :

- l'auto-numérisation **n'utilise plus** le satin naïf par défaut ; toute zone
  remplissable devient un **tatami** (découpé au scanline sur la région, donc
  sans débordement). L'option `AutoOptions::use_naive_satin` (défaut `false`)
  permet de le réactiver pour essais/tests ;
- l'action **Broderie ▸ Convertir les satins auto en tatami** répare un projet
  qui contient déjà des satins débordants ;
- le satin reste créable **manuellement** (menu Broderie, ou clic droit ▸ *Type
  de points ▸ Colonne satin*) — à vérifier visuellement, c'est la même
  heuristique de rails.

Limitation : `rails_from_contour` reste une heuristique (utilisée seulement pour
la création **manuelle** de satin). La bonne méthode est le moteur géométrique
ci-dessous.

## Colonnes automatiques par squelette (`build_satin_columns`)

*État : Présent · Testé numériquement · Validé visuellement (SVG) · non validé
simulateur/physique.*

`auto_satin::build_satin_columns(region, params)` construit une ou plusieurs
**colonnes éditables** (`SatinColumnGeometry` : deux rails + **barreaux**) depuis
une région, via le pipeline : rasterisation → transformée de distance → squelette
(Zhang-Suen) → graphe élagué → satinabilité → **sections transversales** →
rails → barreaux → validation.

Pour chaque branche du squelette : l'axe est lissé (Chaikin) et ré-échantillonné
par longueur d'arc ; à chaque station on calcule la tangente puis la **normale**,
on l'intersecte avec le contour et on retient l'**intervalle intérieur encadrant
l'axe** (jamais la bounding box, jamais une association par index). Les deux
extrémités donnent les rails ; la stabilité gauche/droite vient du signe de la
normale (le rail A est toujours à gauche du sens de parcours). Les barreaux sont
posés aux extrémités, aux virages, aux changements de largeur et à intervalle
maximal. Un nettoyage anti-croisement retire les stations qui replieraient la
colonne. Les rails **se terminent sur le contour** → pas de débordement structurel.

Décision : `Suitable` → une colonne (axe principal) ; `RequiresDecomposition`
(Y/T) → une colonne par branche menant à une extrémité ; `Ambiguous` (quasi
circulaire), `Unsuitable` (trou, trop large/étroite) → **refus explicite**.
Déterministe. Vérifié visuellement via `openstitch-cli auto-satin-debug --shape
<forme> --output-svg` (SVG dans `tests/golden/auto-satin/`).

Intégration : **Broderie ▸ Convertir automatiquement en satin…** affiche la
satinabilité (statut, confiance, largeurs, branches, colonnes) puis crée les
objets satin (annulable). Les **barreaux sont stockés** dans `SatinParams.rungs`
et sérialisés (`.osp` schéma v2, rétrocompatible).

## Génération par barreaux (`fill_satin_columns`, Lot 2)

*État : Présent · Testé numériquement · Validé visuellement (SVG) · non validé
simulateur/physique.*

Quand un satin porte des **barreaux** (`SatinParams.rungs` ≥ 2), la génération
utilise `fill_satin_columns` au lieu de `fill_satin` :

- **Correspondance par sections** : chaque paire de barreaux consécutifs découpe
  les rails en intervalles correspondants, interpolés selon **leur propre
  abscisse curviligne** (rails de longueurs, nombres de nœuds et courbures
  différents autorisés). Les barreaux sont **traversés exactement**.
- **Espacement perpendiculaire** : le pas n'est plus mesuré le long d'un rail
  mais sur la **ligne médiane** (≈ perpendiculaire aux fils) — on ré-échantillonne
  la médiane de chaque intervalle par la densité demandée.
- **Séquence** L0, R0, L1, R1, … déterministe (chaque point cousu traverse la
  colonne). Un satin **sans barreaux** (manuel/legacy) retombe sur `fill_satin`.

Vérifié : espacement médian régulier, barreaux exacts, rails de longueurs
différentes, déterminisme (`tests/unit/stitch/test_satin.cpp`) ; zigzag superposé
dans les SVG `tests/golden/auto-satin/`.

## Finitions : points courts, split, terminaisons (Lot 3)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Toutes ces
options sont **désactivées par défaut** (comportement inchangé) et éditables dans
l'inspecteur (section satin) ; elles sont persistées dans le `.osp`.

- **Points courts** (`ShortStitchMode`) : dans un virage serré (le rail intérieur
  avance beaucoup moins que l'extérieur), les pénétrations intérieures sont
  *rentrées* vers l'axe (`SingleInset`, `MultiLevelInset` = motif triangulaire de
  profondeurs) pour ne pas s'entasser sur le bord, ou allégées
  (`RemoveAndRedistribute` : un fil serré sur deux, jamais un barreau, jamais deux
  d'affilée).
- **Split** (`SplitStitchMode`) : une traversée plus longue que `max_stitch_length`
  reçoit des pénétrations intermédiaires. `Simple` (colinéaire régulier),
  `Staggered` (décalage cyclique) et `DeterministicJitter` (variation
  **reproductible**, graine = identifiant de l'objet) évitent une **ligne centrale**
  visible. Jamais d'aléa non déterministe.
- **Terminaisons** (`SatinCapType`) : `Flat` (barreau final), `Rounded` (réduction
  arrondie de largeur), `Tapered` (effilé vers une pointe **sans** empiler sur une
  coordonnée unique — largeur minimale ~18 %), `Automatic`.

Vérifié : inset modifie le rail intérieur en virage, `RemoveAndRedistribute`
réduit les pénétrations, split ajoute des points décalés (staggered ≠ ligne
centrale ; jitter déterministe), taper réduit la largeur au bout sans l'annuler
(SVG `tests/golden/auto-satin/lot3-*.svg` : effilé 5,00 → 0,91 mm).

## Sous-couches et compensation (Lot 4)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Toutes
désactivées par défaut, éditables (inspecteur) et persistées (`.osp`).

**Modèle de passes** (§4) : chaque commande de la séquence porte une `StitchPass`
(`Underlay`, `TopStitch`, `Travel`, `Lock`, `Manual`) — non sérialisée (la
séquence est régénérée), qui permet d'**identifier, filtrer et analyser** les
passes séparément. Les sous-couches sont émises **avant** la couche supérieure.

- **Center walk** : point droit sur la médiane (retrait aux extrémités).
- **Edge walk** : deux chemins internes rentrés des rails (`underlay_edge_inset`).
- **Zigzag underlay** : satin léger (largeur réduite, pas plus grand).
- Ordre : **center → edge → zigzag → satin** (recommandé).

**Compensation** (§11), appliquée à la géométrie des paires avant génération :

- **Pull latérale asymétrique** : décalage par côté = base symétrique
  (`pull_compensation`) + fixe gauche/droite (`pull_left`/`pull_right`) +
  proportionnel à la largeur, borné (`pull_max`).
- **Push longitudinale** : `push_start`/`push_end` étendent (ou rétractent) la
  colonne aux extrémités le long de l'axe.

Vérifié : center/edge/zigzag = 4 passes distinctes ordonnées, pull élargit **un
seul côté** (asymétrique), push étend le bout, déterminisme, aller-retour `.osp`
(SVG `tests/golden/auto-satin/lot4-*.svg` : sous-couches en vert).

Reste (lots suivants) : tatami avancé (Lot 7). Voir
`docs/stitch-feature-gap-audit.md`.

## Entrée/sortie et points de fixation (Lot 5)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Éditables
(inspecteur) et persistés (`.osp`).

**Points d'entrée/sortie** (§12) : `entry_point` et `exit_point` (optionnels)
orientent la couture. Le satin est **retourné** si son extrémité de départ est
plus proche du point de sortie que du point d'entrée (coût = somme des distances
extrémités↔points) — jamais de réordonnancement partiel, seulement le sens. Sans
point, l'orientation issue du générateur est conservée.

**Points de fixation** (`SatinLock`, émis en passe `Lock` uniquement) : ancrent
le fil au **début** et à la **fin** de l'objet, jamais par sous-passe (un lock au
départ, un à l'arrivée). Chaque bout se règle indépendamment
(`lock_start`/`lock_end`), avec longueur (`lock_length`) et nombre de passages
(`lock_passes`) communs.

- **`None`** : aucun point de fixation.
- **`BackAndForth`** : aller-retour court le long du premier/dernier point.
- **`Triangle`** : petit triangle d'ancrage.
- **`MicroZigzag`** : micro-zigzag progressant dans l'axe de couture.

Toutes les formes sont **bornées** (`lock_length`, défaut 0,8 mm) et ancrées à
l'extrémité exacte du satin (continuité : pas de saut parasite grâce à
l'enchaînement de passes à position identique).

Vérifié : `None` → vide ; les autres progressent dans l'axe de couture et restent
bornés ; le point d'entrée fait démarrer la couture à la bonne extrémité ; locks
en passe `Lock`, exactement deux groupes (début/fin) ; aller-retour `.osp` des
champs Lot 5 (SVG `tests/golden/auto-satin/lot5-lock-*.svg` : fixations en rouge).

## Routage multi-colonnes (Lot 6)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Automatique
à la génération ; aucun réglage persisté.

Une forme décomposée (Y, T, croix…) devient **plusieurs colonnes satin** de même
couleur et même source. Sans routage, elles seraient cousues dans l'ordre du
document, avec de longs sauts. §13 les ordonne et les oriente ensemble
(`route_columns`) :

- **Ordre** : glouton plus proche voisin depuis la position courante de
  l'aiguille (on entre par l'extrémité la plus proche), puis amélioration
  **2-opt**. Déterministe.
- **Orientation** : pour un ordre donné, le sens de chaque colonne est résolu
  **exactement** par programmation dynamique sur ses deux extrémités
  (l'orientation choisie est imposée à `generate_satin` via entrée/sortie).
- **Liaisons** : une transition courte (≤ `underpath_max`, défaut 8 mm) est
  cousue en **trajet caché** (passe `Travel`, running stitch — pas de coupe) ;
  au-delà, elle reste un **saut**. Minimise les coupes et les déplacements à
  découvert.

Le groupe routé est **contigu** et limité aux colonnes auto (porteuses de
barreaux) de couleur et source identiques : l'ordre inter-groupes et le reste du
document sont préservés. Un satin manuel isolé n'est pas réordonné.

Vérifié : liste vide → plan vide ; une colonne → liaison de départ, aucun saut ;
réordonnancement minimisant le déplacement ; orientation par l'extrémité proche ;
liaison longue → saut, liaison courte → trajet caché ; à la génération, un groupe
adjacent n'émet qu'un saut initial (le reste cousu), un groupe éloigné conserve
ses sauts (SVG `tests/golden/auto-satin/lot6-route-*.svg` : trajets cachés en
bleu, sauts en rouge pointillé).

Limite : le trajet caché est un segment **direct** (échantillonné en points
cousus) ; il n'est garanti *sous* la broderie que pour des colonnes adjacentes
(le seuil `underpath_max` bascule les liaisons douteuses en sauts). Un routage
suivant réellement la matière viendra avec le tatami avancé (Lot 7).

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — `SatinParams`, `SatinRung`.
- `libs/stitch_generation/src/satin.cpp` — `fill_satin`, `fill_satin_columns`
  (par barreaux), `rails_from_contour`.
- `libs/stitch_generation/src/generate.cpp` — `generate_satin` (route vers
  `fill_satin_columns` si barreaux présents, oriente par entrée/sortie, émet les
  locks).
- `libs/stitch_generation/src/lock.cpp` — `lock_stitches`, `LockType` (Lot 5).
- `libs/stitch_generation/src/routing.cpp` — `route_columns`, `RoutePlan`
  (ordre/orientation/liaisons, Lot 6) ; `generate_satin_group` dans
  `generate.cpp` (émission des groupes routés).
- `libs/auto_satin/.../satin_column.hpp` + `src/satin_column.cpp` —
  `build_satin_columns`, `SatinColumnGeometry`, `SatinRung` (moteur géométrique).
- `libs/auto_satin/src/debug_export.cpp` — `columns_to_svg`.
- Tests : `tests/unit/stitch/test_satin.cpp`,
  `tests/unit/auto_satin/test_columns.cpp`.
