# Vectorisation

Public : utilisateur avancé, développeur. État : **Implémenté** (édition de nœuds
limitée au déplacement).

## But

Transformer une région (masque de pixels) en **contours vectoriels propres** :
contour extérieur et trous, en coordonnées physiques (µm), simplifiés et
nettoyés.

## Algorithme

1. Construction du **masque binaire** de la région.
2. Extraction des contours et trous par `cv::findContours` (mode `RETR_CCOMP`,
   approximation `CHAIN_APPROX_SIMPLE`) — algorithme de Suzuki-Abe.
3. Conversion des points en **coordonnées physiques** (µm, origine au centre de
   l'image, Y vers le haut).
4. **Simplification** Douglas-Peucker par contour, avec une tolérance
   **adaptative** bornée à `périmètre / 16` : les petits trous (mât, cordage
   traversant une voile) ne sont pas aplatis au point de disparaître.
5. **Nettoyage** par union Clipper2 (`clean_to_path_sets`) qui reconstruit la
   hiérarchie extérieur/trous (règle pair-impair) et corrige les
   auto-intersections. Une région en plusieurs morceaux produit plusieurs
   `PathSet`.

## Résultat

Un `document::VectorObject` contient un ou plusieurs `geometry::PathSet`
(`outer` + `holes`), garde la couleur de la région et un lien vers la
`RegionId` d'origine.

## Édition de nœuds

Les nœuds de l'objet sélectionné s'affichent (poignées de taille constante) et se
**déplacent** à la souris ; chaque déplacement passe par `MoveNodeCommand`
(annulable).

Limitation : l'**ajout/suppression de nœuds**, la **conversion anguleux/lisse**
et l'édition de tangentes de Bézier ne sont **pas** exposés dans l'interface
(le modèle les prévoit : `PathNode` porte `tan_in`/`tan_out` et un `NodeType`).

## Robustesse

La correction de la tolérance de simplification (adaptative) évite un défaut
observé : des trous trop simplifiés faisaient déborder les remplissages (voir
*Tatami* et *Limitations*).

## Implémentation associée

- `libs/vectorization/src/vectorize.cpp` — `vectorize_region`.
- `libs/geometry/src/simplify.cpp` — Douglas-Peucker, aire signée.
- `libs/geometry/src/clean.cpp` — `clean_to_path_sets` (Clipper2 encapsulé).
- `libs/document/include/openstitch/document/vector_object.hpp` — `VectorObject`,
  `NodeRef`.
- `libs/commands/.../project_commands.hpp` — `AddVectorObjectCommand`,
  `MoveNodeCommand`.
- Tests : `tests/unit/vectorization/test_vectorize.cpp`,
  `tests/unit/geometry/test_simplify.cpp`, `test_clean.cpp`.
