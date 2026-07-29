# Refonte de l'interface — Phase 1 : audit UX et technique

Public : mainteneur, contributeur. Cet audit décrit l'interface **réellement
présente** dans `apps/desktop/` au moment de la refonte, sans rien inventer.
Chaque constat renvoie au code (`fichier:symbole`). Il sert de base à la
spécification (`docs/ui-redesign-specification.md`) et au plan
(`docs/ui-redesign-plan.md`).

État de référence : commit `449c0a1` + travail non commité (filtres, rendu
couleur, poignée de rotation, menu contextuel). 161 tests CTest au vert.

---

## 1. Inventaire des écrans et composants existants

Toute l'interface tient dans **6 fichiers** (`apps/desktop/`), dont un fichier
géant :

| Fichier | Lignes | Rôle |
|---|---:|---|
| `main_window.cpp` | ~1994 | **tout** : menus, docks, dialogues inline, rendu, interactions, slots |
| `main_window.hpp` | ~189 | déclarations `MainWindow` |
| `canvas_view.cpp/.hpp` | ~270 | `QGraphicsView` : zoom, pan, grille, cadre, signaux souris |
| `ruler.cpp/.hpp` | ~125 | règles graduées en mm |
| `import_dialog.cpp/.hpp` | ~125 | dialogue de taille physique à l'import |
| `brightness_dialog.cpp/.hpp` | ~80 | dialogue luminosité/contraste (aperçu live) |
| `node_handle.hpp` | ~42 | poignée `QGraphicsEllipseItem` (nœuds + rotation) |

### Structure de la fenêtre (`MainWindow::MainWindow`, ligne 65)

```
QMainWindow (1100×800)
├── centralWidget : QGridLayout 2×2 (spacing 0)
│   ├── (0,0) QLabel « mm »   (0,1) Ruler horizontale
│   ├── (1,0) Ruler verticale (1,1) CanvasView   ← le canevas
├── menuBar : 7 menus
├── QToolBar « Simulation » (haut, addToolBar, caché par défaut)
├── statusBar : message + cursorLabel_ permanent (x/y mm)
└── 3 QDockWidget :
    ├── analysisDock_  (droite, caché)   — liste de problèmes
    ├── orderDock_     (gauche, caché)   — ordre de couture
    └── filterDock_    (droite, caché)   — filtres d'affichage
```

Il n'existe **pas** : de barre d'outils principale, de palette d'outils
verticale, de panneau de propriétés/inspecteur, d'arbre de document, d'indicateur
de workflow, d'état d'accueil (canevas vide = blanc), de menu Aide, de thème
centralisé, de persistance d'interface.

### Dialogues

Tous **modaux**. Deux sont des classes dédiées (`ImportDialog`,
`BrightnessDialog`) ; **tous les autres sont construits inline** dans des slots de
`main_window.cpp` avec un `QDialog` + `QFormLayout` local :

- taille physique image (`ImportDialog`) — aperçu **absent** (pas de vignette) ;
- luminosité/contraste (`BrightnessDialog`) — **aperçu live** via signal
  `previewRequested` ;
- quantification (`quantizeColors`, inline) ;
- création contour (`createRunningStitchObject`, inline) ;
- création tatami (`createTatamiObject`, inline) ;
- création satin (`createSatinObject`, inline) — avertissement largeur inline ;
- orientation (`changeFillAngle`, `QInputDialog::getInt`) ;
- statistiques (`showStatistics`, `QMessageBox::information` — texte brut) ;
- recoloration région (`QColorDialog`).

---

## 2. Inventaire des actions (par menu)

