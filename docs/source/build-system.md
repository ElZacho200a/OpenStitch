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

## Workflow de build rapide (BUILD-OPT-01)

Principe : pendant l'itération, ne compiler que la cible touchée et son exécutable de
test ; la suite complète n'est lancée qu'avant livraison. Seules les commandes de la
section « Existant aujourd'hui » sont garanties ; le reste est la **méthode prévue**.

### Existant aujourd'hui

```powershell
.\scripts\qrun.ps1 build-x cmake --build --preset msvc-debug --target openstitch_<lib> test_<lib>
.\scripts\qrun.ps1 test-x  build\msvc\tests\unit\<lib>\Debug\test_<lib>.exe "sous-chaine du TEST_CASE"
.\scripts\qrun.ps1 ctest-x ctest --preset msvc-debug -R <sous-chaine> --output-on-failure
```

- **Build ciblé** : `--target` d'une lib + son `test_<lib>`. Table lib → exécutable dans le
  skill `openstitch-build-test` §1. Modif de `core`/`geometry`/`document`, de CMake ou d'un
  en-tête très inclus : passer à la suite complète.
- **Tests minimaux pertinents** : exécutable Catch2 de la lib avec filtre de nom ou `[tag]` ;
  tests desktop via `ctest -R test_main_window` (fournit `QT_QPA_PLATFORM=offscreen`).
  Les dépendants d'une lib se trouvent avec `analyze_impact("<lib>")`.
- **Validation complète avant livraison** : `cmake --build --preset msvc-debug` puis
  `ctest --preset msvc-debug --output-on-failure` ; Release en plus si flottants, optimisation
  ou ordre d'évaluation sont touchés ; clang-format **18** sur les fichiers modifiés ;
  garde `check_no_raw_sequence_bypass`. Jamais de reconstruction complète entre deux itérations.
- **Presets existants** : `msvc`, `msvc-relwithdebinfo` (configure), `msvc-debug`,
  `msvc-release`, `msvc-release-relwithdebinfo`, `linux-core`.
- QML de Studio : `apps/desktop_v2/studio/qml/`. Aujourd'hui, toute modification QML passe
  par une reconstruction de la cible Studio.

### Outils de build rapide (BUILD-OPT-01)

Noms vérifiés dans le dépôt ; chaque outil a sa page de détail.

- **Build ciblé de développement** (01.2) : `scripts/build-dev.ps1` (Ninja + MSVC, Studio seul,
  Widgets V1 non construits). Presets `dev-debug`, `dev-relwithdebinfo`, `dev-notests` de
  `CMakePresets.json` ; les presets historiques et `scripts/build.ps1` sont inchangés.
- **QML sans recompilation** (01.3) : option CMake `OPENSTITCH_STUDIO_DEV_QML` (désactivée par
  défaut, jamais en Release) et variable d'exécution `OPENSTITCH_DEV_QML=1` ; Studio charge les
  `.qml` depuis le disque et recharge à chaud. Les changements C++ (types exposés, `qmldir`,
  ressources) imposent toujours un build.
- **Cache d'objets sccache** (01.4) : preset `msvc-sccache`, POC optionnel, voir
  [build-sccache.md](build-sccache.md) (utile pour les rebuilds dans le même arbre, pas entre
  worktrees).
- **Amorçage d'un worktree** : `scripts/worktree-seed-build.ps1` copie le `build/msvc` d'une autre
  copie (dépôt principal par défaut), réécrit ses chemins absolus, réaligne les dates des sources
  identiques et reconfigure : le build suivant ne recompile que ce qui diffère.
- **Cache vcpkg et préparation CI** (01.5) : voir [build-vcpkg-audit.md](build-vcpkg-audit.md).
- **Parallélisme, PCH, Unity** (01.6) : `scripts/build-jobs.ps1`, option `OPENSTITCH_PCH`
  (désactivée par défaut), `scripts/check-unity-safety.py`, `scripts/measure-build-ab.ps1` ;
  voir [build-pch-unity.md](build-pch-unity.md).
- **CI** (01.7) : jobs rapides et porte de validation dans `.github/workflows/ci.yml`, voir
  [build-ci.md](build-ci.md).
- **Mesure** (01.1) : `scripts/measure-build.ps1`, résultats dans
  [build-baseline.md](build-baseline.md).

### Protocole de mesure

Tout gain annoncé doit être mesuré, avant/après, dans les mêmes conditions :

1. Même machine, même configuration (Debug), autres builds arrêtés, antivirus inchangé.
2. Trois scénarios : **à froid** (dossier de build neuf, `-Clean`), **incrémental trivial**
   (aucune modification), **incrémental réaliste** (toucher un `.cpp` de lib, puis un en-tête
   de `document`, puis un `.qml`).
3. Mesurer avec `Measure-Command { cmake --build --preset msvc-debug --target <cible> }`
   (secondes, mur) ; répéter 3 fois, retenir la médiane ; le premier passage seul pour le froid.
4. Consigner : date, commit, cible, scénario, médiane, écart. Un gain sous l'écart observé
   entre répétitions n'est pas un gain.
5. Le cache (sccache/vcpkg) se mesure à froid avec cache vide **et** avec cache chaud.

## Erreurs fréquentes

- « Qt6 introuvable » → vérifier `QT_ROOT`.
- Erreur de toolchain vcpkg → vérifier `VCPKG_ROOT` et le bootstrap.
- Compilation OpenCV très longue → normal au premier configure (cache ensuite).
- Édition de lien de l'exe échouée (`LNK1168`) → l'application est en cours
  d'exécution ; la fermer.

## Intégration continue

Un workflow GitHub Actions (`.github/workflows/ci.yml`) est présent : build MSVC
Debug/Release + tests + artefacts, build Linux du cœur, et vérification du
formatage ; sur `main` elle publie aussi la release « latest » (voir
[Installation](installation.md)).

## Implémentation associée

- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`.
- `.github/workflows/ci.yml`, `.clang-format`.
- `docs/source/installation.md` — installation et compilation Windows (`docs/build-windows.md` y renvoie).
