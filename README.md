# OpenStitch Studio

> Nom temporaire — voir ADR-001.

Logiciel de bureau **libre et gratuit** de numérisation pour broderie machine : de l'image matricielle au fichier de broderie (DST en premier), avec contrôle manuel à chaque étape.

- **Langage** : C++ moderne (C++23, minimum requis C++20 pour les contributeurs)
- **Plateforme prioritaire** : Windows 10/11 (cœur portable, build Linux vérifié en CI)
- **Licence** : [Apache-2.0](LICENSE) — voir [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
- **Interface** : Qt 6 Widgets (LGPL, liaison dynamique) — le cœur ne dépend jamais de Qt
- **Fonctionnement** : 100 % local, sans compte, sans cloud, sans télémétrie

## État du projet

**Phase 1 — socle.** Rien d'utilisable pour broder encore. Voir la [roadmap](docs/phase0/08-roadmap-adr.md) et l'[étude de cadrage](docs/phase0/README.md).

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
apps/cli       openstitch-cli : pipeline et inspection sans interface graphique
libs/          Bibliothèques cœur (core, image, …) — voir docs/phase0/02-architecture.md
tests/         Tests unitaires, d'intégration et golden
docs/          Documentation (cadrage, architecture, formats, guides)
```

## Contribuer

Le projet est conçu pour être contribuable : dépendances via vcpkg (manifeste versionné), presets CMake, CI. Règle absolue : **aucun code copié d'un logiciel propriétaire ni d'une source incompatible Apache-2.0**.