| Menu | Actions | Notes |
|---|---|---|
| **Fichier** | Ouvrir image (Ctrl+O), Enregistrer projet (Ctrl+S), Ouvrir projet, Exporter DST, Importer DST, Quitter (Ctrl+Q) | pas de « Nouveau », pas de récents |
| **Édition** | Annuler (Ctrl+Z), Rétablir (Ctrl+Y) | libellé dynamique nommant l'action |
| **Image** | Niveaux de gris, Luminosité/contraste, Débruitage léger/moyen, Quantifier, Symétrie H/V, Rotation 90° H/AH, Recadrer (mode) | 11 actions à plat |
| **Segmentation** | Segmenter, Afficher la carte, Fusionner (mode), Supprimer région (Suppr), Recolorer région, Vectoriser région | mélange réglages / actions / modes |
| **Broderie** | Numérisation auto, Créer contour, Créer tatami, Créer satin, Orientation du remplissage, Convertir satins→tatami, Statistiques | 7 actions |
| **Affichage** | Calques ▸ (Image, Carte, Vecteurs, Broderie), Zoom +/−, Ajuster | |
| **Analyse** | Analyser le motif (F5) | |
| **Aide** | *(absent)* | |

Total ≈ 35 actions. `updateActions()` (ligne ~1590) gère l'activation
contextuelle via des listes (`imageActions_`, `regionActions_`) et des tests
directs — logique correcte mais dispersée.

---

## 3. Parcours principal actuel (flux utilisateur)

1. **Démarrage** → canevas blanc, cadre rouge 100×100, message barre d'état
   « Ouvrez une image… ». Aucune action visible n'invite à commencer (il faut
   trouver Fichier ▸ Ouvrir).
2. **Ouvrir image** → `ImportDialog` (taille mm, ratio) **sans aperçu**.
3. **Préparer** → menu Image (opérations non destructives, pile `ops`).
4. **Quantifier** → dialogue nombre de couleurs.
5. **Segmenter** → dialogue (couleurs, taille min région).
6. **Sélection région** → clic canevas ; stats en **barre d'état** (fugaces).
7. **Éditer régions** → Fusionner (mode), Supprimer, Recolorer.
8. **Vectoriser** → crée un objet vectoriel.
9. **Créer objet broderie** → menu Broderie + dialogue **à la création
   uniquement**.
10. **Régler** → *quasi impossible après coup* (voir §problèmes).
11. **Ordre** → dock Ordre (monter/descendre, verrou, stratégie).
12. **Simuler** → toolbar haut (lecture/curseur).
13. **Analyser** → F5 → dock Analyse.
14. **Exporter** → DST (pas de résumé pré-export) ou `.osp`.

Sélection **éclatée en trois** mécanismes non unifiés :
`selectedRegion_` (segmentation), `selectedObject_` (objet vectoriel, clic
canevas), `selectedEmbroidery_` (dock Ordre). Il n'existe pas de notion unique
de « l'élément sélectionné » exploitable par un inspecteur.

---

## 4. Problèmes d'ergonomie (constats fondés sur le code)

**P1 — Aucun panneau de propriétés : les paramètres ne sont réglables qu'à la
création.** Un `QDialog` inline s'ouvre à la création
(`createTatamiObject` etc.), puis **il n'existe aucun chemin** pour rééditer le
`row_spacing` d'un tatami, la `density` d'un satin, la `stitch_length` d'un
contour. Seuls l'**angle** du tatami et le **type** sont modifiables après coup.
`SetStitchTypeCommand` **réinitialise** les paramètres aux valeurs par défaut.
→ C'est le manque le plus lourd : impossible d'itérer sur un réglage.

**P2 — Résultats fugaces en barre d'état.** Les stats de région (aire, pixels,
RGB) et de génération s'affichent en `statusBar()->showMessage` et disparaissent.
Rien de persistant ne montre l'état de l'objet courant.

**P3 — Tout paramètre passe par une modale.** Même un changement d'angle ouvre un
`QInputDialog`. Aucun réglage « en ligne ». Charge d'interaction élevée.

**P4 — Modes ad-hoc invisibles.** `cropMode_` et `mergeMode_` sont des booléens
sans indicateur permanent ni curseur dédié fiable ; le mode fusion n'a qu'un
libellé de menu coché. Rien n'explique « le prochain clic choisit la cible ».
Pas de sortie par Échap.

**P5 — Découverte difficile.** Les fonctions clés (changer le type, orientation,
filtres) sont réparties entre menus, clic droit et docks, sans barre d'outils ni
inspecteur pour les rendre visibles. Un débutant ne voit pas le workflow.

