# Palettes et fils

Public : utilisateur avancé, mainteneur. État : **Prévu / partiel**.

## Ce qui existe

- Chaque objet de broderie porte une **couleur RGB** (`std::array<uint8_t,3>`),
  reprise de la région d'origine.
- Un **changement de couleur** est inséré automatiquement entre deux objets
  consécutifs de couleurs différentes lors de la génération.
- La recoloration d'une région est disponible (choix d'une couleur libre).

## Ce qui n'existe pas encore

Limitation : il n'y a **pas** de véritable **palette de fils** :

- pas de base de fils par fabricant/gamme/référence ;
- pas de recherche du fil le plus proche par distance perceptuelle ;
- pas d'association objet ↔ bobine/aiguille ;
- pas d'import/export de palette.

La bibliothèque `thread_palette` évoquée dans l'étude de cadrage **n'est pas
présente** dans `libs/`. La couleur reste un simple RGB attaché à l'objet.

## Conséquence pour le DST

Le format DST ne stocke de toute façon **pas** les couleurs réelles, seulement
des arrêts « changement de fil ». L'ordre des couleurs est porté par le document
`.osp`, pas par le DST (voir *Format DST*).

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — champ `rgb`.
- `libs/stitch_generation/src/generate.cpp` — insertion des `ColorChange`.
- `libs/segmentation/src/segmentation.cpp` — `recolor_region`.
- *Information non déterminée* : aucune source `thread_palette` dans le dépôt.
