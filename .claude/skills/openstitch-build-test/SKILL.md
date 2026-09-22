---
name: openstitch-build-test
description: Compiler et tester OpenStitch efficacement (build ciblé par lib, exécutables Catch2/QTest, filtres, format, garde structurelle). À utiliser après toute modification de code C++ ou CMake, avant d'annoncer qu'une tâche est terminée, quand un test échoue, ou quand l'utilisateur demande de « builder », « lancer les tests » ou « vérifier que ça passe ».
---

# Build & tests OpenStitch

Principe : **boucle courte pendant l'itération (cible + exécutable de test),
boucle complète avant de conclure.** Ne jamais annoncer « c'est fait » sans
avoir compilé et lancé au moins les tests des libs touchées ; rapporter les
échecs tels quels (sortie à l'appui).

Toutes les commandes se lancent depuis la racine du dépôt, via l'outil
**PowerShell** (MSVC/CMake Windows). `VCPKG_ROOT` et `QT_ROOT` doivent être
définis ; s'ils ne le sont pas, `.\scripts\build.ps1` bootstrap la chaîne
d'outils.

## 1. Boucle courte (itération)

Compiler uniquement la lib modifiée et son exécutable de test :

```powershell
cmake --build --preset msvc-debug --target openstitch_<lib> test_<lib>
build\msvc\tests\unit\<lib>\Debug\test_<lib>.exe "nom ou sous-chaine du TEST_CASE"
build\msvc\tests\unit\<lib>\Debug\test_<lib>.exe "[tag]"
```

Correspondance lib → exécutable (dossier `build\msvc\tests\unit\<dossier>\Debug\`) :

| Lib modifiée | Exécutable |
|---|---|
| `stitch`, `stitch_generation` (running, tatami, satin, directionnel, finitions, overrides) | `stitch\test_stitch.exe` |
| `auto_satin` / `satin_planning` / `satin_coverage` | `test_auto_satin` / `test_satin_planning` / `test_satin_coverage` |
| `commands` | `commands\test_commands.exe` |
| `project_io` | `project_io\test_project_io.exe` |
| autres libs | `<lib>\test_<lib>.exe` |
| pipeline bout en bout | `build\msvc\tests\integration\Debug\test_pipeline.exe` |
| `apps/desktop` | `desktop\test_main_window.exe`, `test_properties_panel`, `test_canvas_view`, `test_document_panel`… |

- Tests desktop : passer par **`ctest --preset msvc-debug -R test_main_window`**
  (ctest fournit `QT_QPA_PLATFORM=offscreen` et le PATH Qt). En direct :
  `$env:QT_QPA_PLATFORM='offscreen'` d'abord. La cible de l'app est `openstitch`.
- Une modif dans `document`, `geometry` ou `core` impacte presque tout :
  passer directement à la boucle complète.
- `ctest --preset msvc-debug -R <sous-chaine>` filtre par nom de test CTest.

## 2. Boucle complète (avant de conclure)

```powershell
cmake --build --preset msvc-debug
ctest --preset msvc-debug --output-on-failure
```

(ou `.\scripts\build.ps1 -Configuration Debug -Test -SkipBootstrap`). Lancer
aussi Release (`msvc-release`) quand le changement touche à de l'arithmétique
flottante, de l'optimisation ou du code dépendant de l'ordre d'évaluation —
la CI teste les deux.

Contrôles que la CI fera de toute façon :

- **Format** (Bash) : `git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror`
  — corriger avec `clang-format -i <fichiers modifiés>` uniquement, jamais tout le dépôt.
- **Garde `effective_sequence`** : le test CTest `check_no_raw_sequence_bypass`
  échoue si un nouvel appel à `generate_sequence` apparaît hors de
  `libs/stitch_generation/` sans annotation `raw-sequence-ok: <raison>`.
  Presque toujours, la bonne correction est d'appeler `effective_sequence`.
- **Portabilité `linux-core`** : aucun include Qt ni API Windows dans `libs/`
  ou `apps/cli`. Vérifier par grep (`QString`, `<Q`, `windows.h`) si doute.

## 3. Écrire / modifier un test

- Nom de `TEST_CASE` **en ASCII pur** (pas d'accents : la découverte Catch2
  casse sous l'encodage console Windows). La prose/commentaires restent en
  français accentué.
- Tout générateur de points : ajouter un test de **déterminisme** (deux
  exécutions → séquences identiques octet pour octet).
- Pas de conteneur non ordonné dont l'ordre d'itération fuit dans une sortie.
- Desktop : QTest + QSignalSpy, headless, **pas de `sleep`, pas de
  comparaison de pixels** ; tester le vrai chemin UI (actions de
  `MainWindow`), pas seulement la commande.
- Nouveau fichier de test : l'ajouter au `CMakeLists.txt` du dossier
  (`add_executable(test_<lib> …)` ou `openstitch_add_qt_test(nom fichier.cpp)`).
- Les SVG dorés de `tests/golden/` ne sont jamais réécrits par un test
  (voir le skill `openstitch-cli-debug` pour les régénérer).

## 4. En cas d'échec

1. Relancer **seulement** le test en échec avec son exécutable et son nom,
   ajouter `-s` (Catch2) pour voir les `INFO`/`CAPTURE`.
2. Erreur de link `LNK2019` après ajout d'un `.cpp` : vérifier qu'il est
   listé dans le `CMakeLists.txt` de la lib, et que la lib cliente linke
   bien `openstitch::<lib>`.
3. Cache CMake incohérent (nouveau fichier non vu, preset modifié) :
   `cmake --preset msvc` suffit en général ; `-Clean` de `build.ps1` en
   dernier recours (vcpkg recompile tout, très lent).
4. Un test existant casse à cause d'un changement volontaire de
   comportement : le dire explicitement à l'utilisateur avant de modifier
   l'attendu du test.

## 5. Rapport final

Indiquer ce qui a été lancé et le résultat chiffré, p. ex. « test_stitch :
212 cas / 3 104 assertions OK ; ctest Debug complet : 38/38 ». Si une étape
n'a pas été faite (Release, desktop, format), le dire.
