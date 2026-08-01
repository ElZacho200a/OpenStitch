# Retouche des points et de la géométrie

Public : utilisateur avancé, développeur.

## Ce qui est éditable aujourd'hui

| Niveau | Retouche disponible | État |
|---|---|---|
| Image | pile d'opérations non destructive | Implémenté |
| Segmentation | fusion, suppression, recoloration | Implémenté |
| Vecteurs | **déplacement** de nœuds | Implémenté |
| Objets de broderie | paramètres (longueur, densité, angle, type) | Implémenté |
| Objets de broderie | **changer le type** (contour/tatami/satin), clic droit | Implémenté |
| Objets de broderie | **orientation** des fils (poignée dans la scène) | Implémenté |
| Ordre de couture | monter/descendre, verrouiller | Implémenté |
| Points générés | **déplacement** d'un point (mode d'édition dédié), indicateurs `Clean`/`ManuallyEdited`/`Dirty`, abandon des retouches | Partiel (Lot 8.2 — déplacement seul ; Stitch↔Jump et coupe de fil restent cœur seul, sans UI, cf. Lot 8.3) |

## Régénération vs édition manuelle

Les points sont **régénérés** à chaque modification du document (fonction pure),
puis patchés par les retouches manuelles éventuelles de l'objet
(`stitch_generation::effective_sequence`, ADR-014). Le **cœur** (Lot 8.0/8.1)
implémente le modèle complet : déplacer un point, convertir Stitch↔Jump,
ajouter/retirer une coupe de fil, abandonner les retouches d'un objet — quatre
commandes annulables (`MoveStitchPointCommand`, `SetStitchPointTypeCommand`,
`SetStitchTrimCommand`, `DiscardOverridesCommand`) et une persistance `.osp`
(schéma v3). Le **Lot 8.2** expose le **déplacement** dans le canevas (mode
d'édition dédié, poignées, indicateurs `Clean`/`Dirty`/`ManuallyEdited`,
abandon des retouches) ; `SetStitchPointTypeCommand`/`SetStitchTrimCommand`
restent, pour l'instant, uniquement accessibles par usage programmatique
(CLI, tests) — prévu au sous-lot 8.3 (voir `docs/lot8-manual-editing-design.md`).

Cas particulier : une séquence **importée d'un DST** est traitée comme la vérité
(le DST n'a pas d'objets), elle n'est donc pas régénérée tant qu'aucune image
n'est chargée.

## Édition de nœuds vectoriels

Sélectionnez un objet vectoriel : ses nœuds s'affichent en poignées. Un
déplacement passe par `MoveNodeCommand` (annulable) et **régénère** les points de
tout objet de broderie qui suit cet objet.

Limitation : l'ajout/suppression de nœuds, la conversion anguleux/lisse et
l'édition de tangentes ne sont pas exposés (voir *Vectorisation*).

## Changer le type de points (clic droit)

Un **clic droit** sur une forme ouvre un menu contextuel *Type de points* :
Contour cousu / Remplissage tatami / Colonne satin (le type courant est coché).
Le changement passe par `SetStitchTypeCommand` (annulable) ; pour le satin, les
rails sont reconstruits depuis le contour source (`rails_from_contour`). Le menu
donne aussi accès à l'orientation (si tatami) et aux calques.

## Orientation des fils dans la scène

Quand un remplissage tatami est sélectionné, une **poignée de rotation** (axe
bleu) s'affiche à son centre. La faire glisser réoriente les fils ; l'angle est
validé au relâchement (`SetFillAngleCommand`, annulable). Détail d'ergonomie
important : la commande est **différée** d'un tour de boucle d'événements, car
`refreshImage()` reconstruit la scène et détruirait la poignée pendant son propre
événement souris (le même soin s'applique aux poignées de nœuds).

## Mode d'édition des points (Lot 8.2)

Un objet de broderie sélectionné et non `Dirty` peut entrer en mode
« Éditer les points » (menu Broderie, raccourci **E**, ou barre contextuelle) :
mode **exclusif**, lié à l'unique objet actif à l'activation (changer de
sélection, charger un autre projet, ou faire passer l'objet à `Dirty` en
fait sortir proprement, sans confirmation). Chaque point de couture réel (passe
`TopStitch`, type `Stitch` — ni saut, ni sous-couche) affiche une poignée
dédiée (couleur distincte des poignées de nœuds vectoriels) ; la glisser
construit une `MoveStitchPointCommand` unique au relâchement (différée d'un
tour de boucle d'événements, comme la poignée de rotation ci-dessus — même
raison). Aucune commande n'est créée si le point n'a pas bougé.

Au-delà d'un seuil de points déplaçables (2000, coût mémoire/événements d'un
`QGraphicsItem` par poignée), l'activation est refusée avec un message
explicite en barre de statut plutôt que de laisser le mode actif sans aucune
poignée visible ; réduire la densité ou la longueur de point de l'objet
permet de repasser sous le seuil.

Un objet retouché (`ManuallyEdited`) affiche un indicateur (✎) dans le
panneau Document, la barre contextuelle et l'inspecteur, avec un bouton
« Abandonner les retouches » (confirmation, `DiscardOverridesCommand`
annulable). Un objet `Dirty` (géométrie source changée depuis les dernières
retouches, cf. `docs/lot8-manual-editing-design.md` §1) affiche un
avertissement (⚠) à la place : ses retouches ne sont plus appliquées et le
mode d'édition lui reste fermé tant qu'elles n'ont pas été abandonnées.

## Implémentation associée

- `apps/desktop/node_handle.hpp` — poignée de nœud (réutilisée pour la
  rotation et pour les points de couture, Lot 8.2).
- `apps/desktop/main_window.cpp` — sélection, poignées, menu contextuel,
  poignée de rotation, mode d'édition des points (`onStitchEditModeToggled`,
  `discardOverrides`, gating dans `updateActions`).
- `apps/desktop/canvas_view.cpp` — signal `canvasContextMenu` (clic droit).
- `apps/desktop/document_panel.cpp`, `apps/desktop/properties_panel.cpp` —
  indicateurs `Clean`/`ManuallyEdited`/`Dirty` (Lot 8.2).
- `libs/commands/.../project_commands.hpp` — `MoveNodeCommand`, `SetFillAngleCommand`,
  `SetStitchTypeCommand`, `ConvertFillsToTatamiCommand`, et (Lot 8.1, exposées
  dans l'UI pour le déplacement seul depuis le Lot 8.2)
  `MoveStitchPointCommand`, `SetStitchPointTypeCommand`,
  `SetStitchTrimCommand`, `DiscardOverridesCommand`.
- `libs/stitch_generation/include/.../overrides.hpp` — `effective_sequence`,
  `apply_manual_overrides`, `raw_slice`, `fingerprint`, `classify_edit_state`,
  et (Lot 8.2) `is_movable_point`, `edit_view`, `classify_all_edit_states`,
  `refresh_context` (un seul `generate_sequence` interne pour la séquence
  effective, les états par objet et la vue d'édition d'une cible, réutilisé
  par `MainWindow::refreshImage` à chaque rafraîchissement).
- `libs/document/include/.../embroidery_object.hpp` — `StitchOverride`,
  `StitchPointType`, champs `overrides`/`edited_fingerprint`/`edited_point_count`.
