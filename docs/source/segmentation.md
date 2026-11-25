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
- **Vectorisation.** `VectorizeOptions::detail_mask` et `detail_tolerance_factor` (0,5 par défaut) :
  une région dont la moyenne du masque est ≥ 128 est vectorisée avec des tolérances de
  simplification (polygone, plancher 0,1 mm = pas machine) et de courbe divisées par le facteur,
  donc un contour plus fidèle et plus de nœuds. Transmis par `AutoOptions::detail_mask`,
  `DigitizeSource::detail_mask` (numérisation automatique du bureau) et la vectorisation des régions
  du parcours Workflow. Masque absent ou de mauvaise taille : sortie inchangée.
- **Couture.** `AutoOptions::detail_min_fill_area_mm2` (défaut 1 mm²) remplace `min_fill_area_mm2`
  pour les régions de détail : une petite forme de détail (oeil, lettre) est cousue en tatami
  (ou satin) au lieu d'un simple contour. `merge_small_regions` reçoit le masque et un seuil de
  détail pour ne pas absorber ces régions avant la couture. Sans masque : inchangé. La densité
  et l'angle ne dépendent pas encore de la zone.
- **Limites.** Le CLI et
  le MCP, qui n'ont pas de projet peint, ne passent pas de masque. Pas
  de proposition automatique de zones. Un recadrage ou une rotation de l'image rend le masque
  inutilisable (taille différente) : il est ignoré puis remplacé au prochain trait.

## Structures fines (SEG-FINE-01, expérimental)

Option **désactivée par défaut** : `SegmentationOptions::fine_structures` (un
`std::optional<FineStructureOptions>`). Sans valeur, `segment()` produit les mêmes labels,
octet pour octet, qu'avant le chantier (garde : suite `test_segmentation`).

**Pourquoi.** La quantification k-means affecte chaque pixel au centre le plus proche : un trait
d'un pixel, anticrénelé ou peu contrasté, est (1) confondu avec le fond si sa couleur est rare,
(2) effacé par le vote de lissage (une ligne d'un pixel dans une fenêtre 3×3 ne pèse que 1/3 des
voix), (3) fragmenté par la 4-connexité (une diagonale), (4) absorbé par le nettoyage des petites
régions (16 pixels). Les traits sont donc lus sur l'image **non quantifiée** et protégés.

**Détection** (`detect_fine_structures`, `fine_structures.hpp`, masques raster uniquement) :

1. chapeaux haut-de-forme noir et blanc sur L\*, a\*, b\* à plusieurs diamètres de noyau
   (3, 5, 9, 17 selon `max_width_px`) ; réponse = norme (ΔE) des trois canaux, ce qui capte aussi
   un trait de clarté voisine mais de teinte différente ;
2. validation « crête » dans quatre directions : le centre diffère des deux côtés (≥ la moitié du
   contraste) **et** les deux côtés ne sont pas alignés avec lui (`ridge_side_ratio`) — une
   frontière simple, une rampe de dégradé et un bord d'aplat sont écartés, un trait qui traverse
   deux aplats de couleurs différentes est conservé ;
3. hystérésis 8-connexe (germes ≥ `high_contrast`, propagation ≥ `low_contrast`) ;
4. rejet des composantes plus courtes que `max(min_length_px, largeur moyenne)` : le coin arrondi
   d'un gros aplat répond comme un trait épais et court, un vrai trait est plus long que large.

Sortie : `FineStructureMap` (masque, confiance 0–255, borne de largeur, compteurs). Pas de
géométrie vectorielle.

**Protection dans `segment()`** :

- couleur rare : les pixels protégés dont la couleur est à plus de `rare_color_delta_e` de tout
  centre de la palette fournissent jusqu'à `max_extra_colors` centres **supplémentaires** (la
  palette peut donc dépasser `max_colors` de ce nombre) ; seuls les pixels protégés peuvent les
  rejoindre ;
- un trait anticrénelé est quantifié en plusieurs classes ; pour chaque composante du masque, la
  classe du trait (la plus éloignée du fond local parmi les classes assez fréquentes) absorbe les
  pixels protégés dont la couleur est un mélange fond/trait (ΔE ≤ 12 au segment) ;
- les pixels protégés ne sont jamais écrasés par le vote de lissage ;
- les pixels protégés d'une même classe se relient en **8-connexité** (une diagonale d'un pixel est
  une région) ; les aplats gardent la 4-connexité ;
- une région dont ≥ 50 % des pixels sont protégés échappe au nettoyage si l'étendue de ses
  pixels protégés atteint `min_length_px` sur au moins un axe. Le nettoyage peut ainsi absorber
  les fragments courts créés quand la quantification découpe un trait en plusieurs couleurs.

`segment(img, options, &carte)` renvoie aussi la `FineStructureMap` utilisée, pour qui veut le
masque de traits (couche de traits, aperçu) sans recalcul.

