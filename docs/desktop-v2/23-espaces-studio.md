# 23 — Les quatre espaces du Studio (charpente V0)

Le Studio suit le pipeline réel **image → segmentation → vectorisation → points**. Chaque étape a
son **espace** dédié ; les espaces sont **séparés** (un canevas, une barre d'outils, des panneaux
chacun) mais **liés** (un changement amont a une incidence visible sur l'aval). Ce chapitre est le
**contrat** des quatre agents qui remplissent chacun un espace en parallèle.

## 1. Vue d'ensemble

```
Main.qml (fenêtre, raccourcis globaux, dialogues, palette, barre d'état)
 └─ SpaceHost.qml ── barre d'onglets + pastilles + bandeau amont + UN Loader
      ├─ ImageSpace.qml          (id "image")         Ctrl+1
      ├─ SegmentationSpace.qml   (id "segmentation")  Ctrl+2
      ├─ VectorSpace.qml         (id "vector")        Ctrl+3
      └─ PointsSpace.qml         (id "points")        Ctrl+4   (contenu historique, défaut)
```

- **Un seul espace est instancié à la fois** : donc un seul `GpuCanvas`. Changer d'espace détruit le
  précédent. Un espace ne garde AUCUN état qui doit survivre à son démontage : cet état va dans un
  contrôleur de contexte, dans le document (via `DocumentSession`), ou dans `workspace`.
- Le **document est unique** : image originale intacte + pile `ops` ; `segmentation` (remise à vide
  par toute nouvelle op d'image) ; `vector_objects` (lien `source_region`) ; `embroidery_objects`
  (lien `source_vector`) ; les points sont recalculés à la volée par `effective_sequence`.
- La **sélection** vit dans `DocumentSession::selection()` : elle traverse les espaces sans copie.
- La **caméra** (centre en mm de scène, zoom en px/mm) est sauvegardée par l'hôte avant la bascule
  (`GpuCanvas.viewState()` → `workspace.saveView`) et restaurée à l'arrivée
  (`GpuCanvas.restoreViewState`). Un nouveau document (`documentReplaced`) l'efface (re-cadrage).

## 2. Contrat d'un espace

Un fichier `qml/spaces/<X>Space.qml` est la racine d'un espace COMPLET :

```qml
Item {
    required property var workspace   // WorkspaceController (fourni par l'hôte)
    property bool active              // vrai une fois chargé et visible
    readonly property var canvas: spaceCanvas   // OBLIGATOIRE : le GpuCanvas de l'espace
    ...
}
```

- Il ne lit/écrit que les **contrôleurs de contexte** (`studio`, `editor`, `tools`, `actions`,
  `imageTools`, `regionTools`, `vectors`, `view`, ...), jamais les ids de `Main.qml`. Pour déclencher
  une commande globale : `actions.runCommand("cmd.file.open")` (même chemin que la palette).
- Toute mutation du document passe par `DocumentSession` (**une étape d'annulation** par geste).
  Interdits habituels : pas de logique métier dans le QML, pas d'objet QML par point, pas de shader
  personnalisé, jamais l'attaché `Drag`, raccourcis = `Shortcut` QML (indépendants du focus).
- **Couches du canevas** : `layers: ({ sourceImage, regions, vectors, stitches, selectionOverlays })`.
  Défaut : `stitches` et `selectionOverlays` seulement. V0 ne réalise que ces deux-là ;
  `sourceImage`, `regions`, `vectors` sont des **points d'accroche** (`CanvasLayers` dans
  `canvas_renderer.hpp`, transmis à chaque synchronisation de rendu) que l'agent concerné
  implémente dans le moteur de rendu (`canvas_renderer.cpp`, avec le Lead pour toute modif de
  `gpu_canvas.*`). Les coques ont `stitches: false` : leur canevas est vide tant qu'elles n'ont pas
  de contenu.
- **Panneaux transversaux** (Vue / Fils / Analyse / Simulation / règles) : `ViewPanel { canvasItem:
  <canevas de l'espace> }` est déjà dans chaque espace ; le menu Fichier (récents, imports, exports)
  et la palette sont dans `Main.qml`, donc partout accessibles.
- Le **préparateur historique** (`PreparePanel` : image, segmentation, régions, IA) est hébergé dans
  `ImageSpace.qml` en attendant que les agents Image et Segmentation le répartissent.

### Système de design (D1) : obligatoire dans les espaces

`test_studio_design` parcourt TOUT `qml/**.qml` (sous-dossiers `spaces/` compris) et échoue sur :

- un contrôle Qt non habillé (`CheckBox`, `TextField`, `SpinBox`, `ComboBox`, `Slider`, `Switch`,
  `Button`, `TextArea`, …) : utiliser `StudioCheckBox`, `StudioTextField`, `StudioSpinBox`,
  `StudioComboBox`, `StudioSlider`, `StudioSwitch`, `StudioButton`, `StudioLabel`, `StudioPanel` ;
- l'attaché `Controls.ToolTip.*` : un enfant `StudioToolTip { text: "…" }` ;
- une couleur en dur (`"#rrggbb"`) ou un `font.pixelSize: <nombre>` : jetons de `Theme.qml`
  (`Theme.accent`, `Theme.warning`, `Theme.error`, `Theme.bodySize`, `Theme.captionSize`,
  `Theme.panelPadding`, `Theme.controlHeight`, …). Exception documentée : `lint-ok:` en commentaire
  sur la ligne pour une donnée de document (ex. couleur d'un fil).
- Chaque nouveau composant `Studio*` doit être déclaré dans `qml/qmldir`.

Transversal dans `Main.qml` (jamais dans un espace) : palette de fenêtre, palette de commandes,
préférences, aide, compagnon, barre d'état (indicateur de rendu GPU/CPU, « Réaliste · GPU/CPU »).
Spécifique au centre historique et donc dans `PointsSpace.qml` : gestes de numérisation (famille I :
`digitize: root.digitizeController`, `HandlesOverlay`), `DigitizeHub`, `ViewPanel` (bascule réaliste).

### Sous-composants et CMake

Les sous-composants vont dans `qml/spaces/<x>/…` (minuscule) et se déclarent dans
`studio/CMakeLists.txt` (`studio_qml_files`, **une ligne d'ajout**). Les tests chargent tout
`qml/**.qml` par `GLOB_RECURSE` : rien à toucher côté tests pour embarquer vos fichiers.

## 3. États d'étape, bandeau, mise à jour de l'aval

`WorkspaceController` (adaptateur Qt, sans logique métier) expose :

| API | Rôle |
|---|---|
| `spaces` | `[{id, label, shortcut, state, detail, actionLabel, actionEnabled, index}]` |
| `current`, `setCurrent(id)` | espace courant (défaut `points`) |
| `setStageStatus(id, state, detail)` | l'espace `id` **publie** l'état de SON étape (`empty`, `upToDate`, `stale`, `blocked`) ; il l'emporte sur le calcul de base |
| `clearStageStatus(id)` | retire la publication : le calcul de base reprend |
| `setStageAction(id, label, enabled)` | libellé du bouton « mettre à jour » du bandeau ; vide = pas de bouton |
| `requestUpdate(id)` / signal `updateRequested(id)` | le bouton du bandeau demande à l'espace `id` de se mettre à jour |
| `saveView(map)` / `view` | caméra conservée entre espaces (géré par l'hôte) |
| `segmentationDirty`, `dirtyReason` | corrections de régions non répercutées (alimentés par `regionTools.stale` dans `Main.qml`) |

**Calcul de base** (`core/stage_status.{hpp,cpp}`, pur, testé par `test_stage_status.cpp`) :

- image : vide sans image, à jour sinon ;
- segmentation : bloquée sans image, vide sans segmentation, **obsolète** si des corrections de
  régions ne sont pas répercutées (`segmentationDirty`, la même notion que `GuideInput` du guide de
  parcours), à jour sinon ;
- vecteurs : vide sans forme, **obsolète** si la segmentation est obsolète ou si une forme à
  `source_region` ne retrouve plus sa région (les formes dessinées à la main n'en ont pas) ;
- points : vide sans objet, **obsolète** si les formes le sont ou si un objet perd son `source_vector`.

**Qui publie quoi.** Chaque espace est responsable de l'état de SA propre étape et de l'action qui la
met à jour :

1. l'espace publie `setStageStatus(spaceId, …)` quand il en sait plus que le calcul de base (ex.
   résultat d'une tâche en cours : `blocked`) et `clearStageStatus` ensuite ;
2. l'espace publie `setStageAction(spaceId, qsTr("Mettre à jour les formes"))` ;
3. le **bandeau** de l'espace X affiche l'état de l'étape AMONT (X−1) quand elle est `stale` ou
   `blocked`, avec le bouton d'action de X ; le clic émet `updateRequested(X)` ;
4. l'espace X écoute `Connections { target: workspace; function onUpdateRequested(id) { if (id ===
   spaceId) … } }`, régénère son étape depuis l'amont en **une étape d'annulation**, et la pastille
   repasse à jour (le calcul de base se met à jour tout seul à chaque `studio.changed`).

La pastille de l'onglet (couleur + infobulle `detail` + `Accessible.name`) reflète `state`.

## 4. Propriété des fichiers

Par espace `<x>` ∈ {image, segmentation, vector, points} (X = `Image`, `Segmentation`, `Vector`,
`Points`), l'agent de l'espace possède :

- `apps/desktop_v2/studio/qml/spaces/<X>Space.qml` ;
- `apps/desktop_v2/studio/qml/spaces/<x>/…` (sous-composants) ;
- `apps/desktop_v2/studio/<x>_space_controller.{hpp,cpp}` (adaptateur Qt propre, enregistré comme
  propriété de contexte dans `main.cpp` : une ligne d'ajout) ;
- `tests/unit/desktop_v2/test_studio_space_<x>.cpp` ;
- `parite-studio/statuts/v01-<x>.csv` (statuts de parité de l'espace).

**Fichiers PARTAGÉS : seul le Lead les modifie** (un agent d'espace demande ; hors lignes d'ajout) :

- `apps/desktop_v2/studio/qml/Main.qml` ;
- `apps/desktop_v2/studio/qml/spaces/SpaceHost.qml` ;
- `apps/desktop_v2/studio/workspace_controller.{hpp,cpp}` ;
- `apps/desktop_v2/core/stage_status.{hpp,cpp}` et son test ;
- `apps/desktop_v2/studio/gpu_canvas.{hpp,cpp}`, `canvas_renderer.hpp` (les couches `sourceImage` /
  `regions` / `vectors` se branchent dans `canvas_renderer.cpp`, à coordonner) ;
- `apps/desktop_v2/studio/CMakeLists.txt` et `tests/unit/desktop_v2/CMakeLists.txt` : **lignes
  d'ajout seulement** (vos fichiers, votre cible de test), sans toucher aux lignes des autres ;
- les fichiers QML historiques partagés (`Theme.qml`, `StudioButton.qml`, `ViewPanel.qml`, …) :
  modification minimale, signalée au Lead.

## 5. Tests

- `test_stage_status.cpp` (Catch2, cœur) : règles du calcul de base.
- `test_studio_workspaces.cpp` (QTest, vrai `Main.qml`) : quatre espaces, un seul canevas à la fois,
  conservation de la caméra et de la sélection, raccourcis Ctrl+1..4, pastilles et bandeau sur un
  document réel dont on invalide la segmentation.
- Les tests historiques (`shell`, `edit`, `files`, `inspector`, `view`, `image`, `digitize`, `tools`,
  `companion`) chargent `Main.qml` : l'ancien centre est dans `PointsSpace` (point de départ par
  défaut) ; un test qui vise un autre espace commence par `workspace.setCurrent(id)` (le
  `WorkspaceController` est un enfant du `Window` : `window->findChild<WorkspaceController*>()`).
- Un test d'espace ne dépend pas des autres espaces : il charge `Main.qml`, bascule sur son espace,
  vérifie son comportement ET l'état publié / le bandeau qu'il provoque.

## 6. Limites connues de la charpente

- Les couches `sourceImage`, `regions`, `vectors` du canevas sont des **crochets** : rien n'est dessiné.
- Les trois coques (Image, Segmentation, Vecteurs) affichent « espace en construction » ; l'espace
  Image héberge le préparateur historique.
- La session mémorise l'espace courant (`space`), pas encore la caméra par espace.
