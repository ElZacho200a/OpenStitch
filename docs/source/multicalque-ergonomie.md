# Travail multicalque dans Studio : audit et matrice d'améliorations

État au 2026-11-18, branche `feat/studio-multicalque`. Ce chapitre fait l'inventaire des canevas
réellement présents dans Studio (`apps/desktop_v2`), décrit le modèle de calques et liste au moins
dix améliorations par canevas, avec leur priorité et leur état. « Livré » signifie implémenté **et**
couvert par un test automatisé de ce lot ; rien n'est revendiqué sans test.

## 1. Inventaire des canevas

Studio a **un** canevas de rendu (`GpuCanvas`, `canvas_renderer.cpp`) monté par quatre espaces de
travail (`qml/spaces/SpaceHost.qml`, Ctrl+1 à Ctrl+4). Chaque espace déclare les couches de rendu
qu'il affiche (`canvasLayers`). La simulation n'est pas un espace : c'est un mode de l'espace Points
ouvert depuis le hub Vue (`ViewPanel.qml`, `SimulationBar.qml`, Ctrl+L).

| Canevas / mode | Fichiers | Couches affichées | Notion de calque aujourd'hui |
|---|---|---|---|
| Image | `ImageSpace.qml`, `image_space_controller.*`, `layers/source_image_layer.*` | source image active | sélecteur « Calque image actif » (liste déroulante) ; une seule source dessinée à la fois |
| Segmentation | `SegmentationSpace.qml`, `segmentation_space_controller.*`, `layers/region_layer.*` | source (option), régions, contours | même sélecteur ; « Segmenter tous les calques » ; opacité des régions |
| Vecteurs | `VectorSpace.qml`, `vector_space_controller.*`, `layers/vector_layer.*` | formes | même sélecteur ; onglet Calques (`LayerPanel` + `ObjectTree`) ; « Vectoriser les N calques segmentés » |
| Points | `PointsSpace.qml`, `points_space_controller.*`, `PointsObjectsPanel.qml` | points, sélection | `LayerPanel` + arbre ; filtres d'affichage par objet |
| Simulation (mode de Points) | `ViewPanel.qml`, `SimulationBar.qml`, `RealisticToggle.qml` | points cousus progressivement, rendu réaliste | aucune : simule tout ce qui est visible |

## 2. Modèle de calques

- `document::Calque { id, name, visible, locked }` ; `Project::calques` du fond (cousu en premier)
  vers le dessus. **Le rang du calque est la clé principale de l'ordre de couture**
  (`Project::sewingOrder`). Liste vide = un calque implicite (documents antérieurs).
- Une forme appartient à un calque (`VectorObject::calque`) ; ses couches de couture
  (`EmbroideryObject`) suivent leur forme. Un calque masqué n'est ni dessiné ni cousu.
- Une source image peut porter un calque (`Project::image_sources`, import `.ora` par
  `project_io/openraster.cpp`) avec ses opérations, sa segmentation et son masque de détail.
- Persistance : `.osp` v9 (`calques`, `calque`) puis v10 (sources par calque). `active_calque` et
  `active_image_source` sont des états d'interface, ni annulables ni enregistrés.
- Mutations : `commands/calque_commands.hpp`, une commande = une étape d'annulation.

Trois ordres distincts, à ne pas confondre :

1. **ordre des calques** (`Project::calques`) : affichage dans l'arbre **et** clé de couture ;
2. **ordre du document** (`Project::embroidery_objects`) : départage les objets d'un même calque ;
3. **ordre physique** : `sewingOrder()` puis finitions de `effective_sequence`.

Conséquence assumée : déplacer une forme vers un autre calque, ou réordonner les calques, change
l'ordre de couture. Le message de statut le dit (« l'ordre de couture suit »).

## 3. Constats de l'audit