**P6 — Trois docks concurrents (Analyse, Ordre, Filtres), tous « cachés par
défaut », apparaissant à des bords différents.** Pas de cohérence de placement ni
de regroupement logique (structure du document vs vérification).

**P7 — La toolbar Simulation apparaît/disparaît** (`updateSimulationRange`),
décalant la mise en page dès qu'une séquence existe/n'existe plus.

**P8 — Statistiques dans une `QMessageBox` modale** en texte `\n`-séparé, au lieu
d'un panneau consultable pendant le travail.

**P9 — Pas d'état d'accueil.** Le canevas vide n'offre aucune porte d'entrée.

**P10 — Pas de suivi « modifié ».** Aucun `isWindowModified`, aucun titre `*`,
aucune protection à la fermeture avec des changements non enregistrés. Aucun
rappel d'enregistrer le `.osp` avant export DST.

**P11 — Pas de résumé pré-export DST.** `exportDst` écrit directement (le moteur
refuse seulement la séquence vide) ; l'utilisateur ne voit ni dimensions, ni
dépassement de cadre, ni nombre de couleurs avant d'exporter.

**P12 — Import sans aperçu.** `ImportDialog` n'affiche pas la vignette, ni le
mm/pixel résultant, ni d'alerte si l'image dépasse le cadre.

**P13 — Terminologie et retours par emoji.** Analyse (`⛔ ⚠ ℹ ✓`), Ordre (`🔒`),
Simulation (`▶ ⏸`) reposent sur des emojis — contraire à la direction artistique
visée et à « pas d'info portée seulement par un pictogramme décoratif ».

---

## 5. Problèmes d'accessibilité

**A1 — Aucun `accessibleName`/`accessibleDescription`** dans tout le code desktop
(grep : 0 occurrence). Les contrôles sans libellé (poignées, boutons icône
futurs) ne seront pas annoncés.

**A2 — Focus clavier non stylé.** Aucun style de focus ; on dépend du focus natif
Qt, peu visible sur le canevas et les listes.

**A3 — Navigation clavier incomplète.** Pas de raccourci pour les outils/modes,
pas d'Échap pour sortir des modes crop/fusion, pas de `Tab` pour masquer les
panneaux. Les modes ne sont pas au clavier.

**A4 — Information portée uniquement par la couleur** à plusieurs endroits :
gravité d'analyse (préfixe emoji + texte, mais pas d'icône typée cohérente),
type de point (aucune icône — distingué par la couleur du fil au rendu),
sélection (couleur de contour bleue seule).

**A5 — Poignées trop petites / cible réduite.** `NodeHandleItem` = cercle 8 px,
zone cliquable = la même. Pas de marge d'interaction élargie.

**A6 — Contraste des repères sur canevas** non garanti : la sélection (bleu
`30,90,200`), les poignées (blanc + bleu) et les sauts (orange) peuvent manquer
de contraste sur une image de fond de couleur proche. Pas de stratégie
double-contour (halo clair + trait sombre).

**A7 — Pas de thème sombre**, pas de tokens de contraste centralisés.

---

## 6. Incohérences visuelles

- Couleurs **codées en dur** dispersées : fond canevas `235,235,238`
  (`canvas_view.cpp`), grille `0,0,0,40`, axes `120,120,160`, cadre `200,60,60`,
  points/sauts/nœuds/sélection dans `main_window.cpp`
  (`25,25,45` / `200,120,30` / `30,90,200`…), warning `#8a5a00` inline.
- Aucune constante partagée : les mêmes intentions (accent, sélection) ont des
  valeurs différentes selon le fichier.
- Pas de hiérarchie typographique (police par défaut partout, sauf règles 7.5 pt).
- Espacements hétérogènes (layouts `QFormLayout`/`QVBoxLayout` sans marges
  centralisées).

---

## 7. Fonctions difficiles à découvrir / trop de clics

