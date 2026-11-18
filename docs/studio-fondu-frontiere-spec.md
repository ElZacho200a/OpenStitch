# Fondu à la frontière entre deux zones — spécification

Statut : **lots L1a à L5 livrés** (2026-11-17), recette Studio réelle et essai sur tissu à faire. Rattachement roadmap : HP-STI-011 (mélange de couleurs), dépend de
HP-STI-010 (dégradé de densité).

- 2026-11-16, **L1a livré** (branche `feat/fondu-frontiere-l1a`) : `RowMask`,
  `DensityRowMask`, `row_threshold(k)` (`stitch_generation/row_mask.hpp`) et
  `fill_tatami(region, params, const RowMask*)`. Grille ancrée, découpe aux pénétrations,
  liaison à la prochaine rangée gardée (12 rangées au plus). Aucun appelant ne passe encore de
  masque : la génération des projets est inchangée. Tests `[row_mask]` dans
  `tests/unit/stitch/test_row_mask.cpp` : masque nul identique octet pour octet, masque plein
  sur une région alignée identique au tatami actuel, partition exacte de deux masques
  complémentaires, rangées communes entre deux régions différentes, rangées éclaircies reliées
  sans saut supplémentaire, déterminisme.
- 2026-11-16, **L1b livré pour le tatami** (branche `feat/fondu-frontiere-l1b`, empilée sur
  L1a) : `geometry::shared_frontier` et `geometry::polyline_band` (`geometry/frontier.hpp`),
  `BoundaryBlendField`, `plan_boundary_blend` et `fill_boundary_blend_tatami`
  (`stitch_generation/boundary_blend.hpp`). Codes `E_BLEND_NO_FRONTIER`,
  `E_BLEND_WIDTH_RANGE`. Toujours rien de branché dans la génération des projets (le lien du
  document arrive en L2). Tests `[frontier]` (`tests/unit/geometry/test_frontier.cpp`) et
  `[boundary_blend]` (`tests/unit/stitch/test_boundary_blend.cpp`) : rampe de densité, régions
  prolongées, partition exacte à grille commune (écart < 1 % par bande de 1 mm), superposition
  à angles croisés (85–115 % d'un aplat), frontière oblique, refus codés, déterminisme.
- Trois ajustements trouvés en testant L1b :
  1. **Amorces de frontière** : la tolérance ajoute, au bout de chaque frontière ouverte, un
     bout du contour extérieur (là où le bord tourne mais reste près de l'autre zone). La bande
     gonflait vers les coins. Chaque bout est donc rogné de la tolérance.
  2. **Décision par case de la grille des pénétrations** (et non au milieu de chaque
     intervalle) : une zone prolongée commence au bord de la bande, au milieu d'une case ; avec
     une décision au milieu de l'intervalle, les deux couches échantillonnaient la densité en
     des points différents et se chevauchaient ou laissaient des manques.
  3. **Marge de prolongation** d'une longueur de point au-delà de la demi-bande : une case à
     cheval sur le bord de bande doit exister dans les deux couches. Conséquence : une zone peut
     coudre jusqu'à une demi-case (1,5 mm pour des points de 3 mm) au-delà de la demi-bande de
     l'autre, aux rares rangées dont le seuil est sous la densité résiduelle.
- Limite connue : la lisière se découpe par cases de `stitch_length` (3 mm par défaut) le long
  des rangées ; sur une bande de 5 mm, le dégradé vient surtout de l'alternance des rangées.
  Des points plus courts dans la bande sont une piste pour la recette physique.
- Le directionnel (géométrie `Boundary`, grille ancrée) devient un lot à part, **L1c**, et le
  SVG doré passe en L2 (il lui faut l'opération CLI).
- 2026-11-16, **L1c livré** (branche `feat/fondu-frontiere-l1c`, empilée sur L1b) :
  `fill_directional(region, params, const RowMask*)` et `fill_boundary_blend` (variante
  tatami ou directionnel de chaque côté). Les lignes de courant du directionnel dépendent de la
  région : deux régions différentes ne partagent jamais leurs lignes, donc un fondu qui touche
  un directionnel est **toujours en superposition** (couverture ≈ d_A + d_B). Seuil d'une
  ligne : rang le long de la normale à la direction moyenne des lignes, suite de Weyl. Le masque
  remplace la grille partagée existante ; liaisons courtes, refusées là où le fil couvre moins
  de 20 % des rangées (`RowMask::coverage`). Pas de `DensityGeometry::Boundary` : le masque vit
  à côté du `DensityGradient`, qui reste inchangé. Une zone directionnelle déjà en dégradé de
  densité sera refusée en L2 (combinaison non définie). Tests `[directional]` : masque nul
  identique, demi-densité ≈ moitié des lignes, tatami contre directionnel ≈ une couche,
  déterminisme.
- 2026-11-16, **L2 livré** (branche `feat/fondu-frontiere-l2`, empilée sur L1c) :
  `document::BoundaryBlendLink` (partenaire, demi-bande de cette zone, courbe) sur
  `EmbroideryObject::boundary_blend` ; `document::find_boundary_blend` (liens réciproques, deux
  formes distinctes, tatami ou directionnel sans dégradé de densité, même courbe, largeurs dans
  les bornes) ; `commands::SetBoundaryBlendLinkCommand` (les deux liens en un pas
  d'annulation) ; `project_ops::make_boundary_blend_command`, `make_remove_boundary_blend_command`,
  `boundary_blend_refusal`, `boundary_blend_info` ; opérations `blend_boundary` et
  `unblend_boundary` ; champ `boundary_blend` de `project inspect` ; champ `.osp`
  `boundaryBlend` (facultatif). Génération : `stitch_generation::boundary_blend_layer`, calculé
  toujours dans le même sens (zone d'identifiant le plus petit en A) pour que les deux couches
  viennent du même plan ; sous-couches sur la zone seule ; pas de cache de séquence pour ces
  zones (elles dépendent de la voisine). Copier-coller, insertion de design : le lien suit
  seulement si les deux zones sont copiées ensemble. Tests `[blend]` (7 cas) : un pas
  d'annulation, refus codés, génération modifiée près de la frontière seulement et
  déterministe, retrait et lien orphelin, aller-retour `.osp`, opérations, `inspect`.
- Écarts à la spec dans L2 :
  - **Pas de texture** dans le lien pour l'instant (le champ de densité ne l'applique pas
    encore) ; à ajouter avec le panneau Studio si le besoin se confirme.
  - **Suppression d'une zone** : le lien de la voisine devient orphelin et sans effet, au lieu
    d'être effacé ; un « annuler » de la suppression rend le fondu actif sans autre commande.
  - **Évidement (knockout)** et cache de séquence ignorés pour les zones liées (v1).
  - Le SVG doré reste à faire ; il demande une forme d'essai réaliste, prévue avec la recette
    Studio (L3).
- 2026-11-17, **L3 livré** (Studio) : `BoundaryBlendController` (propriétés `canCreate`,
  `refusal`, `hasBlend`, `active`, largeurs, transition, frontière ; `create`, `edit`, `remove`),
  panneau `BoundaryBlendPanel.qml` dans l'inspecteur (deux zones sélectionnées : réglages et
  « Créer le fondu » ; zone liée : résumé, réglages immédiats, « Retirer le fondu »), actions du
  catalogue `fade.boundary` et `fade.boundary_remove` (palette Ctrl+K, menus). Chaque action est
  un pas d'annulation. Tests : `test_desktop_v2_studio_boundary_blend` (QTest headless) et
  `boundary blend actions…` dans `test_desktop_v2_core`. **Pas encore** de tracé de la
  frontière ni de poignées de largeur sur le canevas : la frontière se lit dans le panneau.
- 2026-11-17, **L4 livré** (frontière tracée) : `BoundaryBlendLink::manual_frontier` (identique
  sur les deux liens, vide = calculée), `BoundaryBlendSpec::manual_frontier`, refus
  `E_BLEND_FRONTIER_SHORT` et `E_BLEND_FRONTIER_FAR`, clé `.osp` `boundaryBlend.frontier`
  (µm), argument `frontier` de `blend_boundary` (mm, `[]` = calculée), `manual_frontier` dans
  `inspect`, `setFrontier` / `clearFrontier` / `manualFrontier` dans le contrôleur Studio. Les
  zones peuvent ne pas se toucher. Reste : le geste de tracé sur le canevas.
- 2026-11-17, **L5 livré** (proposition depuis l'image) : `project_ops::propose_boundary_blends`
  lit l'image source en travers de chaque frontière commune (un profil par mm, ±6 mm) ; une
  transition de couleur d'au moins 1,5 mm (10 %-90 %) donne une proposition, demi-bandes
  reprises de la mesure de chaque côté ; un bord net ne donne rien. Opération
  `propose_boundary_blends` (`apply`, `min_transition`) et action Studio `fade.boundary_auto`
  (pose tout en un pas d'annulation). Refus `E_BLEND_NO_IMAGE`, `E_BLEND_NO_PROPOSAL`. Tests sur
  image synthétique : transition de 6 mm proposée avec ≈ 2,4 mm de chaque côté, bord net
  ignoré. Pas encore d'appel depuis la numérisation automatique elle-même.
- Écart au découpage initial : le **branchement du masque dans le directionnel** passe en
  L1b. La grille partagée actuelle classe ses rangées par rang dans la région ; l'ancrer
  changerait la sortie des fondus directionnels existants, ce qui doit se faire avec la
  géométrie `Boundary` et ses propres tests de non-régression.

## 1. Besoin

Deux zones remplies se touchent (une à gauche, une à droite). L'utilisateur ne veut pas d'une
ligne nette entre elles : il veut que les deux fils **s'entremêlent sur une bande de part et
d'autre de la frontière**, chaque zone restant un aplat normal loin de la limite.

Le fondu ne suit donc pas une direction de la forme : il suit la **distance à la frontière**.
Si la frontière est courbe, en S ou en plusieurs morceaux, la transition la suit.

## 2. Ce qui existe et pourquoi ça ne suffit pas

| Existant | Où | Limite pour ce besoin |
|---|---|---|
| **Forme ▸ Fondre deux zones** | v1 seulement (`apps/desktop/main_window_shapes.cpp`), doc `directional-fill.md` § *Fondre deux zones voisines* | Unit A et B en une seule forme (A et B disparaissent). Axe **droit** du centre de A vers le centre de B, rampe sur toute l'étendue des deux zones et pas sur une bande autour de la limite. Absent de Studio v2. |
| Fondu à deux fils / multicolore | `project_ops/gradient_fill.hpp`, `MulticolorGradientParams`, Studio `fade_controller` | Fondu **à l'intérieur d'une seule forme**, selon un axe (linéaire, radial, courbe, spirale). |
| Géométrie `DensityGeometry::Contour` | `directional_fill.cpp`, classe `ContourDistance` | Distance au bord **entier** de la forme, normalisée par la distance maximale. Le champ de distance (grille + propagation en deux balayages, déterministe) est **réutilisable**. |
| Grille de rangées partagée (`DensityGradient::shared_rows`) | `directional_fill.cpp` | Partition exacte des rangées entre fils (couverture mesurée 88–112 %). Exige un **même angle et un même écart** pour les deux couches. |
| Courbes (`DensityCurve`) et textures (`DensityTexture`) | `embroidery_object.hpp` | Réutilisables telles quelles : la texture donne une transition en « dents » irrégulières. |

## 3. Principe

```
          zone A                  │                zone B
  aplat A ............ bande A ───┼─── bande B ............ aplat B
  densité A = 1        1 → 0,5    │    0,5 → 0              densité A = 0
  densité B = 0        0 → 0,5    │    0,5 → 1              densité B = 1
                               frontière
```

- **Frontière F** : ligne (polyligne ouverte, en µm) où A et B se touchent.
- **Distance signée** `s(p)` : distance de `p` à F, négative dans A, positive dans B.
- **Bande** : `-w_A ≤ s ≤ +w_B`. Largeurs réglables séparément (symétriques par défaut).
- **Position** `t = (s + w_A) / (w_A + w_B)`, bornée à [0 ; 1], puis courbe et texture :
  `c = courbe(t) + texture(p)`, bornée à [0 ; 1].
- **Densités complémentaires** : `d_A = 1 - c`, `d_B = c`. Une rangée dont la densité tombe
  sous le seuil minimal (écart > `max_spacing`, 4 mm par défaut) n'est pas cousue : c'est là
  que le fil s'arrête.

Chaque zone est donc **prolongée** dans la moitié de bande de l'autre, avec une densité qui
décroît jusqu'à zéro. Elle ne dépasse jamais `A ∪ B`.

## 4. Modèle de données

### 4.1 Lien non destructif

A et B restent **deux objets** avec leurs paramètres, couleurs et fils. Un lien réciproque les
relie ; c'est une métadonnée qui décrit le fondu, jamais des points stockés (invariant :
les points sortent toujours de `effective_sequence`).

```cpp
// document/embroidery_object.hpp
struct BoundaryBlendLink {
    ObjectId partner;
    Micrometers width_self{2'500};    // demi-bande dans CETTE zone, bornée [0,5 ; 15] mm
    DensityCurve curve{DensityCurve::Linear};
    DensityTexture texture{DensityTexture::None};
    double texture_strength{0.35};
    std::uint32_t texture_seed{1};
    // Frontière dessinée à la main (§5.2). nullopt : calculée depuis la géométrie.
    std::optional<geometry::Path> manual_frontier;
    bool operator==(const BoundaryBlendLink&) const = default;
};
// EmbroideryObject : std::optional<BoundaryBlendLink> boundary_blend;
```

- `curve`, `texture*` et `manual_frontier` sont **dupliqués à l'identique** sur les deux
  objets ; les commandes écrivent toujours les deux côtés en une annulation. Un lien non
  réciproque ou incohérent est ignoré à la génération (comme `find_color_blend`).
- Résolution : `document::find_boundary_blend(project, id) -> optional<BoundaryBlendPair>`,
  même style que `find_color_blend` (liens réciproques, deux objets compatibles, sources
  vectorielles valides et **distinctes**).
- v1 : **un seul** lien de frontière par objet (pas de jonction A–B–C, cf. §10).

### 4.2 Types de points acceptés

- v1 : **tatami et remplissage directionnel**, dans toutes les combinaisons (tatami–tatami,
  directionnel–directionnel, tatami–directionnel). Décidé le 2026-11-16 : le tatami reçoit un
  **dégradé natif**, jamais de conversion en directionnel. L'objet de l'utilisateur garde son
  type, ses briques (`stagger`), ses sous-couches et sa compensation.
- Le dégradé du tatami passe par un **masque de rangées** (§6.1), pas par un champ ajouté à
  `TatamiParams` : le masque est dérivé du lien à la génération, aucun réglage n'est dupliqué.
  Le même masque servira plus tard à un dégradé linéaire de tatami seul (HP-STI-010), hors
  périmètre ici.
- Côté directionnel : `DensityGeometry::Boundary = 5`, qui lit le champ de frontière au lieu de
  `from`/`to`, et réutilise la grille partagée existante.
- Satin, contour, motif, radial, spirale : refusés (`E_BLEND_UNSUPPORTED_FILL`).

### 4.3 Persistance `.osp`

Nouveau champ optionnel `boundary_blend` sérialisé dans `project_io/json_serialize.cpp`.
Absent dans les anciens projets : génération strictement inchangée. Aller-retour testé.

## 5. Frontière

### 5.1 Calcul automatique (défaut)

Dans `libs/geometry` (Clipper2 reste derrière l'interface) :

```cpp
// Portions du bord de `a` à moins de `tolerance` de `b`, en polylignes ouvertes, ordonnées
// de façon déterministe (plus long d'abord, puis premier point en x, y).
std::vector<Polyline> shared_frontier(const PathSet& a, const PathSet& b, Micrometers tolerance);
```

- Méthode : découpe en chemins ouverts du bord de A par `offset(B, tolerance)` (Clipper2 sait
  découper des chemins ouverts), puis fusion des morceaux bout à bout et suppression des
  morceaux plus courts que 1 mm.
- `tolerance` par défaut 0,6 mm : couvre les jours et les recouvrements (`inset` de 0,2 mm entre
  tatamis voisins) laissés par la vectorisation.
- **Recalculée à chaque génération** depuis la géométrie courante : déplacer un nœud d'une
  zone fait suivre le fondu. C'est déterministe, donc aucun instantané n'est nécessaire.
- Aucune frontière trouvée : refus `E_BLEND_NO_FRONTIER`, avec le conseil « dessiner la
  frontière ».

### 5.2 Frontière dessinée (lot ultérieur)

Quand les zones ne se touchent pas, se recouvrent largement, ou que l'utilisateur veut une
autre limite : outil « Tracer la frontière » (polyligne ou Bézier) stocké dans
`manual_frontier`. Le signe reste donné par l'appartenance à A ou à B.

## 6. Génération (`stitch_generation`)

1. `effective_sequence` résout la paire. Pour A : région étendue
   `R_A = A ∪ (B ∩ {s ≤ w_B})`, et symétriquement pour B.
2. Champ de distance signée `BoundaryDistance` : même technique que `ContourDistance`
   (grille, germes le long de F, propagation en deux balayages), mais les germes sont pris
   sur **F seulement**, et le signe vient d'un test d'appartenance à A ou à B sur la grille.
   Construit une fois par paire, partagé par les deux couches, lecture seule (compatible
   `parallel_for`).
3. Densité de A en `p` : `d_A(p)` (§3). Elle est appliquée en **éclaircissant les rangées**
   d'une grille fixe, pas en écartant les rangées (§6.1), pour les deux types de points.
4. **Deux régimes, choisis automatiquement :**
   - **Grille commune** : les deux zones tracent exactement les mêmes rangées. C'est le cas de
     deux tatamis de même angle et même écart, ou de deux directionnels sans guide de même
     angle et même écart. A coud les morceaux de rangée de seuil `< d_A`, B ceux de seuil
     `≥ d_A`. Chaque morceau est cousu par un seul fil : couverture exacte.
   - **Superposition** : dans tous les autres cas (angles ou écarts différents, directionnel
     guidé, tatami contre directionnel guidé). Chaque zone éclaircit sa propre grille avec son
     propre angle ; comme `d_A + d_B = 1`, la couverture reste proche d'un aplat, et le
     croisement des angles donne une texture tissée. Le régime est affiché dans l'inspecteur.

### 6.1 Masque de rangées (commun au tatami et au directionnel)

- **Grille ancrée dans l'absolu** : dans le repère tourné de l'angle de la zone, la rangée `k`
  est à `y = k × écart`, avec `k` entier. Aujourd'hui le tatami part du `minY` de sa région et
  la grille partagée du directionnel classe les rangées par rang ; les deux dépendent donc de
  la région. Or `R_A` et `R_B` diffèrent : sans ancrage absolu, les deux zones ne
  traceraient pas les mêmes rangées, et la grille commune serait impossible. L'ancrage ne
  s'applique qu'aux zones liées : les autres gardent leur génération à l'octet près.
- **Seuil de rangée** : `θ_k = frac(k × 0,618…)` (suite de Weyl du nombre d'or, celle de
  `row_thresholds`), calculé depuis `k` et non depuis le rang dans la région. Toute fenêtre
  de rangées voisines a ses seuils presque uniformes dans [0 ; 1) : à densité `d`, une
  proportion ≈ `d` des rangées est cousue, régulièrement répartie.
- **Découpe aux pénétrations** : une rangée est coupée entre deux pénétrations quand la
  densité au milieu du segment passe sous son seuil (même règle que `append_owned_runs`).
  Les bouts de rangées s'arrêtent donc à des distances différentes de la frontière, ce qui
  donne une lisière en peigne et pas une ligne. Les pénétrations gardent leurs positions de
  brique (`stagger`) : le motif du tatami reste intact dans la bande.
- Morceau de moins de deux pénétrations ou plus court que `min_length` : non cousu.
- Tatami : nouvelle surcharge `fill_tatami(region, params, const RowMask*)` ; `nullptr`
  donne le comportement actuel. Sous-couches : elles suivent la région étendue mais **ne
  sont pas masquées** sur la moitié de bande de la zone. Dans la moitié de bande de l'autre
  zone, où la densité tombe à zéro, elles sont retirées pour ne pas apparaître sous le fil
  voisin (à vérifier sur la pièce-test).
5. Ordre de couture inchangé : A puis B (ou l'ordre du document). La couche cousue en second
   recouvre la première dans la bande.
6. **Cache** : la clé de génération de A inclut désormais la géométrie et les paramètres de B
   (et inversement). Garde à ajouter : modifier B invalide A.

## 7. Commandes et API

- `project_ops::make_create_boundary_blend_command(project, a, b, params)` : valide
  (frontière trouvée, types compatibles, couleurs distinctes, aucun lien existant) et écrit
  les deux liens, sans modifier le type ni les paramètres des zones. **Une seule
  annulation.**
- `make_edit_boundary_blend_command` (largeurs, courbe, texture, frontière dessinée) et
  `make_remove_boundary_blend_command`. Supprimer une zone retire le lien de l'autre.
- Copier/coller d'une seule zone : le lien n'est pas copié. Les deux zones ensemble : le lien
  est recréé entre les copies.
- Codes d'erreur stables : `E_BLEND_NO_FRONTIER`, `E_BLEND_UNSUPPORTED_FILL`,
  `E_BLEND_ALREADY_LINKED`, `E_BLEND_SAME_COLOR`, `E_BLEND_WIDTH_RANGE`.
- CLI / MCP : opération `blend_boundary` dans le registre (`op_registry.hpp`), paramètres en mm,
  `dry_run` pris en charge ; `project_inspect` expose le lien et la longueur de frontière.

## 8. Studio v2

- **Déclenchement** : deux zones sélectionnées dans l'espace Points → bouton et menu
  contextuel « Fondu à la frontière ». Désactivé avec une raison affichable (frontière
  absente, type non pris en charge, déjà lié).
- **Canevas** (`HandlesOverlay.qml`, `gpu_canvas`) : frontière en trait plein, bords de bande
  en pointillés, une poignée par côté pour régler la largeur (Maj : symétrique). Aperçu en
  direct pendant le glissé (`previewHandle` / `commitHandle`, comme les fondus actuels).
- **Panneau** (dans `FadeProperties.qml`) : largeur (lien symétrique activé par défaut),
  transition Douce / Moyenne / Nette (courbe), texture et force, régime affiché (« fils
  alternés » ou « superposés »), bouton « Retirer le fondu ».
- Calculs hors du fil d'interface (`BusyIndicator::run`) ; aucune logique métier dans les
  contrôleurs : tout passe par les commandes du §7.

## 9. Tests et critères d'acceptation

**Cœur (Catch2)**
- `shared_frontier` : deux carrés accolés (une frontière droite), deux carrés avec un jour de
  0,3 mm, frontière en S, contact en deux morceaux, zones disjointes (aucune frontière).
- Distance signée : signe correct, nulle sur F, bande bornée ; deux calculs identiques octet
  pour octet.
- Génération : aucun point hors de `A ∪ B` ; aucun point de A au-delà de `w_B` dans B ;
  loin de la bande, les points sont **identiques** à ceux d'un aplat sans lien.
- Couverture par bande de 1 mm le long de `s` : 88–112 % d'un aplat (même critère que la
  grille partagée), dans les deux régimes et pour les trois combinaisons de types.
- Grille commune tatami–tatami : aucun morceau de rangée cousu deux fois ; les pénétrations
  de la bande tombent sur les positions de brique du tatami sans lien.
- Tatami sans lien, ou avec `RowMask` nul : séquence identique octet pour octet à celle
  d'avant (non-régression de l'ancrage absolu).
- DST exporté deux fois : identique octet pour octet.

**Commandes / E/S** : création, réédition, suppression en une annulation ; suppression d'une
zone ; aller-retour `.osp` ; ancien projet sans lien inchangé.

**Studio (QTest headless)** : action activée ou désactivée selon la sélection, glissé de
poignée puis annulation, panneau qui reflète le lien.

**Doré** : un SVG `tests/golden/boundary_blend_s_curve.svg` (deux zones en S, bande de
3 mm).

**Recette physique** (non déductible des tests) : pièce-test 2 zones, frontière droite et en
S, largeurs 2, 4 et 8 mm, régimes alternés et superposés. On regarde l'aspect et le tirage du
tissu dans la bande. Statut « Validé » seulement après cette recette.

## 10. Questions ouvertes

1. ~~Tatami : conversion ou dégradé natif ?~~ **Tranché le 2026-11-16 : dégradé natif**
   par masque de rangées (§4.2, §6.1).
2. **Jonctions à trois zones** et zone liée à plusieurs voisines : hors v1. Piste : densités
   normalisées sur plusieurs champs de distance (`Σ d_i = 1`).
3. **Angle dans la bande** quand les angles diffèrent : superposition (proposé), angle commun
   imposé dans la bande, ou rangées parallèles à la frontière (remplissage courbe) ?
4. **Satin d'un côté** : fondu satin ↔ remplissage, à étudier séparément.
5. **Bord de la bande sur le contour extérieur** : la distance au bout de F arrondit la bande ;
   à vérifier visuellement sur la pièce-test.

## 11. Découpage en lots

| Lot | Contenu | Couches |
|---|---|---|
| L1a ☑ | Masque de rangées : grille ancrée, seuils par `k`, surcharge `fill_tatami(…, RowMask*)` ; non-régression octet pour octet sans masque | `stitch_generation` |
| L1b ☑ | `shared_frontier`, `polyline_band`, champ de densité, régions prolongées, paire tatami–tatami dans les deux régimes, tests cœur | `geometry`, `stitch_generation` |
| L1c ☑ | Masque dans le directionnel (superposition), `fill_boundary_blend` pour les trois combinaisons | `stitch_generation` |
| L2 ☑ | Lien, commandes, `.osp`, opérations CLI / MCP, génération, `inspect`, codes d'erreur | `document`, `commands`, `project_ops`, `project_io`, `stitch_generation` |
| L3 ☑ | Studio : contrôleur, panneau d'inspecteur, actions du catalogue, QTest (poignées de canevas : reste) | `apps/desktop_v2` |
| L4 ☑ | Frontière tracée : lien, moteur, `.osp`, opération, contrôleur (geste de tracé sur le canevas : reste) | `document`, `stitch_generation`, `project_ops`, Studio |
| L5 ☑ | Proposition depuis l'image : transition douce en travers des frontières, opération et action Studio | `project_ops`, Studio |

L1a passe en premier : il touche le cœur du tatami et doit prouver seul qu'il ne change rien
sans lien. L1c et L2 peuvent ensuite avancer en parallèle si le struct du §4.1 est figé
d'abord ; L2 peut déjà brancher le cas tatami–tatami. L3 dépend de L2.
