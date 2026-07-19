# OpenStitch Studio

> Nom temporaire — voir ADR-001.

Logiciel de bureau **libre et gratuit** de numérisation pour broderie machine : de l'image matricielle au fichier de broderie (DST en premier), avec contrôle manuel à chaque étape.

- **Langage** : C++ moderne (C++23, minimum requis C++20 pour les contributeurs)
- **Plateforme prioritaire** : Windows 10/11 (cœur portable, build Linux vérifié en CI)
- **Licence** : [Apache-2.0](LICENSE) — voir [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
- **Interface** : Qt 6 Widgets (LGPL, liaison dynamique) — le cœur ne dépend jamais de Qt
- **Fonctionnement** : 100 % local, sans compte, sans cloud, sans télémétrie

## État du projet

**Chaîne complète fonctionnelle (phases 1 à 13 implémentées).** De l'image au fichier DST brodable, avec contrôle manuel à chaque étape :

1. import PNG/JPEG/BMP/TIFF, taille physique en millimètres ;
2. prétraitement non destructif (recadrage, symétries, rotations, luminosité/contraste, débruitage, quantification) ;
3. segmentation perceptuelle (CIELAB) en régions éditables (fusion, suppression, recoloration) ;
4. vectorisation (contours, trous, simplification, édition de nœuds) ;
5. objets de broderie : **point droit/triple, remplissage tatami, colonne satin** ;
6. **numérisation automatique** (image → objets éditables selon la forme des régions) ;
7. ordre de couture manuel et optimisé (par couleur / proximité, verrous) ;
8. analyse pré-export (points trop courts/longs, sauts, hors cadre) et simulation de couture animée ;
9. export/import **DST**, export SVG de diagnostic ;
10. format de projet **`.osp`** (sauvegarde/chargement complet).

Undo/redo sur toutes les opérations. 121 tests unitaires et d'intégration.

Voir la [roadmap](docs/phase0/08-roadmap-adr.md) et l'[étude de cadrage](docs/phase0/README.md).

> **Note honnête** : ce socle est complet et testé, mais n'a pas encore été validé sur une machine à broder réelle. Les heuristiques de compensation (tirage, densité) et les conventions DST de certaines machines demandent des essais terrain avant un usage en production.

## Compilation (Windows)

Voir [docs/build-windows.md](docs/build-windows.md). Résumé :

```powershell
# Prérequis : Visual Studio 2022+ (C++), CMake ≥ 3.27, Git, vcpkg, Qt 6 (binaires officiels)
$env:VCPKG_ROOT = "C:\chemin\vers\vcpkg"
$env:QT_ROOT    = "C:\chemin\vers\Qt\6.8.3\msvc2022_64"

cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

## Structure

```
apps/desktop   Application Qt (seule cible dépendant de Qt)
apps/cli       openstitch-cli : info / stats / dst2svg (sans interface graphique)
libs/
  core            unités fortes (µm/mm/px), ids, Result, logging
  geometry        chemins, simplification (Douglas-Peucker), booléens/offsets (Clipper2)
  image           chargement + prétraitement non destructif (OpenCV encapsulé)
  segmentation    quantification CIELAB, régions connexes à ids stables
  vectorization   régions → contours vectoriels propres
  document        modèle métier (projet, objets vectoriels et de broderie)
  stitch          commandes machine, statistiques
  stitch_generation  point droit/triple, tatami, satin
  stitch_analysis    moteur de règles de validation
  optimization    ordre de couture (coût, stratégies)
  autodigitize    image → objets éditables automatiquement
  commands        undo/redo (Command pattern)
  formats         codec DST maison, SVG de diagnostic
  project_io      format projet .osp (JSON + ZIP)
tests/         121 tests unitaires et d'intégration
docs/          Documentation (cadrage, architecture, formats, guides)
```

Le cœur (tout sauf `apps/desktop`) ne dépend jamais de Qt — vérifiable en compilant la CLI et via le job Linux de la CI.

## Contribuer

Le projet est conçu pour être contribuable : dépendances via vcpkg (manifeste versionné), presets CMake, CI. Règle absolue : **aucun code copié d'un logiciel propriétaire ni d'une source incompatible Apache-2.0**.
