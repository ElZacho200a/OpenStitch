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
| Points générés | modèle + commandes (cœur), aucune UI | Partiel (Lot 8.1) |

## Régénération vs édition manuelle

Les points sont **régénérés** à chaque modification du document (fonction pure),
puis patchés par les retouches manuelles éventuelles de l'objet
(`stitch_generation::effective_sequence`, ADR-014). Le **cœur** (Lot 8.0/8.1)
implémente le modèle complet : déplacer un point, convertir Stitch↔Jump,
ajouter/retirer une coupe de fil, abandonner les retouches d'un objet — quatre
commandes annulables (`MoveStitchPointCommand`, `SetStitchPointTypeCommand`,
`SetStitchTrimCommand`, `DiscardOverridesCommand`) et une persistance `.osp`
(schéma v3). **Aucune UI ne les expose encore** : pas de mode d'édition, pas de
poignée sur un point généré, pas d'indicateur `Clean`/`Dirty`/`ManuallyEdited`
dans le canevas — prévu au sous-lot 8.2 (voir
`docs/lot8-manual-editing-design.md`). Seuls les tests et un usage
programmatique (CLI, tests) exercent ces commandes aujourd'hui.

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

## Implémentation associée

- `apps/desktop/node_handle.hpp` — poignée de nœud (réutilisée pour la rotation).
- `apps/desktop/main_window.cpp` — sélection, poignées, menu contextuel, poignée
  de rotation.
- `apps/desktop/canvas_view.cpp` — signal `canvasContextMenu` (clic droit).
- `libs/commands/.../project_commands.hpp` — `MoveNodeCommand`, `SetFillAngleCommand`,
  `SetStitchTypeCommand`, `ConvertFillsToTatamiCommand`, et (Lot 8.1, cœur
  sans UI) `MoveStitchPointCommand`, `SetStitchPointTypeCommand`,
  `SetStitchTrimCommand`, `DiscardOverridesCommand`.
- `libs/stitch_generation/include/.../overrides.hpp` — `effective_sequence`,
  `apply_manual_overrides`, `raw_slice`, `fingerprint`, `classify_edit_state`.
- `libs/document/include/.../embroidery_object.hpp` — `StitchOverride`,
  `StitchPointType`, champs `overrides`/`edited_fingerprint`/`edited_point_count`.