| # | Constat | Effet | Corrigé |
|---|---|---|---|
| A1 | Les commandes de calque héritaient de `affectsSegmentation() = true` (et `affectsEmbroidery() = true` pour renommer/verrouiller/ajouter) | renommer ou verrouiller un calque relançait le rafraîchissement des régions et la reconstruction de la scène de points | oui |
| A2 | Studio ne fixait jamais `Project::active_calque` | toute forme créée tombait dans le calque du fond, quel que soit le calque choisi dans l'arbre | oui |
| A3 | Trois notions de « calque courant » : `ObjectTree.currentLayer` (QML), `active_calque`, `active_image_source` | le panneau, les nouvelles formes et l'espace Image pouvaient viser trois calques différents | oui (une seule notion) |
| A4 | Tout sélectionner, Inverser, par couleur/type/calque et le rectangle prenaient les objets des calques verrouillés | un Suppr ou une transformation après Ctrl+A modifiait un calque verrouillé | oui |
| A5 | « Déplacer la sélection ici » acceptait un calque verrouillé comme source ou destination | un calque verrouillé pouvait être vidé ou rempli | oui |
| A6 | Réordonner = boutons Monter/Descendre seulement (une étape par rang) | N clics et N étapes d'annulation pour un déplacement | oui (glisser-déposer, une étape) |
| A7 | Pas d'isolation (solo) ; pas de « tout afficher » | masquer un à un chaque calque | oui |
| A8 | `EditController::tree()` rebalayait tout l'ordre de couture pour chaque groupe | coût O(objets × groupes) à chaque rafraîchissement de l'arbre | oui (index unique) |
| A9 | Visibilité et verrou affichés en texte (« Voir », « Libre ») | état peu lisible, cible de clic étroite | oui (icônes thémables) |
| A10 | L'arbre ne sélectionnait qu'avec Ctrl ; pas de Maj+clic | sélection de plage impossible | oui |
| A11 | L'espace Image ne dessine que la source active | pas d'aperçu composé des calques d'un `.ora`, pas d'opacité par calque image | non (P1, §5) |
| A12 | `DocumentSession::snapshot()` copie tout le projet, images comprises, à chaque révision | un geste de calque (même un renommage) copie les pixels si une tâche demande un instantané | non (mesuré à 6 ms pour 12 Mpx dans `docs/desktop-v2/10-performances.md` ; à reprendre si les `.ora` grossissent) |

## 4. Matrice d'améliorations

Priorité : **P0** sûreté du travail ou socle commun, **P1** gain d'ergonomie fort, **P2** confort.
Coût : S (≤ 1 j), M (2-4 j), L (> 4 j). État : ✅ livré dans ce lot, ◐ existait avant ce lot,
☐ à faire.

### 4.1 Socle commun (tous les canevas)

