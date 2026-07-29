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
| Points générés | — | Non implémenté |

## Régénération vs édition manuelle

Les points sont **régénérés** à chaque modification du document (fonction pure).
Il n'existe **pas** d'édition manuelle des points générés (déplacer un point,
convertir un point en saut, ajouter une coupe). Le modèle prévoit cette
distinction (états `Clean`/`Dirty`/`ManuallyEdited` décrits dans l'étude de
cadrage) mais elle n'est pas exposée.

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
  `SetStitchTypeCommand`, `ConvertFillsToTatamiCommand`.
