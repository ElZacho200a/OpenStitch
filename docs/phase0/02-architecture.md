# Phase 0 — Architecture proposée et structure du dépôt

## 1. Principes directeurs

1. **Le cœur ne dépend pas de Qt.** Toute la logique (modèle, algorithmes, formats) est compilable sans interface graphique — vérifié par la CLI et par un build Linux du cœur en CI.
2. **Dépendances orientées vers le bas uniquement** : `apps → libs`, jamais l'inverse ; entre libs, un graphe acyclique explicite (voir §3).
3. **Une abstraction n'est créée que lorsqu'elle est utilisée.** La structure ci-dessous est la cible ; les répertoires ne sont créés que lorsque leur première fonctionnalité arrive.
4. **Chaque lib = une cible CMake** (`openstitch::core`, `openstitch::geometry`, …) avec ses propres tests.
5. **Les heuristiques textiles (compensation, densité, ordre) sont des Strategy remplaçables**, jamais codées en dur dans les générateurs.

## 2. Structure du dépôt (cible)

```
openstitch/
├── CMakeLists.txt              # racine, options globales
├── CMakePresets.json           # presets MSVC Debug/Release, Ninja
├── vcpkg.json                  # manifeste des dépendances
├── LICENSE                     # GPLv3 (voir 03-bibliotheques-licences.md)
├── THIRD_PARTY_LICENSES.md
├── README.md
├── apps/
│   ├── desktop/                # application Qt Widgets (seule cible dépendant de Qt)
│   └── cli/                    # openstitch-cli : pipeline sans GUI + inspecteur de formats
├── libs/
│   ├── core/                   # types forts (unités, ids), erreurs, résultats, logging
│   ├── geometry/               # points, polygones, polylignes, courbes, offsets (encapsule Clipper2)
│   ├── image/                  # chargement, prétraitement (encapsule OpenCV)
│   ├── segmentation/           # quantification, régions connexes, carte de régions
│   ├── vectorization/          # contours → chemins vectoriels, simplification
│   ├── document/               # modèle métier : document, calques, objets, palette, ordre
│   ├── stitch/                 # commandes de points, séquences, statistiques
│   ├── stitch_generation/      # générateurs : running, triple, tatami (puis satin)
│   ├── stitch_analysis/        # moteur de règles de validation
│   ├── thread_palette/         # fils, palettes, distance perceptuelle
│   ├── machine_profiles/       # cadres, machines (minimal au début)
│   ├── formats/                # DST (codec maison), SVG diagnostic, registre de formats
│   ├── project_io/             # format projet (.osp) : JSON + ZIP, versionnement
│   └── commands/               # undo/redo (Command pattern) sur le document
├── tests/
│   ├── unit/                   # par lib
│   ├── integration/            # chaînes complètes
│   └── golden/                 # fichiers de référence + comparateur
├── docs/
├── resources/                  # icônes, palettes de fils de démonstration
├── examples/                   # images et projets d'exemple
├── tools/                      # scripts (format, packaging)
└── cmake/                      # modules CMake partagés
```

Notes vs. l'arborescence du cahier des charges : `embroidery` et `stitch_generation` sont fusionnés au départ (`stitch` = données, `stitch_generation` = algorithmes) ; `rendering`, `simulation`, `optimization` n'apparaîtront qu'aux phases qui les utilisent (11–12) ; `format_inspector` est une sous-commande de la CLI (`openstitch-cli inspect motif.dst`) plutôt qu'une application séparée — moins de coquilles vides.

## 3. Diagramme textuel des modules et dépendances