| # | Amélioration | Comportement attendu | Prio | Composants | Coût | État | Tests |
|---|---|---|---|---|---|---|---|
| C1 | Calque actif unique | clic sur un calque = calque actif du document ; reçoit les formes créées ; s'il porte une image, devient la source des espaces Image/Segmentation/Vecteurs ; le sélecteur d'image fait l'inverse | P0 | `DocumentSession::selectCalque`, `EditController::activeLayer`, `ObjectTree`, `LayerPanel` | S | ✅ | `layerWorkflowIsSharedLockedAndScoped`, `openingAnotherProjectClearsLayerAndObjectSelection` |
| C2 | Portées de recalcul exactes | renommer/verrouiller/ajouter : aucun recalcul ; masquer/réordonner/déplacer : points seulement, jamais la segmentation | P0 | `calque_commands.hpp` | S | ✅ | Catch2 « renommer et verrouiller ne recalculent… », QTest |
| C3 | Verrou respecté par la sélection en masse | Ctrl+A, Inverser, par couleur/type/calque, rectangle et Maj+clic ignorent les calques verrouillés ; ils restent cibles d'aimantation | P0 | `EditController::selectableObject` | S | ✅ | QTest |
| C4 | Transfert intercalque protégé | refus si la destination est verrouillée ; les objets d'un calque verrouillé ne sont jamais déplacés | P0 | `moveObjectsToCalque` | S | ✅ | QTest |
| C5 | Réordonner par glisser-déposer | glisser une ligne de calque sur une autre : prend sa place, une étape | P1 | `ReorderCalqueCommand`, `ObjectTree` | S | ✅ | Catch2 + QTest |
| C6 | Isoler / tout afficher | Alt+clic sur l'œil, bouton « Isoler », menu ; second appel = tout réafficher ; une étape | P1 | `SetCalquesVisibilityCommand`, `isolateCalque` | S | ✅ | Catch2 + QTest |
| C7 | Menu contextuel de calque | clic droit : rendre actif, sélectionner le contenu, déplacer la sélection ici, isoler, verrouiller, dupliquer, supprimer | P1 | `ObjectTree.qml` | S | ✅ | non testé en QTest (menu natif) |
| C8 | Raccourcis communs | Ctrl+Maj+N nouveau calque, F2 renommer le calque actif, Ctrl+Alt+] / [ monter/descendre | P1 | `LayerPanel.qml` | S | ✅ | non testé en QTest |
| C9 | États lisibles | icônes œil/cadenas aux couleurs du thème, calque masqué en italique estompé, barre d'accent sur le calque actif, nombre d'objets par calque | P1 | `StudioIcon`, `ObjectTree` | S | ✅ | non testé (rendu) |
| C10 | Sélection de plage | Maj+clic dans l'arbre, Ctrl+Maj+clic ajoute la plage | P1 | `selectTreeRange` | S | ✅ | QTest |
| C11 | Arbre en temps linéaire | index calque/groupe calculé une fois par rafraîchissement | P1 | `EditController::tree` | S | ✅ | couvert par les tests d'arbre existants ; pas de mesure chiffrée |
| C12 | Groupes de calques | calques imbriqués | P2 | modèle `Calque` (+ parent), `.osp` | L | ☐ | — |
| C13 | Miniatures par type | vignette image / régions / formes / points dans la ligne de calque | P2 | fournisseur d'images QML | M | ☐ | — |

### 4.2 Canevas Image

| # | Amélioration | Prio | Coût | État |
|---|---|---|---|---|
| I1 | Sélection directe : clic sur l'image → calque dont l'alpha est non nul au point (le plus haut d'abord) | P1 | M | ☐ |
| I2 | Sélection multiple Ctrl/Maj dans la liste des calques image (aujourd'hui une liste déroulante) | P1 | M | ☐ |
| I3 | Réorganisation par glisser-déposer : socle C5 ; à exposer dans l'espace Image (panneau Calques absent de cet espace) | P1 | S | ◐ |
| I4 | Visibilité instantanée sans recalcul : socle C2 (aucune resegmentation) ; dessin composé des calques visibles à faire (A11) | P0 | M | ◐ |
| I5 | Mode solo : socle C6 | P1 | S | ✅ |
| I6 | Opacité par calque image avec aperçu immédiat (propriété d'affichage, hors document) | P1 | M | ☐ |
| I7 | Verrouillage : socle C3/C4 pour les formes ; à étendre aux opérations d'image du calque (refuser recadrage/pipette sur source verrouillée) | P0 | S | ◐ |
| I8 | Transformation indépendante d'un calque image (décalage/échelle propres, aujourd'hui toutes les sources changent ensemble) | P2 | L | ☐ |
| I9 | Renommage rapide : double-clic et F2 (socle) ; miniature à faire (C13) | P1 | S | ◐ |
| I10 | Duplication d'un calque image avec sa pile d'opérations (aujourd'hui `duplicateCalque` copie formes et broderies, pas la source) | P2 | M | ☐ |
| I11 | Calque actif : le sélecteur d'image et l'arbre visent le même calque (socle C1) | P0 | S | ✅ |

### 4.3 Canevas Segmentation

| # | Amélioration | Prio | Coût | État |
|---|---|---|---|---|
| S1 | Segmentation ciblée du calque actif : chaque source garde sa segmentation (◐) ; C1 rend la cible explicite | P0 | S | ✅ |
| S2 | Segmentation multiple d'une sélection de calques (aujourd'hui : un seul ou tous) | P1 | M | ☐ |
| S3 | Affichage différencié actif / inactif / verrouillé (régions des autres calques en filigrane) | P1 | M | ☐ |
| S4 | Édition ciblée : les retouches de région ne visent que la source active (◐, `RegionController` élague à chaque changement de source) | P0 | S | ◐ |
| S5 | Masques par calque : afficher et peindre le `detail_mask` de la source active (◐ pinceau existant) ; liste des masques à faire | P2 | M | ◐ |
| S6 | Transférer une région segmentée vers un autre calque | P2 | L | ☐ |
| S7 | Fusion contrôlée de régions de calques différents | P2 | L | ☐ |
| S8 | Comparaison avant/après de la source active (◐ interrupteur « source » + opacité des régions) ; rideau de comparaison comme l'espace Image | P1 | S | ◐ |
| S9 | Historique sûr : un geste de calque ne provoque plus de resegmentation ni de rafraîchissement des régions (C2) | P0 | S | ✅ |
| S10 | Recalcul local d'une région sans resegmenter la source entière | P1 | L | ☐ |
| S11 | Raccourcis pour naviguer entre régions (Tab / Maj+Tab) | P2 | S | ☐ |

### 4.4 Canevas Vecteurs

| # | Amélioration | Prio | Coût | État |
|---|---|---|---|---|
| V1 | Calque de chaque forme lisible dans l'arbre (◐) et dans le panneau de forme | P1 | S | ◐ |
| V2 | Édition isolée : sommets d'une forme d'un calque verrouillé non saisissables (◐ `shapeEditLocked`) ; sélection en masse protégée (C3) | P0 | S | ✅ |
| V3 | Superposition lisible : teinte d'accent par calque sur les contours (jeton de thème) | P1 | M | ☐ |
| V4 | Nœuds affichés d'abord pour le calque actif | P2 | M | ☐ |
| V5 | Verrouillage des vecteurs terminés (C3/C4) | P0 | S | ✅ |
| V6 | Transfert intercalque par glisser-déposer dans l'arbre (◐) et protégé par le verrou (C4) | P0 | S | ✅ |
| V7 | Opérations booléennes limitées à la sélection explicite, calques verrouillés exclus | P1 | S | ☐ (exclusion des calques verrouillés acquise par C3 pour la sélection en masse ; le chemin des opérations booléennes reste à auditer) |
| V8 | Vectorisation partielle : un calque (◐) ou tous (◐) ; une sélection de calques à faire | P1 | M | ◐ |
| V9 | Comparaison image/vecteurs avec curseur d'opacité de la source | P1 | S | ☐ |
| V10 | Plusieurs formes par calque sans fusion involontaire (◐ : modèle forme ⊃ couches) | P0 | S | ◐ |
| V11 | Réordonner, isoler, renommer depuis l'onglet Calques (C5, C6, C8) | P1 | S | ✅ |

### 4.5 Canevas Points / numérisation

| # | Amélioration | Prio | Coût | État |
|---|---|---|---|---|
| P1 | Sélection des objets par calque : clic sur la ligne du calque (◐), rend le calque actif (C1), refusé si verrouillé (C3) | P0 | S | ✅ |
| P2 | Paramètres indépendants par objet (◐ inspecteur) ; un changement de calque ne touche aucun paramètre | P0 | S | ◐ |
| P3 | Calque actif mis en avant, autres atténués au canevas | P1 | M | ☐ |
| P4 | Aperçu isolé d'un calque : C6 (masquage par calque, une étape annulable) | P1 | S | ✅ |
| P5 | Recalcul ciblé : renommer/verrouiller ne reconstruisent plus la scène (C2) ; cache par objet de la séquence à étendre | P0 | M | ◐ |
| P6 | Ordre de broderie explicite : ordre des calques = clé, ordre du document = départage ; message de statut lors d'un réordonnancement | P0 | S | ✅ |
| P7 | Déplacer un objet vers un autre calque en gardant ses paramètres (◐ `SetShapesCalqueCommand` ne touche que `calque`) | P0 | S | ◐ |
| P8 | Édition groupée de la sélection (◐ inspecteur multi-objets) ; sélection par plage (C10) | P1 | S | ✅ |
| P9 | Indicateurs « à jour / obsolète / en calcul » par objet | P2 | M | ☐ |
| P10 | Historique sûr : une étape par geste de calque (C5, C6) | P0 | S | ✅ |
| P11 | Statistiques par calque : nombre d'objets livré dans la ligne de calque ; points, sauts et coupes à faire | P1 | M | ◐ |

### 4.6 Simulation / aperçu (mode de Points)

| # | Amélioration | Prio | Coût | État |
|---|---|---|---|---|
| R1 | Isolation d'un calque pendant la simulation : la simulation ne joue que les calques visibles ; C6 l'isole en un geste | P1 | S | ✅ (via la visibilité) |
| R2 | Visibilité temporaire hors document (masquer sans étape d'annulation) | P1 | M | ☐ |
| R3 | Correspondance de sélection : point simulé → objet → calque dans l'arbre | P1 | M | ☐ |
| R4 | Navigation rapide d'un calque à l'autre (sauter au premier point du calque suivant) | P2 | M | ☐ |
| R5 | Simulation ciblée d'une sélection de calques ou de séquences | P1 | M | ☐ |
| R6 | Comparaison source/rendu (rideau, comme l'espace Image) | P2 | M | ☐ |
| R7 | Ordre de couture affiché : badges d'ordre par calque | P1 | S | ☐ |
| R8 | Diagnostic par calque : points, couleurs, sauts, coupes | P1 | M | ☐ |
| R9 | Signalement des superpositions et discontinuités entre calques | P2 | L | ☐ |
| R10 | Cadrage et zoom conservés d'un espace à l'autre (◐ canevas unique partagé) | P1 | S | ◐ |

## 5. Restant prioritaire

1. **A11 / I4 / I6** — dessin composé des calques image visibles avec opacité par calque, dans
   `source_image_layer` : aujourd'hui une seule source est dessinée. P1, M.
2. **R2** — visibilité temporaire hors document pour la simulation et l'inspection (un « masque
   de vue » dans `display_filters`, jamais enregistré). P1, M.
3. **S3 / V3 / P3** — atténuer au canevas tout ce qui n'est pas dans le calque actif (jeton de
   thème, pas de couleur codée). P1, M.
4. **P11 / R8** — statistiques par calque (points, sauts, coupes) depuis `effective_sequence`.
5. **I7** — refuser les opérations d'image sur une source verrouillée.

## 6. Risques résiduels

- Le rang de calque restant la clé de couture, réordonner change l'ordre physique : c'est le
  modèle voulu (HP-OBJ-024), désormais signalé au statut, mais il n'existe pas de vue « ordre des
  calques ≠ ordre de couture ».
- Cliquer sur un calque qui porte une image change la source active : l'espace Segmentation
  rafraîchit alors ses régions (comportement voulu, une seule notion de calque courant).
- Le menu contextuel, les raccourcis et les icônes ne sont pas couverts par un test automatisé ;
  la recette visuelle dans Studio reste à faire.
