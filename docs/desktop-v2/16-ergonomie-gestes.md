# 16 — Ergonomie : faire suivre les actions à la sélection

État au 2026-11-06. Chantier **ouvert, non implémenté** : ce chapitre pose le problème, ce qui a été
vérifié dans le code, une maquette à essayer, et ce qu'il reste à décider avant d'écrire du Qt.

## Le problème (retour d'usage)

> « J'ai cette triste impression que la V2 est moins complète et ergonomique que la V1. »
> « Tout me semblait plus accessible en V1 ; là, des opérations terriblement utiles sont dans des menus
> déroulants. »

Deux causes distinctes : des fonctions manquent (`13`, `15`), et celles qui existent sont enfouies.
Ce chapitre ne traite que la seconde.

## Ce qui a été vérifié dans le code (V2, `apps/desktop_v2/ui/shell_window.cpp`)

- `commandEntries()` alimente la palette de commandes, `runCommand`, l'aide, et le **menu clic droit**.
- Le menu clic droit (`buildContextMenu`) ne retient que les commandes dont l'identifiant commence par
  une liste fixe de préfixes (`edit.`, `transform.`, `order.`, `selection.`, `action.satin.`,
  `action.fill.`, `preset.apply.`, `preset.save`) : liste plate, sans sous-menus.
- Les commandes ajoutées pendant la finalisation (`satin.rung.*`, `node.*`, `text.*`, `thread.palette`,
  `export.options`, `analysis.*`, `chat.open`…) ne sont donc accessibles **que par la palette** : ni menu,
  ni bouton, ni raccourci, ni clic droit.
- La V1 a une **barre contextuelle** qui change avec la sélection (le type de points y est une rangée de
  boutons visibles) et un menu clic droit de ~22 entrées avec sous-menus (`apps/desktop/main_window_toolbars.cpp`,
  `main_window_context_menu.cpp`). La V2 n'a pas d'équivalent de la barre contextuelle.
- Ce qui n'est PAS vérifié : le ressenti à l'écran. Aucun test ne le mesure, et aucune session de travail
  n'a pu lancer l'application (voir la méthode de captures du journal `07`).

## Maquette

`maquettes/gestes.html` (HTML autonome, à ouvrir dans un navigateur ; polices via Google Fonts). Elle
montre cinq idées sur un motif de broderie, toutes pilotées par **un seul tableau « contexte → commandes »** :

| Idée | Inspiration | Réalisation Qt envisagée |
|---|---|---|
| Barre flottante près de la sélection | Figma, Photoshop | `QWidget` flottant au-dessus de la vue |
| Menu radial (huit secteurs, geste ou touches 1–8) | Blender, Maya | `QWidget` peint |
| Rail de bobines (plan de fils toujours visible, glisser pour réordonner) | propre à la broderie | liste + glisser-déposer |
| Champs numériques « à glisser » | Blender, Figma | filtre d'événements sur le libellé |
| Loupe de points (3× sous le curseur, touche L) | propre à la broderie | vue secondaire |

Le mode Nœuds de la maquette modifie réellement le tracé ; plusieurs autres actions (« Modifier le
texte », outils de dessin) sont des simulations signalées par un message. Les valeurs (nombres de
points) sont des exemples.

## Le principe à implémenter : un tableau contexte → commandes

Aujourd'hui chaque surface (palette, clic droit, menus) décide seule quelles commandes elle montre. Le
changement structurant est de **déclarer, une fois, quelles commandes servent dans quel contexte**
(aucune sélection, un objet de broderie, un texte, une colonne satin, l'outil Nœuds, l'outil Régions…),
puis de laisser chaque surface (barre flottante, menu radial, clic droit, menus) lire ce tableau :

- une commande ajoutée une fois apparaît partout et ne peut plus être « palette seulement » ;
- les états grisés viennent de `CommandEntry::enabled`, déjà calculé en direct ;
- point d'ancrage : remplacer `kPrefixes` dans `buildContextMenu` par ce tableau, puis construire la
  barre flottante sur la même source.

## Contraintes à respecter

- **Alternative clavier à chaque geste** (menu radial → touches 1–8 et menu classique ; glisser de champ →
  saisie et flèches ; rail → Alt+flèches). Un geste seul exclut des utilisateurs.
- Qt Widgets : pas de dépendance web ; les effets doivent rester légers.
- Ne pas casser les invariants (`CLAUDE.md`) : aucune logique métier dans `apps/desktop_v2/ui`, toute
  mutation par `ICommand`.
- Ajouter un test QTest par surface ; garder le garde « toutes les commandes sont listées » de
  `test_shell_window.cpp`.

## À décider (par l'utilisateur, avant d'implémenter)

1. Quelles idées garder, jeter, ou changer (le menu radial peut paraître gadget ; la barre flottante peut
   gêner le motif sur un petit écran).
2. Quelles opérations doivent être « sous la main » en premier (la liste de départ vient de la V1 : type
   de points, orientation, guides de direction, convertir, décaler).
3. L'ordre : barre flottante + tableau d'abord (proposition), puis menu radial, rail, champs, loupe.

## Ce qui est fait

- **Tableau contexte → commandes, première étape** : `core/context_commands.{hpp,cpp}` ; le clic droit de `buildContextMenu` le lit (`kPrefixes` supprimé). Les commandes `node.*`, `text.*`, `satin.rung.*`… sont donc au clic droit. Tests : `test_context_commands.cpp`, `contextMenuListsOnlyActiveSelectionCommands`.

- **Barre flottante** : `ui/floating_action_bar.*` ; contextes `Object`, `Text`, `Satin`, `Nodes` déclarés dans `core/context_commands` (`floatingBarCommands`), déduits de la sélection par `ShellWindow::refreshFloatingBar`. Fixée en haut et au centre du canevas (choix : ne jamais masquer l'objet sélectionné ; la suivre au plus près reste à juger). Ne montre que les commandes actives. Test : `floatingBarFollowsTheSelectionAndRunsCommands`. Capture réelle : `captures/barre-flottante.png`.
- Limite : la barre ne se rafraîchit pas quand seuls des nœuds sont choisis sans changer la sélection d'objets.

## Ce qui n'est pas fait

Le menu radial, le rail de bobines, les champs à glisser et la loupe ne sont pas implémentés (en attente des décisions ci-dessus). La maquette n'est pas du Qt.