| Fonction | Accès actuel | Coût |
|---|---|---|
| Changer un paramètre d'un objet existant | **impossible** (sauf angle/type) | ∞ |
| Changer l'orientation | menu Broderie **ou** clic droit **ou** poignée | dispersé |
| Sélectionner un objet de broderie | uniquement via dock Ordre | non évident |
| Voir les stats | `QMessageBox` modale (Broderie ▸ Statistiques) | bloquant |
| Filtres d'affichage | dock caché, à ouvrir | découverte faible |
| Vectoriser | menu Segmentation, région sélectionnée | ok |
| Fusionner régions | mode + clic, sans guidage | fragile |

---

## 8. Types d'objets manipulés (rappel métier, source de vérité `document::Project`)

1. **Image** (`project.original` + pile `ops`, résultat `processed_`).
2. **Segmentation** (`project.segmentation`) → **régions** (`RegionId`, couleur,
   pixels).
3. **Objet vectoriel** (`VectorObject` : `paths` = `PathSet{outer, holes}`,
   `source_region`, `rgb`, `visible`).
4. **Objet de broderie** (`EmbroideryObject` : `StitchParams` =
   variant `Running|Tatami|Satin`, `rgb`, `source_vector`, `visible`, `locked`).
5. **Séquence de points** (`stitch::StitchSequence`, régénérée, chaque commande
   porte `source` = ObjectId).

Distinction essentielle mal rendue par l'UI actuelle : **région ≠ objet vectoriel
≠ objet de broderie ≠ points**.

---

## 9. Interactions disponibles dans le canevas (`canvas_view.cpp` + slots)

- **Zoom** molette ancrée curseur (`wheelEvent`), bornes 0,2–400 px/mm.
- **Pan** `ScrollHandDrag` (clic-glisser).
- **Recadrage** `RubberBandDrag` en `cropMode_` → `cropSelectedMm`.
- **Clic gauche** → `canvasClickedMm` → `onCanvasClicked` : sélectionne un objet
  vectoriel (priorité) sinon une région ; ignore les clics sur item `Movable`.
- **Clic droit** → `canvasContextMenu` → menu (type de points, orientation,
  calques).
- **Poignées de nœuds** (`NodeHandleItem`) : déplacement → `MoveNodeCommand`
  (différé via `QTimer::singleShot`, cf. correctif crash).
