# Segmentation

Public : utilisateur avancé, développeur. État : **Implémenté**.

## But

Réduire l'image de travail en **régions de couleur connexes**, sélectionnables et
éditables, qui serviront de base à la vectorisation puis aux objets de broderie.

## Algorithme

1. Les pixels d'alpha < 128 sont traités comme **fond**.
2. Les pixels opaques sont convertis en **CIELAB** (espace perceptuel) ;
3. un **k-means** déterministe (graine fixe) sur un échantillon de pixels calcule
   jusqu'à *N* couleurs représentatives ;
4. chaque pixel est affecté au centre Lab le plus proche ;
5. par couleur, les **composantes connexes** (4-connexité, `cv::connectedComponents`)
   deviennent des **régions**. Deux régions de même couleur mais disjointes
   restent distinctes ;
6. les régions plus petites que la taille minimale sont **absorbées** par leur
   voisine majoritaire (nettoyage).

## Aperçu avant de segmenter (bureau)

Le dialogue *Segmentation → Segmenter l'image…* (F6) est **non modal** et affiche un **aperçu en
direct** : chaque changement de réglage (nombre de couleurs, taille minimale, lissage) relance
`segmentation::segment` après un court délai (250 ms), et la carte des régions résultante s'affiche
sur le canevas avec le nombre de régions, de couleurs et de régions plus petites que le seuil de
remplissage de l'auto-numérisation (`AutoOptions::min_fill_area_mm2`, qui ne seront cousues qu'en
contour).

- **Le document n'est pas touché** : le calcul tourne dans un fil, sur une **copie** de l'image de
  travail prise à l'ouverture du dialogue, et l'aperçu s'affiche à la place de la segmentation du
  document (`previewSegmentation_`) sans aucune commande ni pas d'annulation. Fermer le dialogue
  abandonne l'aperçu.
- **Un seul calcul à la fois** : les changements faits pendant un calcul sont coalescés en un seul
  calcul suivant ; seul le résultat correspondant aux réglages courants est affiché.
- **« Segmenter » valide** : confirmation d'écrasement (si la segmentation a été éditée) puis
  `SetSegmentationCommand`, en un seul pas d'annulation. Si l'aperçu correspond aux réglages et à
  l'image, il est **réutilisé** (pas de second calcul) ; sinon la segmentation est recalculée.
- **Coût mesuré** : `segment` sur l'illustration du voilier (1254 × 1254 px, 8 couleurs, lissage 3)
  prend **0,09 à 0,22 s** en Debug (0,6 s au premier appel), de sorte que l'aperçu se fait sur
  l'image entière, sans version réduite.

## Zones de détail et aplats

