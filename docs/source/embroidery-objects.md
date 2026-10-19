# Objets de broderie

Public : utilisateur avancé, développeur. État : **Implémenté** (quatre types).

## Définition

Un `document::EmbroideryObject` porte l'**intention de couture** : un type de
point et ses paramètres, une couleur de fil, un lien vers l'objet vectoriel
source, un état de visibilité et de verrouillage. Ses **points ne sont pas
stockés** dans l'objet : ils sont régénérés à la demande (séparation
intention/points).

## Le variant `StitchParams`

Le type de point est un `std::variant` à quatre alternatives :

| Objet | Params | Géométrie suivie | Sous-couche |
|---|---|---|---|
| Point de contour | `RunningStitchParams` | contours de l'objet vectoriel | non |
| Remplissage tatami | `TatamiParams` | régions pleines (avec trous) | contour et/ou rangées perpendiculaires (désactivées par défaut ; activées selon l'aire par l'auto-numérisation) |
| Colonne satin | `SatinParams` | deux **rails** portés par l'objet | centrale (point droit) |
| Remplissage directionnel | `DirectionalFillParams` | régions pleines ; points qui suivent des **guides** | contour + perpendiculaire (du tatami) |

Le satin est particulier : il **porte sa propre géométrie** (deux rails
éditables), car une colonne ne se déduit pas d'un simple contour.

## Tableau récapitulatif

| Objet | Générateur | Paramètres clés | Statut recommandé |
|---|---|---|---|
| Running (simple/double/triple) | `run_stitch` + `apply_repeats` | `stitch_length`, `min_length`, `repeats` | Implémenté |
| Tatami | `fill_tatami` | `row_spacing`, `stitch_length`, `angle`, `inset`, `stagger` | Partiel |
| Satin | `fill_satin` | `density`, `pull_compensation`, `center_underlay` | Expérimental |
| Directionnel | `fill_directional` | `guides`, `break_lines`, `row_spacing`, `stitch_length`, `handmade` | Expérimental |

Le remplissage directionnel est détaillé dans son chapitre (*Remplissage
directionnel*) ; il se crée par conversion d'un tatami (inspecteur) ou via
*Type de points*.

Le tableau reflète la **qualité métier**, pas seulement la présence de code : le
running stitch est solide ; le tatami est fonctionnel, sa sous-couche et son
underpath caché existent mais sont désactivés par défaut (l'auto-numérisation
active la sous-couche) ; le satin est une génération simple à deux rails (voir les
chapitres dédiés). Aucun n'est validé sur machine réelle.

## Création

Depuis l'interface (menu Broderie) sur un objet vectoriel sélectionné, ou en lot
par la **classification automatique expérimentale des régions** (voir
*Limitations*) qui choisit le type selon la forme :

- bande fine compatible → une ou plusieurs sections de **satin topologique** ;
- autre zone **remplissable** → **tatami**, orienté selon l'axe principal de
  la région et avec une sous-couche selon son aire (voir *Numérisation
  automatique*) ;
- petite forme → **contour** cousu (point triple) ;
- satin **automatique** naïf : désactivé par défaut car il débordait sur les
  formes concaves/branchues (`AutoOptions::use_naive_satin` pour le réactiver).

Le **type se change ensuite** à tout moment (clic droit sur la forme ▸ *Type de
points* ▸ contour / tatami / satin / directionnel) via `SetStitchTypeCommand` (annulable) ;
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

## Couches (empilement d'objets sur une même zone)

État : **en cours de développement** (roadmap HP-OBJ-023). Une couche permet
de coudre plusieurs objets de types différents sur une même zone vectorielle,
à la manière des calques : par exemple un tatami puis un contour par-dessus.

- **Principe.** Il n'y a pas de nouveau champ dans le modèle : une couche est
  un `EmbroideryObject` ordinaire dont `source_vector` désigne le même objet
  vectoriel que l'objet de base. Elle est insérée juste après la base dans
  `project.embroidery_objects`.
- **Ordre de couture.** Comme `generate_sequence` parcourt les objets dans
  l'ordre du document, la couche est cousue **après** la base, donc par-dessus.
  Les points restent dérivés à la demande (`effective_sequence`), jamais
  stockés.
- **Création.** Le bouton **+ Couche** sous la liste Document (ou « Ajouter une couche » dans
  le menu de la ligne) empile en un geste une couche de la même famille de points que la
  ligne choisie, avec un fil complémentaire, sans dialogue. Maj + clic sur le bouton, ou
  « Ajouter une couche… » (menus de ligne, de canevas et Broderie), demande le type et la
  couleur. Sur une ligne de groupe, la couche est ajoutée au-dessus de la couche du dessus.
  Chaque ajout pousse `commands::AddLayerCommand(base_id, layer)`.
- **Liste Document.** Les couches d'une forme sont regroupées sous une ligne « Forme (N
  couches) » (« N sections » pour un plan satin) qui porte la pastille du fil de la couche du
  dessus et des cases Visible / Figé agrégées (partiellement cochées si les couches
  divergent) ; cocher la ligne agit sur toutes les couches, en un pas d'annulation. Le menu de
  ligne propose Ajouter une couche, Dupliquer la couche et « Supprimer cette couche ».
- **Nommage.** `<forme> – couche N`, N étant le premier numéro libre parmi les couches de la
  forme ; ajouter une couche depuis une couche n'empile pas les suffixes.
- **Duplication.** Dupliquer une forme (menu contextuel, Alt + glisser) copie aussi toutes ses
  couches, en un pas d'annulation.
- **Suppression.** « Supprimer la forme et ses N couches » (canevas) supprime tout ;
  « Supprimer cette couche » ne retire que la couche visée.
- **Annulation.** `AddLayerCommand` est un `ICommand` : un seul pas d'undo
  retire la couche et rétablit l'ordre précédent, redo compris.
- **Sélecteur de couches (Propriétés).** Dès qu'une forme a deux couches, un sélecteur apparaît
  dans l'inspecteur (fond puis dessus) avec une barre : ▲ Monter / ▼ Descendre (échange le rang
  de couture avec la voisine de la même forme, `ReorderEmbroideryCommand`), Masquer/Afficher
  (`SetEmbroideryVisibleCommand`) et Renommer… (`RenameEmbroideryCommand`), chacun en un pas
  d'annulation.
- **Clic au canevas.** Choisir une forme visée au canevas sélectionne sa couche du **dessus**
  (la dernière cousue) ; le menu contextuel du canevas agit donc sur elle et « Ajouter une
  couche… » empile par-dessus. Les autres couches se choisissent dans la liste Document ou le
  sélecteur.
- **Glisser-déposer.** Déplacer une couche dans la liste Document réordonne la pile **dans sa
  forme** : un dépôt hors de la forme est ramené au bord le plus proche de la pile. Maj enfoncé
  pendant le dépôt libère la contrainte (coudre une couche à un autre rang du dessin). Une forme
  à couche unique se déplace librement.
- **Limites.**
  - Pas de lien parent-enfant dans le modèle : supprimer la base **ne supprime pas** ses
    couches, qui restent sur la zone et deviennent des objets indépendants.

## Implémentation associée

- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `EmbroideryObject`, `StitchParams`, `RunningStitchParams`, `TatamiParams`,
  `SatinParams`.
- `libs/stitch_generation/src/generate.cpp` — `generate_sequence`, dispatch.
- `libs/autodigitize/src/autodigitize.cpp` — choix automatique du type.
- Tests : `tests/unit/stitch/test_generate.cpp`.
