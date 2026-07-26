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

Note : « Supprimer » fait **disparaître** la région (retour au fond) et ne la
fusionne pas avec une voisine — corrigé après un retour utilisateur (voir
*Limitations*). Pour transférer des pixels à une autre région, utilisez la
**fusion** explicite.

## Undo/redo

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
- `libs/commands/.../project_commands.hpp` — commandes de segmentation.
- Tests : `tests/unit/segmentation/test_segmentation.cpp`,
  `tests/unit/commands/test_undo_stack.cpp`.