**Où l'activer.** Même réglage partout, décoché par défaut :

- Studio, espace Segmentation, panneau Segmentation : case « Les garder dans les régions
  (expérimental) » (`segFineStructures`), sous le titre « TRAITS FINS (CORDAGES, NERVURES) ». Elle suit l'aperçu en direct, « Appliquer », « Segmenter
  la sélection » et « Re-segmenter » (`segmentationSettingsFrom`, clé `fine_structures`) ; grisée en
  mode « zones de fondu ». À ne pas confondre avec « Coudre les traits sombres en lignes (étape Vecteurs) » (anciennement
  « Préserver les traits fins »), qui agit plus loin (aplats continus sous les traits, traits cousus par-dessus) ; les deux se
  complètent ;
- ancien bureau : case « Garder les traits fins (expérimental) » dans « Segmenter l'image » (mémorisée avec les autres réglages) ;
- `openstitch-cli project digitize --fine-structures` et l'opération `segment_image` (argument
  booléen `fine_structures`, donc aussi côté MCP) ; le rapport `digitize-report/1` porte
  `settings.fine_structures`.

Mesure sur une image de voilier (640×400, haubans d'un pixel) : 4 régions sans l'option, 32 avec.

**Mesures** (corpus synthétique, `tests/unit/segmentation/test_fine_structures.cpp`, tolérance 1 px) :
rappel 1,00 et précision ≥ 0,97 pour des traits sombres de 1, 2, 4 et 8 px (horizontaux,
diagonaux, courbes, verticaux), traits clairs sur fond sombre, traits chromatiques, trait traversant
deux aplats, bruit JPEG ±3 ; zéro faux positif sur frontière simple, dégradé, bruit seul, gros
aplat. Diagonale d'un pixel (k = 3, lissage 1, nettoyage 16) : 0 % de la vérité conservée sans
l'option, 100 % avec, en 2 régions (fond + trait). Le masque détecté déborde d'environ 1 px de part
et d'autre de la vérité (précision stricte 0,8–0,9) : c'est le halo anticrénelé.

**Coût et fragmentation.** Comparatif Windows Release, trois répétitions alternées par réglage,
`segment_image` avec `max_colors=8`, `min_region_px=16` et `smoothing_radius_px=1`, après
`set_source_image` dans un lot `project apply`. Le temps mur et le pic de mémoire résidente incluent
le lancement du CLI, la lecture de l'image et l'écriture OSP. Les comptes excluent le label 0.

| Image | Régions OFF / ON initial / ON corrigé | Temps médian OFF / ON corrigé | Pic résident médian OFF / ON corrigé |
| --- | ---: | ---: | ---: |
| Illustration marine `sample/0215a7e5-97b1-44cb-8b5a-2b5a67e9efbe.png` (1445 × 1088) | 148 / 3502 / 594 | 0,34 / 2,28 s | 133 / 163 Mio |
| Motif `tests/fixtures/tentabrode.png` (1117 × 1408) | 226 / 4629 / 613 | 0,37 / 2,28 s | 133 / 154 Mio |
| Marine synthétique `tests/fixtures/ai-benchmark/marine-synth-01/source.png` | 20 / 54 / 35 | 0,12 / 0,21 s | 26 / 28 Mio |

Le SHA-256 de `segmentation.u32` est stable sur les trois répétitions de chaque réglage. Les
empreintes OFF sont identiques avant et après le correctif, y compris avec le paramètre omis.
La marine synthétique donne 20 formes vectorielles et 20 objets brodés en OFF, 33 formes et 33
objets en ON via `vectorize_segmentation` et `convert_vectors`.

**Photo de gréement.** Avec les mêmes réglages, la photo de Mathieu Dréo
([Wikimedia Commons, domaine public](https://commons.wikimedia.org/wiki/File:Tall_ship_rigging_in_amsterdam_close.jpg),
640 × 480) passe de 756 à 1488 régions, de 0,30 à 0,75 s et de 44 à 52 Mio (médianes OFF / ON
corrigé). À l'inspection des cartes de régions recolorées, plusieurs cordages diagonaux sont plus
visibles, tandis que des bordures et des ombres sont épaissies.

**Limites.** Le gain de fidélité reste à mesurer sur des images réelles annotées. La branche B
(surfaces reconstituées indépendamment), Hessienne/Sato/Frangi et la complétion des aplats sous les
traits restent à comparer. Deux traits parallèles écartés de moins que la portée du noyau peuvent
se masquer mutuellement (le test de crête utilise alors la portée courte).

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
| Fond présumé | `background_candidate(options)` | Couleur de la plus grande région (ou, à défaut, couleur qui encadre l’image), surface, L*, bords touchés, recommandation (voir *Numérisation automatique*) |

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