- **Poignée de rotation** (tatami) : axe + bouton → `SetFillAngleCommand` (différé).
- **Rendu deux couches** : `renderBase` (image/vecteurs/régions/poignées/gizmo) et
  `renderStitches` (points par couleur d'objet, filtrés). À préserver
  (performance).

Manques : pas d'outil de sélection explicite, pas de mesure, pas de « fit
sélection », pas d'état de survol des objets, modes non unifiés.

---

## 10. Contraintes Qt 6 Widgets

- `CMAKE_AUTOMOC ON` ; `Qt6::Widgets` uniquement (LGPL, lien dynamique,
  `windeployqt`). Pas de QML, pas de Quick.
- `QGraphicsScene/View` déjà optimisés (`DontSavePainterState`,
  `SmartViewportUpdate`, `CacheBackground`). **À conserver.**
- Docks : `QDockWidget` repositionnables/flottants nativement — on peut fiabiliser
  leur placement par défaut et sauver l'état (`saveState`).
- Style : possible via `QStyle`/`QProxyStyle`, palette (`QPalette`) et QSS
  **ciblé**. Éviter une QSS monolithique. Le dessin du canevas reste en
  `QPainter`.
- `QSettings` disponible (aucune dépendance) pour la persistance UI.
- Icônes : pas de bibliothèque embarquée ; à ajouter (SVG monochromes, licence
  compatible Apache-2.0, p. ex. jeu MIT/Apache) ou dessin `QPainter`.
- Accessibilité : `QAccessible`, `setAccessibleName/Description` disponibles.

---

## 11. Dépendances interface ↔ cœur métier (à préserver)

L'application **ne contient aucune logique métier** (règle projet, ADR). Elle
câble des bibliothèques :

- image : `image::apply_pipeline`, `ImageOp` ;
- segmentation : `segmentation::segment`, `merge/remove/recolor_region`,
  `render_map` ;
- vectorisation : `vectorization::vectorize_region` ;
- génération : `stitch_generation::generate_sequence`, `fill_tatami`,
  `fill_satin`, `rails_from_contour` ;
- analyse : `stitch_analysis::analyze` ; stats : `stitch::compute_stats` ;
- ordre : `optimization::compute_cost`, stratégies ;
- I/O : `project_io::save/load`, `formats::` DST ;
- **toute mutation passe par `commands::` + `UndoStack`** ; `document::Project`
  est la seule vérité ; les points sont **régénérés** (`refreshImage`).

Contrainte de refonte : la nouvelle UI **réorganise** ces appels, n'en
réimplémente aucun, et **n'ajoute pas de seconde vérité métier**. Un panneau de
propriétés qui édite les paramètres devra passer par des **commandes** (une
généralisation de `SetFillAngleCommand` vers un `SetStitchParamsCommand`).

---

## 12. Risques de régression

| Risque | Origine | Mitigation |
|---|---|---|
| Casser le rendu deux couches (perf) | refonte `renderBase/renderStitches` | ne pas toucher la logique de couches ; extraire sans changer le flux |
| Casser la sélection éclatée | unification sélection | introduire un modèle de sélection sans supprimer les slots existants d'un coup |
| Régression undo/redo | édition de params en ligne | passer par commandes ; tests aller-retour |
| Casser `.osp` / DST | aucune raison de toucher I/O | ne pas modifier `project_io`/`formats` |
| `main_window.cpp` géant → merges/erreurs | extraction en panneaux | découper par incréments compilés/testés |
| Modales inline dispersées | migration vers panneau | conserver un fallback dialogue tant que le panneau n'existe pas |

---

## 13. Proposition d'architecture cible (résumé — détail en spécification)

- **Design system** : `app_theme.*` (palette claire/sombre, `QSettings`),
  `design_tokens.*` (couleurs, espacements, tailles), QSS ciblée générée depuis
  les tokens.
- **Fenêtre** : menus (7 + Aide) · **barre d'outils principale** · **barre
  contextuelle** · **palette d'outils verticale** (modes) · canevas central ·
  **inspecteur** (droite) · **panneau document** (gauche, onglets Objets/Régions/
  Ordre) · **workflow** (repliable) · **barre de simulation** (zone réservée) ·
  barre d'état enrichie.
- **Modèle de sélection unifié** : `SelectionModel` (type + id) synchronisé
  canevas ↔ panneaux.
- **Contrôleur d'outils** : `ToolController` remplaçant `cropMode_`/`mergeMode_`
  par un enum d'outils avec curseurs, focus clavier, sortie Échap.
- **Commandes** : `SetStitchParamsCommand` (édition de paramètres), généralisation
  pour l'inspecteur.
- **Widgets de paramètres** : `running_params_widget`, `tatami_params_widget`,
  `satin_params_widget` réutilisés dans l'inspecteur et les créateurs.
- **Persistance UI** : `QSettings` (géométrie, docks, thème, densité, derniers
  dossiers).

---

## 14. Modifications prioritaires (ordonnées)

1. **Thème centralisé + tokens** (fondation ; aucun risque métier).
2. **Inspecteur de propriétés** + `SetStitchParamsCommand` → débloque P1/P2/P3
   (édition post-création). Impact ergonomique maximal.
3. **Barre d'outils principale** + **palette d'outils verticale** + `ToolController`
   → P4/P5 (modes visibles, découverte).
4. **Panneau document** (Objets/Régions/Ordre) + **modèle de sélection unifié** →
   P6, sélection croisée.
5. **Barre contextuelle** (réglages rapides selon sélection).
6. **État d'accueil** + **résumé pré-export** + **suivi modifié** → P9/P10/P11.
7. **Refonte visuelle Analyse/Ordre/Simulation** sans emoji → P7/P8/P13.
8. **Accessibilité transversale** (accessibleName, focus, contraste double,
   clavier) → A1–A7.
9. **Persistance `QSettings`**.

Chaque étape est **livrable indépendamment**, compile et garde les 161 tests au
vert. Le détail (fichiers, classes, risques, tests) est dans
`docs/ui-redesign-plan.md`.
