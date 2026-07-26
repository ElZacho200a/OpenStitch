# Retouche des points et de la géométrie

Public : utilisateur avancé, développeur.

## Ce qui est éditable aujourd'hui

| Niveau | Retouche disponible | État |
|---|---|---|
| Image | pile d'opérations non destructive | Implémenté |
| Segmentation | fusion, suppression, recoloration | Implémenté |
| Vecteurs | **déplacement** de nœuds | Implémenté |
| Objets de broderie | paramètres (longueur, densité, angle, type) | Implémenté |
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

## Implémentation associée

- `apps/desktop/node_handle.hpp` — poignée de nœud.
- `apps/desktop/main_window.cpp` — sélection d'objet, affichage des poignées.
- `libs/commands/.../project_commands.hpp` — `MoveNodeCommand`.
