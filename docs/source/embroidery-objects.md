# Objets de broderie

Public : utilisateur avancé, développeur. État : **Implémenté** (trois types).

## Définition

Un `document::EmbroideryObject` porte l'**intention de couture** : un type de
point et ses paramètres, une couleur de fil, un lien vers l'objet vectoriel
source, un état de visibilité et de verrouillage. Ses **points ne sont pas
stockés** dans l'objet : ils sont régénérés à la demande (séparation
intention/points).

## Le variant `StitchParams`

Le type de point est un `std::variant` à trois alternatives :

| Objet | Params | Géométrie suivie | Sous-couche |
|---|---|---|---|
| Point de contour | `RunningStitchParams` | contours de l'objet vectoriel | non |
| Remplissage tatami | `TatamiParams` | régions pleines (avec trous) | prévue, non générée |
| Colonne satin | `SatinParams` | deux **rails** portés par l'objet | centrale (point droit) |

Le satin est particulier : il **porte sa propre géométrie** (deux rails
éditables), car une colonne ne se déduit pas d'un simple contour.

## Tableau récapitulatif

| Objet | Générateur | Paramètres clés | Statut recommandé |
|---|---|---|---|
| Running (simple/double/triple) | `run_stitch` + `apply_repeats` | `stitch_length`, `min_length`, `repeats` | Implémenté |
| Tatami | `fill_tatami` | `row_spacing`, `stitch_length`, `angle`, `inset`, `stagger` | Partiel |
| Satin | `fill_satin` | `density`, `pull_compensation`, `center_underlay` | Expérimental |

Le tableau reflète la **qualité métier**, pas seulement la présence de code : le
running stitch est solide ; le tatami est fonctionnel mais sans sous-couche ni
underpath caché ; le satin est une génération simple à deux rails (voir les
chapitres dédiés). Aucun n'est validé sur machine réelle.

## Création

Depuis l'interface (menu Broderie) sur un objet vectoriel sélectionné, ou en lot
par la **classification automatique expérimentale des régions** (voir
*Limitations*) qui choisit le type selon la forme :

- bande fine compatible → une ou plusieurs sections de **satin topologique** ;
- autre zone **remplissable** → **tatami** ;
- petite forme → **contour** cousu (point triple) ;
- satin **automatique** naïf : désactivé par défaut car il débordait sur les
  formes concaves/branchues (`AutoOptions::use_naive_satin` pour le réactiver).

Le **type se change ensuite** à tout moment (clic droit sur la forme ▸ *Type de
points* ▸ contour / tatami / satin) via `SetStitchTypeCommand` (annulable) ;
l'action **Convertir les satins auto en tatami** répare en lot un projet importé.

Limitation : cette classification est une heuristique simple (largeur moyenne
= 2·aire/périmètre total) autour d'un moteur géométrique ; ce n'est pas encore
un moteur d'auto-numérisation robuste
comparable à un logiciel commercial. Elle produit des objets **éditables** qu'il
faut vérifier et retoucher.

## Régénération

`generate_sequence(project)` parcourt les objets **visibles** dans l'ordre,
insère un changement de couleur entre deux objets de couleurs différentes, et
appelle le générateur adapté à chaque type via `std::visit`. Le résultat est une
`StitchSequence`. Chaque commande porte l'`ObjectId` de sa source : l'interface
s'en sert pour afficher la broderie **dans la couleur de chaque fil** et pour
**filtrer** l'affichage (par type, couleur ou taille de zone).

## Implémentation associée

- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `EmbroideryObject`, `StitchParams`, `RunningStitchParams`, `TatamiParams`,
  `SatinParams`.
- `libs/stitch_generation/src/generate.cpp` — `generate_sequence`, dispatch.
- `libs/autodigitize/src/autodigitize.cpp` — choix automatique du type.
- Tests : `tests/unit/stitch/test_generate.cpp`.
