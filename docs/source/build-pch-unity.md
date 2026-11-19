# Parallélisme mémoire, PCH sélectifs et Unity Build (BUILD-OPT-01.6)

Public : développeur, mainteneur. Tout est **opt-in** : sans option, la configuration CMake et la sortie
sont inchangées.

## 1. Bornage des jobs selon la RAM

MSVC `cl.exe` consomme ~1,5 Go par unité de traduction lourde. Sur une machine 16 Go partagée,
`CL_MPCount = nb de cœurs` fait swapper.

- `scripts/build-jobs.ps1` : `jobs = clamp(floor((RAM libre − 2 Go) / 1,5 Go), 1, cœurs)` (détail sur stderr,
  entier sur stdout). `-GoPerJob 2.5` conseillé avec Unity. `OPENSTITCH_JOBS=n` force la valeur.
- `scripts/build.ps1` : sans `-Jobs`, utilise ce calcul (avant : tous les cœurs). `-Jobs n` reste prioritaire.
- Ligne de commande manuelle : `cmake --build --preset msvc-debug --parallel (& scripts\build-jobs.ps1)`.

## 2. PCH sélectifs

`-DOPENSTITCH_PCH=ON` applique `target_precompile_headers` aux cibles de `OPENSTITCH_PCH_TARGETS`
(défaut : `geometry`, `stitch_generation`, `stitch_analysis`), avec `cmake/pch/<lib>.hpp` (en-têtes standard
stables + `clipper2/clipper.h` pour geometry). Aucun en-tête du projet n'y figure (une modification d'un en-tête
public ne doit pas tout recompiler). Ajouter une lib = un `cmake/pch/<lib>.hpp` + son nom dans la liste.

## 3. Unity Build

`-DOPENSTITCH_UNITY_TARGETS="openstitch_x;openstitch_y"` (lot : `OPENSTITCH_UNITY_BATCH`, défaut 8).
Jamais activé par défaut. Préalable obligatoire : `python scripts/check-unity-safety.py libs/<lib>`
(symboles `static`/espace anonyme homonymes, `#define`, `using namespace` de fichier). Les collisions
donnent une erreur de compilation (C2084), donc échec bruyant ; le risque silencieux restant est un
changement de résolution de surcharge entre fonctions homonymes de signatures différentes. Les
`TEST_CASE` ne sont pas concernés (Unity n'est proposé que pour des libs de production, pas les tests).

## 4. Mesures A/B (machine 16 Go, Release, `--parallel 2`, RAM libre 0,6–2,5 Go : bruit élevé)

Build propre de la cible **et de ses dépendances** (`--clean-first`) ; pic = somme de l'arbre de processus
du build, échantillonnée à 500 ms (borne basse). Une seule mesure par cas.

| Cible | Base | PCH | Unity |
|---|---|---|---|
| geometry | 28,6 s / 0,27 Go | **18,6 s** / 0,26 Go | échec C2084 (9 collisions) |
| stitch_generation | 133 s / 0,49 Go | **107 s** / 0,47 Go | échec (12 collisions) |
| project_io (0 collision) | 196,5 s / 0,52 Go | n/m | **148,7 s** / 0,54 Go (lib 26,1→17,2 Mo) |
| stitch_render (0 collision) | 20,1 s / 0,26 Go | n/m | 19,3 s / 0,27 Go (neutre, dans le bruit) |

Lecture : le PCH gagne 19-35 % sans surcoût mémoire mesurable ; `lib` produite de même taille. Le bruit des
autres builds concurrents interdit de conclure à moins de ~10 %.

## 5. Statut d'activation et rollback

- **PCH** : prouvé sur 2 libs, **reste OFF par défaut**. Avant d'activer dans la CI : refaire A/B sur runner propre,
  puis `ctest` complet (le PCH ne change pas le code généré en principe ; déterminisme à confirmer par les tests
  de golden/DST sur un build complet — non exécuté ici).
- **Unity** : gain sur project_io (−24 %), neutre sur stitch_render ; reste OFF par défaut, car l'équivalence des sorties (tests complets) n'a pas été exécutée. Refusé pour geometry et stitch_generation (collisions). Seules les libs à 0 collision sont candidates.
- **Rollback** : retirer l'option du cache (`cmake -DOPENSTITCH_PCH=OFF -DOPENSTITCH_UNITY_TARGETS=`) ; rien
  d'autre n'est modifié. Régression de temps, de pic RAM ou de tests : désactiver et supprimer le dossier de build.
- Ninja vs Visual Studio : non comparé (01.2 absent de cette branche).

## 6. Outils

`scripts/measure-build.ps1 -BuildDir <dir> -Target <t> -Jobs 2` (temps + pic RAM, JSON),
`scripts/check-unity-safety.py`, `scripts/build-jobs.ps1`.