```
                          ┌─────────────┐   ┌─────────────┐
                          │ apps/desktop│   │  apps/cli   │
                          │  (Qt 6)     │   │  (CLI11)    │
                          └──────┬──────┘   └──────┬──────┘
                                 │                 │
              ┌──────────────────┼─────────────────┤
              ▼                  ▼                 ▼
        ┌──────────┐      ┌────────────┐    ┌───────────┐
        │ commands │─────▶│  document  │◀───│project_io │──▶ (miniz, json)
        └──────────┘      └─────┬──────┘    └───────────┘
                                │ contient objets + réfs
        ┌───────────────────────┼────────────────────────────┐
        ▼                       ▼                            ▼
  ┌───────────────┐    ┌─────────────────┐          ┌───────────────┐
  │thread_palette │    │stitch_generation│─────────▶│    stitch     │◀── formats (DST, SVG)
  └───────┬───────┘    └────────┬────────┘          └───────┬───────┘
          │                     │                           │
          │            ┌────────┴───────┐          ┌────────┴──────┐
          │            ▼                ▼          ▼               │
          │      ┌──────────┐   ┌──────────────┐ ┌───────────────┐ │
          │      │ geometry │◀──│vectorization │ │stitch_analysis│ │
          │      └────┬─────┘   └──────┬───────┘ └───────────────┘ │
          │           │                │                           │
          │           │         ┌──────┴──────┐                    │
          │           │         ▼             │                    │
          │           │   ┌──────────────┐    │                    │
          │           │   │ segmentation │    │                    │
          │           │   └──────┬───────┘    │                    │
          │           │          ▼            │                    │
          │           │    ┌──────────┐       │                    │
          │           │    │  image   │──▶ (OpenCV)                │
          │           │    └────┬─────┘       │                    │
          ▼           ▼         ▼             ▼                    ▼
        ┌────────────────────────────────────────────────────────────┐
        │                      core (unités, ids, erreurs, log)      │
        └────────────────────────────────────────────────────────────┘
```

Règles : `core` ne dépend de rien (hors STL, fmt, spdlog). `geometry` est la seule lib qui voit Clipper2. `image`/`segmentation` sont les seules qui voient OpenCV. `formats` est la seule qui connaît l'encodage DST. `apps/desktop` est la seule cible qui voit Qt.

## 4. Patrons de conception retenus (et refusés)

| Besoin | Patron | Justification |
|---|---|---|
| Undo/redo | **Command** (lib `commands`) | Requis §22 ; commandes granulaires avec `apply`/`revert`, fusion des commandes contiguës (drag) ; pas de snapshot complet du document |
| Générateurs de points | **Strategy + registry** | `IStitchGenerator` par type d'objet ; le satin s'ajoutera sans toucher au moteur |
| Algorithmes de segmentation | **Strategy** | median cut / k-means interchangeables |
| Formats de fichiers | **Registry** | `FormatRegistry` : extension → codec ; ajout de PES/JEF plus tard sans modification du cœur |
| Notification du document | **Observer** (signaux maison dans `document`, connectés aux signaux Qt côté GUI) | Le cœur ne peut pas utiliser les signaux Qt |
| Ordre de couture | **Graphe de dépendances** (Phase 12) | Contraintes avant optimisation |
| Compensation | **Strategy** (`ICompensationModel`) | Heuristiques v1 remplaçables (§7 du cahier des charges) |
| Visitor | **Refusé pour l'instant** | `std::variant` + `std::visit` sur les types d'objets suffit et reste plus simple |
| Singletons | **Interdits** | Contexte passé explicitement |

## 5. Multithreading (architecture, pas implémentation immédiate)

Dès la Phase 1 : les opérations lourdes passent par une interface `Task` (annulable, progression, résultat) exécutée hors du thread UI, résultat rapatrié sur le thread UI par le mécanisme Qt (`QMetaObject::invokeMethod`/signaux). Le cœur fournit des fonctions **pures sur des snapshots** (le document est copié ou la partie concernée est immuable pendant le calcul) — c'est la contrainte d'architecture ; l'infrastructure complète (pool, file) viendra quand la première opération lente existera (Phase 3–4).

## 6. CLI (validation de la séparation cœur/GUI)

Sous-commandes prévues, dans l'ordre où les capacités apparaissent :

```
openstitch-cli info image.png            # Phase 1 : métadonnées image
openstitch-cli quantize in.png -k 4      # Phase 4
openstitch-cli vectorize ...             # Phase 5
openstitch-cli stats motif.dst           # Phase 7 : inspecteur DST
openstitch-cli dst2svg motif.dst out.svg # Phase 7 : SVG de diagnostic
openstitch-cli export projet.osp out.dst # Phase 10
```
