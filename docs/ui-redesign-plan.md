# Refonte de l'interface — Phase 3 : plan d'implémentation

Public : mainteneur. Décline la spécification (`docs/ui-redesign-specification.md`)
en **incréments livrables**, chacun compilé et validé (161 tests CTest au vert)
avant le suivant. Aucune modification métier : tout passe par les bibliothèques du
cœur et le système de **commandes**. On **n'ouvre pas** de seconde vérité métier
dans les widgets.

Règle de travail (imposée par le prompt) : après chaque incrément → compiler,
lancer les tests, corriger, vérifier les régressions, documenter les décisions.
Pas de remplacement massif non vérifié.

Convention : les nouveaux fichiers vont dans `apps/desktop/` (ou un sous-dossier
`apps/desktop/ui/` si le nombre le justifie), ajoutés à
`apps/desktop/CMakeLists.txt` (AUTOMOC déjà actif).

---

## Étape 0 — Filet de sécurité (préalable)

- **Objectif** : pouvoir refactorer `main_window.cpp` (1994 lignes) sans peur.
- **Fichiers** : `tests/unit/desktop/` (nouveau, **si** headless possible via
  `QApplication` en `offscreen`), sinon tests de logique pure extraite.
- **Contenu** : extraire la logique testable **hors widgets** (voir Étape 2/4 :
  `SelectionModel`, activation d'actions, prédicats de filtre) pour la tester sans
  IHM.
- **Risque** : les tests Qt GUI en CI headless (`QT_QPA_PLATFORM=offscreen`).
- **Livrable indépendant** : oui (n'change rien visuellement).

---

## Étape 1 — Design system : thème + tokens (fondation)

- **Objectif** : centraliser couleurs/espacements/typo ; thème clair (+ sombre) ;
  aucune régression visuelle métier.
- **Fichiers créés** : `design_tokens.hpp` (structs/const `Tokens`),
  `app_theme.hpp/.cpp` (`AppTheme` : applique `QPalette` + QSS **ciblée** générée
  depuis les tokens ; charge/sauve le choix via `QSettings`).
- **Fichiers modifiés** : `main.cpp` (instancier le thème avant `MainWindow`),
  `canvas_view.cpp`/`ruler.cpp`/`main_window.cpp` (remplacer les couleurs codées
  en dur — fond, grille, cadre, sélection, points, sauts, nœuds — par les tokens ;
  cf. audit §6).
- **Classes** : `AppTheme`, `DesignTokens`.
- **Risques** : QSS trop agressive dégradant l'accessibilité/natif → rester
  **ciblé** (widgets nommés), pas de QSS monolithique. Thème sombre incomplet →
  livrer clair d'abord, sombre derrière un `if`.
- **Tests** : chargement du thème (clair/sombre) sans crash ; tokens exposés ;
  (offscreen) palette appliquée.
- **Livrable indépendant** : oui.

---

## Étape 2 — Inspecteur de propriétés + édition post-création (impact max)

- **Objectif** : débloquer P1/P2/P3 — éditer les paramètres d'un objet existant.
- **Fichiers créés** : `properties_panel.hpp/.cpp` (`PropertiesPanel` : dock
  droite, sections repliables) ; widgets de paramètres réutilisables
  `running_params_widget`, `tatami_params_widget`, `satin_params_widget`
  (mutualisés avec les créateurs de l'Étape 8).
- **Cœur/commandes** : `libs/commands/.../project_commands.hpp` →
  **`SetStitchParamsCommand`** (généralise `SetFillAngleCommand` : remplace les
  `StitchParams` d'un objet, mémorise l'ancien, annulable). C'est la **seule**
  addition « métier » et elle reste dans `commands` (pas dans un widget).
- **Fichiers modifiés** : `main_window.cpp` (instancier le panneau, le peupler
  selon la sélection, brancher les changements → commande → `refreshImage`).
- **Risques** : boucles de signaux (mettre à jour le panneau **regénère** la
  scène) → aperçu temporaire + commande au relâchement/blur, `blockSignals`
  pendant le repeuplement. Ne pas régénérer les points pour un simple affichage.
- **Tests** : aller-retour `SetStitchParamsCommand` (undo restaure exact) ;
  (offscreen) le panneau reflète la sélection ; changer un paramètre passe bien
  par une commande (undo/redo).
- **Livrable indépendant** : oui (panneau optionnel, le reste marche sans).

---

## Étape 3 — Contrôleur d'outils + palette verticale + barre principale

- **Objectif** : P4/P5 — modes visibles, découverte, clavier.
- **Fichiers créés** : `tool_controller.hpp/.cpp` (`ToolController` : enum
  `Tool{Select,Pan,Zoom,Rect,Region,Nodes,Orient,Measure}`, curseurs, `Échap`),
  `tool_palette.hpp/.cpp` (barre verticale), `main_toolbar.hpp/.cpp`.
- **Fichiers modifiés** : `canvas_view.*` (consommer l'outil actif au lieu de
  `cropMode_` ; garder `setCropMode` en interne mappé sur `Tool::Rect`),
  `main_window.cpp` (remplacer `mergeMode_`/`cropMode_` par le contrôleur ;
  brancher raccourcis `V/H/Z/M/R/N/O`).
- **Classes** : `ToolController`, `ToolPalette`, `MainToolbar`.
- **Risques** : ré-entrance avec la sélection ; conflits de raccourcis Qt → table
  centralisée, vérifier `QKeySequence` standard. Mesure : **n'ajouter que si**
  faisable en vue seule (sinon la retirer de la palette — ne pas simuler).
- **Tests** : transitions d'outil, `Échap` revient à Sélection et annule fusion ;
  activation des raccourcis ; le mode fusion affiche un indicateur.
- **Livrable indépendant** : oui.

---

## Étape 4 — Panneau Document + modèle de sélection unifié

- **Objectif** : P6 + sélection croisée bidirectionnelle.
- **Fichiers créés** : `selection_model.hpp/.cpp` (`SelectionModel` :
  `{Kind{Region,Vector,Embroidery,None}, id}` + signaux) ; `document_panel.hpp/.cpp`
  (onglets Objets/Régions/Ordre) ; modèles Qt `objects_model`, `regions_model`,
  `stitch_order_model` + delegates.
- **Fichiers modifiés** : `main_window.cpp` (router `selectedRegion_/
  selectedObject_/selectedEmbroidery_` **à travers** `SelectionModel` sans casser
  les slots existants ; migrer `orderDock_` dans l'onglet Ordre) ;
  `canvas_view`/rendu (surligner selon la sélection unifiée).
- **Classes** : `SelectionModel`, `DocumentPanel`, modèles/delegates.
- **Risques** : régression de la sélection éclatée → migration progressive
  (le `SelectionModel` **encapsule** les trois champs existants d'abord, on ne les
  supprime qu'ensuite). Perf des listes → modèles/delegates, pas de widget par
  ligne.
- **Tests** : sélection panneau→canevas et canevas→panneau synchronisées ;
  réordonnancement (drag) = même résultat que `ReorderEmbroideryCommand` ;
  visibilité/verrou passent par commandes ; le filtrage d'affichage **ne modifie
  pas** le projet.
- **Livrable indépendant** : oui (le dock Ordre actuel reste fonctionnel tant que
  la migration n'est pas finie).

---

## Étape 5 — Barre d'outils contextuelle

- **Objectif** : réglages rapides sans dialogue.
- **Fichiers créés** : `context_toolbar.hpp/.cpp` (`ContextToolbar` : contenu
  selon `SelectionModel`, réutilise les widgets de paramètres de l'Étape 2).
- **Fichiers modifiés** : `main_window.cpp` (insérer sous la barre principale ;
  brancher sur la sélection).
- **Risques** : duplication avec l'Inspecteur → partager les widgets de
  paramètres ; un seul chemin de commande.
- **Tests** : le contenu suit la sélection ; l'édition en ligne = commande.
- **Livrable indépendant** : oui.

---

## Étape 6 — Workflow (indicateur d'étapes)

- **Objectif** : repère pédagogique non contraignant.
- **Fichiers créés** : `workflow_panel.hpp/.cpp` (`WorkflowPanel` : états déduits
  du document).
- **Fichiers modifiés** : `main_window.cpp` (recalcul de l'état sur
  `refreshImage`/sélection ; clic = mettre en avant outils/panneaux, **jamais**
  d'opération destructive).
- **Risques** : recalcul trop fréquent → recalcul léger, pas à chaque mouvement
  souris.
- **Tests** : l'état de chaque étape correspond au contenu du document.
- **Livrable indépendant** : oui (repliable/masquable).

---

## Étape 7 — Canevas & interactions (finitions)

- **Objectif** : sélection double-contour, survol, poignées à cible élargie,
  angle affiché pendant le glisser, zone réservée simulation.
- **Fichiers modifiés** : `canvas_view.*`, `node_handle.hpp` (zone d'interaction
  élargie, états normal/survol/sélection/focus), `main_window.cpp`
  (`renderBase`/gizmo : halo + libellé d'angle ; conserver le **rendu deux
  couches** et le gating `drawDots`).
- **Risques** : perf sur gros motifs → ne pas ajouter de coût par point ; la
  partie « à venir atténuée » en simulation seulement si non pénalisante.
- **Tests** : (offscreen) rendu sans crash ; pas de régression perf (motif > 4000
  points garde `drawDots=false`).
- **Livrable indépendant** : oui.

---

## Étape 8 — Dialogues (uniformisation + nouveaux)

- **Objectif** : import avec aperçu, résumé pré-export, statistiques hors modale.
- **Fichiers créés/modifiés** : refonte `import_dialog.*` (vignette, mm/pixel,
  alerte cadre, validation en ligne) ; `export_summary_dialog.hpp/.cpp`
  (nouveau) ; migrer `showStatistics` vers l'Inspecteur/un panneau ; uniformiser
  les créateurs inline (`createTatamiObject`… → widgets de paramètres partagés).
- **Cœur** : réutiliser `stitch::compute_stats`, `stitch_analysis::analyze`,
  `document::placement_*`, `formats::` (aucune logique nouvelle).
- **Risques** : ne pas bloquer l'export sur avertissement non critique (seule la
  séquence vide est refusée par le moteur) ; garde « enregistrer .osp ».
- **Tests** : validation d'entrées (import : refus incohérent) ; le résumé
  pré-export reflète `compute_stats` ; suivi « modifié » (`isWindowModified`).
- **Livrable indépendant** : oui, dialogue par dialogue.

---

## Étape 9 — État d'accueil + suivi « modifié » + garde fermeture

- **Fichiers créés** : `empty_state_widget.hpp/.cpp`.
- **Fichiers modifiés** : `main_window.cpp` (afficher/masquer selon présence d'un
  document ; `setWindowModified` sur chaque commande ; `closeEvent` demandant
  d'enregistrer si modifié ; récents si `QSettings`).
- **Risques** : récents affichés sans persistance → **n'afficher que si**
  `QSettings` implémenté.
- **Tests** : l'état d'accueil disparaît à l'ouverture ; le titre `*` suit l'état
  modifié.
- **Livrable indépendant** : oui.

---

## Étape 10 — Accessibilité transversale

- **Objectif** : A1–A7.
- **Fichiers modifiés** : tous les widgets (`setAccessibleName/Description`,
  relations label↔champ) ; `app_theme` (focus visible stylé, contrastes WCAG,
  double contour canevas) ; table de raccourcis centralisée + menu Aide ▸
  Raccourcis.
- **Risques** : régressions de focus natif → vérifier l'ordre de tab.
- **Tests** : (offscreen) présence d'`accessibleName` sur les contrôles critiques ;
  navigation clavier des actions principales ; aucune info portée par la couleur
  seule (revue).
- **Livrable indépendant** : oui (transversal, par lots).

---

## Étape 11 — Persistance UI (`QSettings`) & finition

- **Fichiers créés** : `ui_settings.hpp/.cpp` (wrapper `QSettings`).
- **Contenu** : géométrie fenêtre, état des docks (`saveState/restoreState`),
  thème, densité, derniers dossiers, récents. **Jamais** de donnée métier (elle
  reste dans `.osp`).
- **Risques** : état de docks corrompu → réinitialisation sûre par défaut.
- **Tests** : sérialisation/lecture des préférences ; défauts si vide.
- **Livrable indépendant** : oui.

---

## Récapitulatif : nouveaux fichiers (indicatif, créés seulement si utiles)

`design_tokens.*` · `app_theme.*` · `main_toolbar.*` · `context_toolbar.*` ·
`tool_palette.*` · `tool_controller.*` · `properties_panel.*` +
`*_params_widget.*` · `selection_model.*` · `document_panel.*` +
modèles/delegates · `workflow_panel.*` · `empty_state_widget.*` ·
`export_summary_dialog.*` · `ui_settings.*`.

Nouvelle commande cœur : `SetStitchParamsCommand` (dans `libs/commands`).

## Ordre de livraison & dépendances

```
1 (thème) ──► 2 (inspecteur) ──► 5 (barre contextuelle)
        └──► 3 (outils) ──► 7 (canevas)
        └──► 4 (document+sélection) ──► 6 (workflow)
8 (dialogues) · 9 (accueil/modifié) · 10 (accessibilité) · 11 (settings)
   sont largement indépendants et s'insèrent quand utile.
```

Chaque étape : **compile, 161 tests verts, pas de régression métier**, aucune
fonctionnalité retirée, `.osp`/DST/undo intacts. On ne présente jamais une
fonction absente comme disponible ; les fonctions expérimentales (satin) portent
une mention discrète.

## Tests à prévoir (transversal, cf. prompt)

Activation/désactivation contextuelle des actions · synchronisation sélection
canevas↔panneaux · application des paramètres (via commande) · undo/redo
préservé · ordre de couture · affichage des erreurs d'analyse · modes temporaires
(fusion) · raccourcis principaux · chargement du thème · accessibilité des
contrôles critiques (offscreen) · **le filtrage d'affichage ne modifie pas le
projet**. Les tests métier existants doivent continuer à passer.
