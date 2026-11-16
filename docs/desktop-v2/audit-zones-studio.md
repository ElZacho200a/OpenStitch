# Audit par zone d'OpenStitch Studio — point de reprise

Audit fonctionnel de `apps/desktop_v2/studio`, zone par zone, dans l'ordre de priorité
Segmentation → Vecteurs → Image → Points. Pour chaque zone : (a) les outils nécessaires sont-ils
là, (b) chaque bouton est-il branché et que fait-il vraiment, (c) la zone s'inscrit-elle dans le
parcours Image → Segmentation → Vecteurs → Points.

Ce fichier est écrit pour qu'un autre agent reprenne le travail sans relire l'historique.

## 1. État au 2026-11-16

- Branche : `claude/studio-audit-zones` (partie de `main` b0e9e375), dans le worktree
  `.claude/worktrees/open-stitch-studio-ui-ux-56ea70`. Rien n'est poussé, aucune PR.
- `build/msvc` existe (Debug). Le C++ des correctifs **compile** (cibles `openstitch_v2_studio`,
  `test_desktop_v2_studio_space_segmentation`, `test_desktop_v2_studio_space_vector`).
- **Aucun test n'a encore été exécuté sur les correctifs.** Les cinq nouveaux cas de test
  n'ont jamais tourné : ils peuvent échouer pour une raison de test et non de code.
- Les trois retouches QML de la zone Image (§4) ont été faites après la dernière compilation :
  elles ne sont ni compilées dans le qrc ni essayées.
- Rien n'a été essayé dans l'application réelle. Projet d'exemple à régénérer :
  `build\msvc\apps\cli\Debug\openstitch-cli.exe project digitize tests/fixtures/ai-benchmark/marine-synth-01/source.png -o marine.osp`
  (19 régions, 18 objets).
- clang-format 18.1.8 passé sur les `.cpp/.hpp` modifiés, mais le code n'a pas été recompilé
  après ce formatage.
