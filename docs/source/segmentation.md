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
- Tests : `tests/unit/segmentation/test_segmentation.cpp`,
  `tests/unit/segmentation/test_color_merge.cpp`,
  `tests/unit/commands/test_undo_stack.cpp`, `tests/unit/commands/test_command_merge.cpp`,
  `tests/unit/desktop/test_segmentation_colors_panel.cpp`,
  `tests/unit/desktop/test_segmentation_workflow.cpp`.