Un seul réglage de lissage et de taille minimale pour toute l'image oblige à choisir : lisser fort
nettoie les aplats mais efface les détails fins (yeux, texte), lisser peu garde les détails mais
laisse du « poivre et sel » dans les aplats. Le **masque de détail** (`segmentation::DetailMask`,
un octet par pixel de l'image de travail : 0 = aplat, 255 = détail) donne un réglage par zone.

- **Réglages.** `SegmentationOptions::detail_mask` (pointeur, `nullptr` = aucune zone),
  `detail_smoothing_radius_px` (défaut 0) et `detail_min_region_px` (défaut 2) remplacent
  `smoothing_radius_px` et `min_region_px` **dans les zones de détail**.
- **Lissage.** Pour chaque classe de couleur, la densité locale est calculée avec le rayon d'aplat
  et avec le rayon de détail, puis mélangée pixel par pixel avec le poids du masque ; le bord doux
  du pinceau donne donc une transition progressive et pas de coupe nette sur la frontière.
- **Nettoyage.** Une région est « de détail » si la moyenne du masque sur ses pixels est ≥ 128 ;
  elle est alors comparée à `detail_min_region_px` au lieu de `min_region_px`.
- **Inchangé sans masque.** Un masque absent, vide ou sans pixel de détail suit exactement le chemin
  historique (même sortie octet pour octet). Le k-means reste global : les couleurs sont cohérentes
  d'une zone à l'autre. Un masque qui n'a pas la taille de l'image est refusé (`UserInput`).
- **Pinceau.** `paint_detail_disc` peint (valeur 255) ou gomme (valeur 0) un disque, bord franc ou
  doux, et renvoie les pixels modifiés avec leur ancienne valeur : l'annulation ne copie pas le
  masque entier.
- **Bureau.** Outil *Segmentation → Zones de détail* (Maj+D, aussi dans la palette d'outils, bouton
  « Peindre les zones de détail… » du dialogue) : glisser peint, la case *Gomme* retire, *Tout
  effacer* vide le masque, Échap abandonne le trait. La surcouche colorée montre les zones. Un trait
  est **un** pas d'annulation (`PaintDetailMaskCommand`) ; le masque est enregistré dans le projet
  (`detail_mask.u8` dans l'archive `.osp`, sans changement de version de schéma). Le masque n'agit
  qu'à la prochaine segmentation ; l'aperçu du dialogue le suit.
- **Limites.** Seuls le lissage et la taille minimale dépendent de la zone ; la vectorisation et la
  couture (tolérance de simplification, type de point préféré) ne lisent pas encore le masque. Pas
  de proposition automatique de zones. Un recadrage ou une rotation de l'image rend le masque
  inutilisable (taille différente) : il est ignoré puis remplacé au prochain trait.

## Identifiants stables

Chaque région a un `RegionId` = index de slot + 1. Les slots ne sont **jamais**
réutilisés : un identifiant reste valide pour toute la vie de la segmentation
(important pour l'undo/redo). La carte est stockée comme `labels[y*w+x]`
(0 = fond) et `region_slots[]` (la région ou `nullopt` si supprimée/fusionnée).

## Éditions

| Action | Fonction | Effet |
|---|---|---|
| Sélection | `region_at(x,y)` | Renvoie la région sous un pixel |
| Fusion | `merge_regions(keep, absorb)` | Réétiquette `absorb` en `keep` |
| Suppression | `remove_region(id)` | Renvoie les pixels au **fond** (pas de fusion) |
| Recoloration | `recolor_region(id, rgb)` | Change la couleur représentative |
| Carte | `render_map(highlight)` | Image RGBA (fond transparent, sélection éclaircie) |
| Nettoyage | `remove_thin_parts(min_width_px)`, `merge_small_regions(min_px, excluded)` | Ouverture morphologique et fusion des fragments, sur la copie de travail de l'auto-numérisation (voir *Numérisation automatique*) |
| Fond présumé | `background_candidate(options)` | Couleur de la plus grande région, surface, L*, bords touchés, recommandation (voir *Numérisation automatique*) |

Note : « Supprimer » fait **disparaître** la région (retour au fond) et ne la
fusionne pas avec une voisine — corrigé après un retour utilisateur (voir
*Limitations*). Pour transférer des pixels à une autre région, utilisez la
**fusion** explicite.

## Fusion de couleurs

Une « région » est une composante connexe : plusieurs régions peuvent porter la **même teinte**.
Fusionner des couleurs recolore donc toutes les régions d'une teinte avec une autre, puis (option)
fusionne les régions **voisines** devenues de même couleur, pour éviter une frontière et un objet de
broderie inutiles. Deux régions de même couleur mais séparées ne sont jamais fusionnées (une région
reste connexe).

Tout passe par un **plan** (`segmentation/color_merge.hpp`), calculé sans écrire puis appliqué :

| Fonction | Rôle |
|---|---|
| `color_classes(seg)` | Teintes distinctes, de la plus étendue à la plus petite, avec leurs régions |
| `plan_reduce_colors(seg, max_colors, max_distance, merge_adjacent, excluded)` | Réduit la palette à N teintes et/ou fusionne les teintes distantes d'au plus `max_distance` (CIEDE2000) ; le plus gros aplat garde sa teinte exacte (`thread_palette::reduce_colors`, étendu d'un seuil de distance optionnel) |
| `plan_merge_colors(seg, sources, target, merge_adjacent)` | Fusion manuelle : les teintes `sources` prennent la couleur `target` |
| `apply_color_merge(seg, plan)` | Applique les recolorations puis les fusions |

Le calcul est déterministe (aucun parallélisme, égalités départagées par l'ordre d'entrée).

**Bureau** : menu *Segmentation → Fusionner les couleurs…* ouvre le panneau **Couleurs** : liste des
teintes (pastille, part de l'image, nombre de régions) ; cliquer une teinte sélectionne toutes ses
régions ; *Fusionner dans la teinte active* fusionne les teintes sélectionnées ; les réglages *Réduire
à N* et *Fusionner si écart ≤ x* affichent un **aperçu** de la carte des régions sur le canevas
(calculé sur une **copie** de la segmentation : le document n'est pas touché, aucun pas d'annulation)
jusqu'à *Appliquer* ou *Annuler l'aperçu*. Un aperçu devient caduc dès que le document change.
Les objets vectoriels déjà créés depuis une région absorbée suivent le même choix que *Fusionner*
(conserver, supprimer, annuler).

**Limite** : fusionner des couleurs fait perdre du détail de teinte (un détail dont la teinte est
proche d'un aplat prend la couleur de l'aplat) ; l'aperçu sert à le voir avant d'appliquer.

## Undo/redo

`MergeColorsCommand` applique un plan en **un seul pas** d'annulation (copie des seuls emplacements de
régions et des étiquettes modifiées, comme `CleanIslandsCommand`).

Les commandes `SetSegmentationCommand`, `MergeRegionsCommand`, `RemoveRegionCommand`
et `RecolorRegionCommand` mémorisent les **indices de pixels** réétiquetés (pas
une copie de la carte entière), ce qui rend l'annulation exacte et peu coûteuse.

## Erreurs

- Image entièrement transparente → erreur `UserInput`.
- Nombre de couleurs hors bornes → refus.

## Implémentation associée

- `libs/segmentation/include/openstitch/segmentation/segmentation.hpp`
- `libs/segmentation/src/segmentation.cpp` — `segment`, `merge_regions`,
  `remove_region`, `recolor_region`, `render_map`.
- `libs/segmentation/src/color_merge.cpp` — fusion de couleurs (plans et application).
- `libs/commands/.../project_commands.hpp` — commandes de segmentation (dont `MergeColorsCommand`).
- `apps/desktop/segmentation_colors_panel.*`, `apps/desktop/main_window_colors.cpp` — panneau
  « Couleurs » et aperçu.
- `apps/desktop/segmentation_dialog.*`, `apps/desktop/main_window_segment_preview.cpp` — dialogue
  « Segmenter » non modal et aperçu en direct.
- Tests : `tests/unit/segmentation/test_segmentation.cpp`,
  `tests/unit/segmentation/test_color_merge.cpp`,
  `tests/unit/commands/test_undo_stack.cpp`, `tests/unit/commands/test_command_merge.cpp`,
  `tests/unit/desktop/test_segmentation_colors_panel.cpp`,
  `tests/unit/desktop/test_segmentation_dialog.cpp`,
  `tests/unit/desktop/test_segmentation_workflow.cpp`.
