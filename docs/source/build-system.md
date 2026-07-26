# Compilation et développement

Public : développeur, mainteneur.

## Chaîne de build

- **CMake** (≥ 3.27) avec `CMakePresets.json`.
- **MSVC** (Visual Studio 2022+) sous Windows ; générateur par défaut du preset.
- **vcpkg** en mode manifeste (`vcpkg.json`, baseline verrouillée).
- **Qt 6.8** hors vcpkg (variable `QT_ROOT`).

## Presets

| Preset | Cible |
|---|---|
| `msvc` (configure) | Windows, application complète |
| `msvc-debug` / `msvc-release` (build/test) | Debug / Release |
| `linux-core` | Cœur + CLI **sans Qt** (garde-fou de portabilité) |

## Commandes

```powershell
$env:VCPKG_ROOT = "C:\dev\vcpkg"
$env:QT_ROOT    = "C:\Qt\6.8.3\msvc2022_64"

cmake --preset msvc                 # configuration (premier run long : vcpkg)
cmake --build --preset msvc-debug   # compilation Debug
ctest  --preset msvc-debug          # tests
cmake --build --preset msvc-release # Release
```

## Options CMake

- `OPENSTITCH_BUILD_DESKTOP` (ON) — construit l'application Qt.
- `OPENSTITCH_BUILD_TESTS` (ON) — construit les tests (Catch2).
- Cible d'interface `openstitch::warnings` : `/W4 /permissive- /utf-8` (MSVC),
  `-Wall -Wextra -Wconversion` (GCC/Clang), liée en PRIVATE par chaque cible.

## Déploiement des DLL (Windows)

L'application lie dynamiquement Qt et OpenCV. `windeployqt` copie les DLL Qt à
côté de l'exécutable (étape POST_BUILD) ; vcpkg copie ses propres DLL.

## Linux (cœur uniquement)

Le preset `linux-core` compile les libs cœur et la CLI **sans Qt** — c'est un
garde-fou pour éviter toute dépendance Windows/Qt involontaire dans le cœur.
L'interface graphique n'est pas construite sous Linux à ce stade.

## Erreurs fréquentes

- « Qt6 introuvable » → vérifier `QT_ROOT`.
- Erreur de toolchain vcpkg → vérifier `VCPKG_ROOT` et le bootstrap.
- Compilation OpenCV très longue → normal au premier configure (cache ensuite).
- Édition de lien de l'exe échouée (`LNK1168`) → l'application est en cours
  d'exécution ; la fermer.

## Intégration continue

Un workflow GitHub Actions (`.github/workflows/ci.yml`) est présent : build MSVC
Debug/Release + tests + artefacts, build Linux du cœur, et vérification du
formatage. *Information non déterminée* : aucun dépôt distant n'étant déclaré, la
CI n'a pas encore été exécutée.

## Implémentation associée

- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`.
- `.github/workflows/ci.yml`, `.clang-format`.
- `docs/build-windows.md`.