- Notion non mis à jour ; `qa/checklist.csv` non rapproché (le skill `openstitch-studio-ux`
  demande de rattacher chaque constat à un ID existant avant d'en créer un).

## 2. Méthode et résultat du croisement

Croisement automatique de tous les accès `contexte.membre` des fichiers QML avec les
identifiants des en-têtes des contrôleurs (`tools`, `vectors`, `vectorStage`, `editor`,
`imageTools`, `imageSpace`, `regionTools`, `segmentationSpace`, `studio`, etc., y compris les
alias `toolCtl`, `vec`, `seg`, `stage`) : **aucun appel vers une méthode ou une propriété
inexistante**. Les coquilles trouvées sont donc de comportement, pas de câblage manquant.
Elles ont été trouvées par lecture du QML et des `.cpp`, pas par essai dans l'application.

## 3. Zone Segmentation

Fichiers : `qml/spaces/SegmentationSpace.qml`, `spaces/segmentation/SegmentationPanels.qml`,
`RegionLayerControls.qml`, `qml/RegionTools.qml`, `SegmentationSection.qml`, `AiSection.qml` ;
`segmentation_space_controller`, `region_controller`, `image_controller`.

### Coquilles corrigées sur la branche (à valider)

| # | Constat | Correctif |
|---|---|---|
| S1 | « Re-segmenter » ignorait le lissage : le QML envoie `smoothing_radius_px`, `RegionController::regenerate` lisait `smoothing_px`. Aucune borne non plus. | `region_controller.cpp` lit `smoothing_radius_px` (repli `smoothing_px`) et borne comme `ImageController::optionsFrom`. |
| S2 | Double boîte de confirmation : `confirmRequested` était capté à la fois par `RegionTools.qml` et par `SegmentationSpaceController`. Confirmer depuis la carte du canevas après un « Re-segmenter » lançait une simple régénération ; confirmer depuis l'onglet après « Mettre à jour les formes » rejouait une ancienne demande (éventuellement une re-segmentation) ; l'autre boîte restait affichée. | La demande en attente vit dans `RegionController` (`confirmText`, `confirmRegeneration()`, `cancelConfirmation()`), les deux vues s'y lient. |
| S3 | « Choisir la cible… » ne marchait que dans la liste : un clic sur le canevas ou la carte remplaçait la région source. | Propriété `mergeTargetMode` dans `RegionController`, honorée par `selectAtPixel` ; `choosingMergeTarget` du QML devient une lecture seule de cette propriété. |
| S4 | L'aperçu de segmentation restait actif en changeant d'onglet ou d'espace : il masque la couche des régions, donc on retouchait des régions invisibles. | Annulation de l'aperçu à la sortie de l'onglet 0 (`SegmentationPanels.qml`) et à la désactivation de l'espace. |
| S5 | `selectAll()` existait sans bouton ; « Aucune » toujours actif. | Bouton « Toutes », « Aucune » désactivé sans sélection. |
| S6 | Le champ de couleur de « Recolorer » ne reflétait pas la région choisie. | Il prend la couleur de l'unique région sélectionnée. |
| S7 | Sélecteur de calque affiché avec un seul calque (`> 0`) alors que Vecteurs utilise `> 1`. | `> 1` partout (Segmentation et Image). |

### Constats non corrigés

- **Démarrage de l'aperçu en direct, à vérifier dans l'application** : `SegmentationSection`
  relance l'aperçu sur `onMaxColorsChanged` etc. Si ces gestionnaires se déclenchent à
  l'initialisation des liaisons, l'aperçu part tout seul 350 ms après le chargement du panneau
  et remplace les régions du projet sur le canevas dès l'ouverture de l'espace. Non vérifié.
- **Cohérence du parcours** : « Mettre à jour les formes » (carte du canevas) appelle
  `regionTools.regenerate(false)`, qui régénère directement les *objets de broderie*, alors que
  l'espace Vecteurs propose « Mettre à jour depuis la segmentation » (formes seulement) puis
  « Convertir en broderie ». Deux chemins concurrents pour la même étape, dont un saute
  l'étape Vecteurs. Décision de conception à prendre, non tranchée ici.
- Après une première segmentation, l'étape passe en « stale » avec le texte « Corrections de
  régions non répercutées sur les formes : l'image a été re-segmentée », trompeur quand il
  n'y a encore aucune forme.
- Quatre entrées pour segmenter (« Segmenter l'image » du bandeau, de la carte vide, de la
  carte du canevas, « Appliquer » de l'onglet) plus « Re-segmenter » qui fait autre chose
  (segmente et régénère les objets). À clarifier.
- Onglet IA : seul « Sonder les backends » existe ; l'analyse IA n'est pas migrée.
- Pinceau et gomme au canevas : pas de curseur montrant le rayon ; le refus (« choisissez
  d'abord UNE région ») ne s'affiche que dans l'onglet Régions.
- Nettoyer / Reboucher / Fusionner ne sont pas désactivés pendant un calcul (`busy`).

## 4. Zone Vecteurs

Fichiers : `qml/spaces/VectorSpace.qml`, `spaces/vector/VectorShapePanel.qml`, `ToolRail.qml`,
`ToolOptions.qml`, `EditBar.qml`, `LayerPanel.qml`, `ObjectTree.qml`, `CanvasContextMenu.qml` ;
`vector_space_controller`, `vector_controller`, `tool_controller`, `edit_controller`.

### Coquilles corrigées sur la branche (à valider)

| # | Constat | Correctif |
|---|---|---|
| V1 | « Aller à la région » ouvrait Segmentation sans rien sélectionner : `selectOriginRegion()` écrit une cible `Region` dans la sélection de session, que `RegionController` (sélection propre) ne lisait jamais. | `RegionController::adoptSessionSelection()`, appelé par `SegmentationSpaceController::attach`. La même cible n'est reprise qu'une fois. |
| V2 | Raccourcis affichés faux dans `EditBar` : « R » et « Maj+R » pour pivoter (les vrais sont Ctrl+R / Ctrl+Maj+R, « R » est l'outil Rectangle), « E » pour la pipette (le vrai est « I », « E » est l'Ellipse). | Libellés corrigés, raccourcis des miroirs ajoutés. |
| V3 | Pas de menu contextuel dans l'espace Vecteurs (`CanvasContextMenu` n'était instancié que dans Points). | Menu branché sur `contextMenuRequested` ; dans `gpu_canvas.cpp`, le clic droit en mode vecteurs passe par le sélecteur de formes au lieu de la scène des points. |
| V4 | `ToolOptions.qml` : le titre « ACCROCHAGE » était placé au-dessus des réglages du pinceau de déformation. | Titre déplacé au-dessus des cases d'accrochage. |
| V5 | `objectName: "vectorMessage"` en double (`ToolOptions` et `VectorShapePanel`) ; signal `updateRequested` de `VectorShapePanel` jamais émis. | Renommé `vectorOpsMessage` ; signal supprimé. |

### Constats non corrigés

- Le menu contextuel repris de Points contient « Type de points » et « Ordre de couture »,
  notions de broderie peu parlantes sur des formes.
- `ViewPanel` est instancié dans Image, Segmentation et Vecteurs avec ses raccourcis
  Ctrl+L (simulation) et Ctrl+Maj+T (rendu réaliste) actifs alors que ces espaces ne dessinent
  pas les points. À vérifier dans l'application.
- `objectName` en double entre `ViewPanel` et `PointsPanelBar` (`realisticButton2`,
  `simulationButton`) et `viewHub` dans chaque espace.
- Simplifier (0,1), Lisser (0,15), Combler trous (4) : valeurs fixes sans réglage.

## 5. Zone Image

Fichiers : `qml/spaces/ImageSpace.qml`, `spaces/image/*.qml`, `qml/ImagePrepSection.qml` ;
`image_space_controller`, `image_controller`. Lecture seule du QML, contrôleurs survolés.

Corrigé (non compilé, non essayé) : infobulle des boutons d'échelle qui disait « par 0.5 % »
pour 50 % ; bouton « Annuler l'aperçu » qui annule en réalité le calcul en cours ; valeur des
curseurs affichée sans arrondi ; sélecteur de calque affiché avec un seul calque.

Non corrigé : pas de remise à zéro des curseurs d'un traitement ; pas de suppression de fond
ou de transparence ; `ImageSizeSection` passe une chaîne à `setImageWidthMm(double)` et ignore
le message de refus renvoyé.

## 6. Zone Points

**Non auditée.** Seule vérification : les libellés de raccourcis de `PointsPanelBar`,
`PointsToolRail` et `ViewPanel` correspondent à des `Shortcut` existants.

## 7. Tests ajoutés (jamais exécutés)

Dans `tests/unit/desktop_v2/test_studio_space_segmentation.cpp` :
`resegmentHonoursThePanelSmoothing`, `overwriteConfirmationIsSharedAndRunsTheRequestedAction`,
`mergeTargetCanBeChosenOnTheCanvas`, `sharedRegionSelectionIsAdoptedOnEntry`,
`previewIsDroppedOutsideTheSegmentationTab`.

Points fragiles connus : le premier suppose que les lissages 0 et 10 donnent un nombre de
régions différent sur l'image de test à quatre aplats (sinon il échoue avec un message
explicite, il faudra une image plus détaillée) ; le deuxième suppose que supprimer une région
puis re-segmenter rend le nombre de régions initial.

Aucun test n'a été ajouté pour V2 à V5 ni pour la zone Image.

## 8. Prochaines actions, dans l'ordre

1. Recompiler puis lancer :
   `.\scripts\qrun.ps1 build cmake --build --preset msvc-debug --target openstitch_v2_studio test_desktop_v2_studio_space_segmentation test_desktop_v2_studio_space_vector`
   puis `ctest --preset msvc-debug -R "studio_space_(segmentation|vector)" --output-on-failure`.
2. Lancer aussi `test_desktop_v2_studio_image`, `test_desktop_v2_studio_files` et
   `test_desktop_v2_studio_workspaces`, qui chargent les fichiers QML modifiés.
3. Essayer chaque correctif dans l'application sur `marine.osp`, et trancher le point
   « démarrage de l'aperçu en direct » (§3).
4. Auditer la zone Points, puis approfondir Image (contrôleurs).
5. Suite complète, contrôle clang-format 18 de tout le dépôt, rapprochement avec
   `qa/checklist.csv`, commit final, PR contre `main`, mise à jour Notion.

## 9. Échecs locaux connus avant ce travail (non revérifiés)

- `test_desktop_v2_studio_realistic_gpu` échouait en local après l'ajout du tambour (PR 349).
- `test_studio_space_segmentation::stagePublishedAndDownstreamUpdate` échoue en local en rendu
  logiciel, déjà avant ces changements : à ne pas attribuer d'office aux correctifs ci-dessus,
  mais S2 touche ce parcours (« Mettre à jour les formes »), donc à regarder de près.
- `docs/desktop-v2/08-visual-feel-roadmap.md` dit encore « pas d'ombres ».
