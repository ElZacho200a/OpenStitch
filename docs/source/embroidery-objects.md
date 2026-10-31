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

| Point de croix (HP-STI-012) | `fill_cross_stitch` | `cell`, `grid_angle`, `full_cross`, `passes`, `hidden_underpath` | Nouveau |

Le remplissage directionnel est détaillé dans son chapitre (*Remplissage
directionnel*) ; il se crée par conversion d'un tatami (inspecteur) ou via
*Type de points*.

**Point de croix** (`CrossStitchParams`, dernière alternative du variant). La région
vectorielle source est pavée par une grille de cellules carrées de côté `cell` (1 à 8 mm,
2,5 mm par défaut), ancrée à l'origine du projet et tournée de `grid_angle` ; une cellule est
cousue si son **centre** est dans la région (trous respectés). Chaque rangée est cousue en
tronçons de cellules contiguës, comme se brode une croix en rangs : toutes les jambes « / » à
l'aller, les « \ » au retour (`full_cross`) ; en demi-croix seules les « / » sont cousues, une
rangée sur deux en sens inverse. `passes` (1 à 3) répète chaque tronçon. Tous les points sont des
coins de cellule ; entre deux tronçons (trou, rangée non contiguë) la liaison est un saut, relié
par un trajet caché si `hidden_underpath`. Les points de fixation (entrée/sortie) sont ceux du
traitement de séquence commun (`SequenceFinishing`). Exposé dans le bureau (« Point de croix » du
menu *Type de points*, inspecteur : taille de cellule, angle, passes, croix complète) et au CLI
(`set_stitch_type` avec `cross_stitch`, `set_params` avec `cell_mm`, `grid_angle_deg`,
`full_cross`, `passes`, `hidden_underpath`).

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

## Calques du document

État : **en cours de développement** (roadmap HP-OBJ-024). Les calques classent les **formes** au
niveau du document, au-dessus des couches d'une forme : *calque ⊃ forme ⊃ couches*. Un calque
« Broderie principale » peut recevoir la broderie, un calque « Effets » des formes posées par-dessus
qui couvrent plusieurs zones ; un effet propre à une seule zone reste une couche de cette forme.

- **Modèle.** `Project::calques` est ordonné du fond (cousu en premier) vers le dessus ; chaque
  `VectorObject` porte `calque` (invalide = calque du fond). Une liste vide est un calque implicite :
  les documents existants ne changent pas.
- **Ordre de couture.** Le rang du calque est la clé principale ; l'ordre de
  `embroidery_objects` ne départage que les objets d'un même calque (`Project::sewingOrder`).
  Un calque masqué n'est ni dessiné ni cousu. L'optimisation de l'ordre ne franchit pas les calques.
- **Panneau Calques** (au-dessus de la liste Document) : pile du dessus vers le fond, œil, cadenas
  (un calque verrouillé n'est plus saisissable au canevas), calque actif en gras — il reçoit les
  formes créées. Boutons + − ▲ ▼ ; double-clic pour renommer ; menu de ligne « Déplacer la sélection
  ici ». Chaque geste est une commande annulable (`commands/calque_commands.hpp`).
- **Suppression.** Supprimer un calque replie ses formes dans le calque en dessous.
- **Persistance.** `.osp` v9 (clés `calques` et `calque`), écrit uniquement si le document a des
  calques : un document sans calque garde sa version et reste lisible par les versions antérieures.
- **Limites actuelles.** Pas d'effet lié à ses cibles (une forme d'un calque supérieur est
  indépendante), calques absents de la CLI `project`, pas de glisser-déposer entre calques.

## Implémentation associée

- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `EmbroideryObject`, `StitchParams`, `RunningStitchParams`, `TatamiParams`,
  `SatinParams`.
- `libs/stitch_generation/src/generate.cpp` — `generate_sequence`, dispatch.
- `libs/autodigitize/src/autodigitize.cpp` — choix automatique du type.
- Tests : `tests/unit/stitch/test_generate.cpp`.
