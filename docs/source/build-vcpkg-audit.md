# Audit vcpkg et temps de préparation CI (BUILD-OPT-01.5)

Date de l'audit : 2026-11-19. Source : `vcpkg.json`, `.github/workflows/ci.yml`, journaux des runs
GitHub Actions (lecture seule via `gh run view --log`). Tout chiffre ci-dessous vient d'un run daté ;
ce qui n'a pas pu être mesuré est indiqué comme tel (§6).

## 1. Constat principal : le cache binaire déclaré ne sert à rien

`ci.yml` déclarait `VCPKG_BINARY_SOURCES: "clear;x-gha,readwrite"` et exportait `ACTIONS_CACHE_URL` /
`ACTIONS_RUNTIME_TOKEN`. La configuration existe donc bien, mais dans les trois runs dont le journal
complet a été lu (44169011644 le 2026-11-17, 44203767918 le 2026-11-17/18, 44383502174 le 2026-11-18, et
44324516933 pour Windows), vcpkg écrit à chaque configuration :

> `warning: The 'x-gha' binary caching backend has been removed. Consider using a NuGet-based binary caching provider instead`

Conséquences mesurées : aucune ligne « Restored » n'apparaît, les 26 paquets Windows (et leurs
équivalents Linux) sont **reconstruits depuis les sources à chaque run**, y compris OpenCV.
Le backend a disparu de l'outil vcpkg téléchargé par le bootstrap (`vcpkg-tool` 2026-10-29, vu dans
les journaux Windows). Le dépôt cloné `microsoft/vcpkg` n'était pas épinglé (clone de `master`), donc
l'outil suit l'amont sans que la CI le choisisse.

Cause secondaire : le clone non épinglé fait aussi dériver les scripts vcpkg, qui entrent dans
l'empreinte ABI des paquets. Preuve de dérive : l'ABI de `opencv4[core,jpeg,png,tiff]:x64-windows@4.12.0#5`
vaut `937b08f6…` le 2026-11-17 et `10cea79d…` le 2026-11-18 (même version, mêmes features). La cause
exacte (scripts, image runner, hash du compilateur) n'est pas démontrée ; l'ABI Linux, elle, est
restée stable (`dfe80e7c…`).

## 2. Chiffres du run 44324516933 (main, `b502f616`, 2026-11-18 14:27Z)

Run en échec (erreur interne MSVC C1001 dans `generate.cpp`, corrigée ensuite par #408), mais utile
pour la phase de préparation :

| Phase | Durée |
|---|---|
| Install Qt (aqtinstall, dont pip) | 122 s |
| Set up vcpkg (clone + bootstrap) | 17 s |
| Début `-- Running vcpkg install` → fin du dernier paquet (14:30:35 → 14:40:30) | 9 min 55 s |
| dont `opencv4` | 6 min |
| dont `catch2` / `libjpeg-turbo` / `liblzma` / `libpng`+`tiff` | 44 s / 28 s / 24 s / 5 s + 16 s |
| `-- Configuring done` | 638,5 s |
| Début configuration → début MSBuild (14:30:35 → 14:41:18) | **10 min 43 s** |

Les ~10 min 45 s signalées sont donc presque entièrement la construction vcpkg sans cache. Autre
constat : le journal affiche 18 `Building x64-windows-dbg`. vcpkg construit les bibliothèques Debug
**et** Release alors que le job Windows ne compile que Release.

## 3. Premier build, cache chaud, changement de dépendance

Mesures sans cache effectif (état actuel), toutes les valeurs viennent des journaux ou de `gh run view` :

| Run (date) | Job | « Configuring done » | `opencv4` | Étape Configure+build / Configure |
|---|---|---|---|---|
| 44169011644 (11-17) | Windows | 762,9 s | 7,9 min | 2956 s |
| 44203767918 (11-17/18) | Windows | 482,7 s | 4,9 min | 1762 s |
| 44383502174 (11-18) | Windows | 501,7 s | 4,9 min | 1907 s |
| 44324516933 (11-18) | Windows | 638,5 s | 6 min | échec compilation |
| 44169011644 | Linux (1re configuration) | 477 s | 4,2 min | 478 s |
| 44203767918 | Linux (1re configuration) | 755,6 s | 6,7 min | 756 s |
| 44383502174 | Linux (1re / 2e configuration) | 511 s / 467 s | 4,3 / 4,2 min | 512 s + ~470 s |

À retenir :

- **Premier build / cache absent** (c'est aujourd'hui *tous* les runs) : 8 à 13 min de vcpkg, avec une
  variance liée à la charge du runner (opencv 4,9 à 7,9 min pour le même code).
- **Linux paie deux fois** : `cmake --preset linux-core` puis la configuration « Build Linux CLI
  (Release) » reconstruisent les mêmes paquets (2 configurations × ~8 min dans 44383502174). Un cache
  local fonctionnel supprime la seconde.
- **Cache chaud** : non mesurable, il n'a jamais fonctionné dans les runs lus. Estimation, non mesurée :
  restauration des archives + extraction, de l'ordre de la minute ; à confirmer au premier run de
  `main` après fusion.
- **Changement de dépendance** : avec le cache par paquet (ABI), seuls les paquets modifiés et leurs
  dépendants se reconstruisent ; `restore-keys` permet de repartir du cache précédent.
  Changer `opencv4` ou une dépendance de ses sources (zlib, libjpeg-turbo, libpng, tiff) invalide OpenCV
  (~5-8 min).

## 4. Optimisation implémentée (`.github/workflows/ci.yml`)

Changements minimaux, localisés (autre session sur `ci.yml` : 01.7) :

1. **Vcpkg épinglé au `builtin-baseline`** : variable `VCPKG_COMMIT` =
   `4e82e29f14eac2b3422f18f72c7524d04f19924e` (le SHA de `vcpkg.json`, qui reste la référence
   verrouillée ; les deux doivent être changés ensemble). `git checkout --detach` après le clone
   (clone complet conservé : la résolution de baseline en a besoin). Effet attendu : ABI stables. Effet
   de bord à vérifier en CI : l'outil vcpkg devient celui du commit épinglé (`VCPKG_TOOL_RELEASE_TAG=2026-07-13`
   au lieu de `2026-10-29`).
2. **Backend de cache `files` + `actions/cache`** (restauration avant l'installation, sauvegarde après
   « Configure and build » / « Configure ») : dossier `$RUNNER_TEMP/vcpkg-bincache`, clé
   `vcpkg-<os>-<hash(vcpkg.json)>-<VCPKG_COMMIT>`, repli par préfixe `vcpkg-<os>-`. La sauvegarde a lieu même
   si la compilation C++ échoue (`!cancelled()`), mais seulement sur `main`, pour ne pas dépasser le quota
   de 10 Go du dépôt (les PR restaurent le cache de `main`). Une PR qui change `vcpkg.json` reconstruit donc à
   froid jusqu'à sa fusion.
3. Le backend `x-gha` et la variable globale `VCPKG_BINARY_SOURCES` sont retirés ; les étapes
   « Export GitHub Actions cache environment » sont laissées en place (inertes, hors périmètre).

Rien n'a été supprimé côté artefacts, tests ou compilation ; aucune CI rouge n'est contournée.
**Non validé** : je n'ai pas poussé, donc ce changement n'a pas tourné en CI (§6).

### Correctifs amont conservés dans le dépôt (2026-11-20)

GitHub répond par intermittence 503 aux URL `github.com/<dépôt>/commit/<sha>.patch?full_index=1` que
téléchargent les ports opencv4 et spdlog hors cache binaire. Le 2026-11-20, `linux-core` a raté ses trois
configurations de suite, chaque fois sur un correctif différent. L'entrée de cache `vcpkg-linux-…` de
`main`, vide (206 o) depuis le 2026-11-19, aggravait la panne : chaque job reconstruisait les 28 paquets
(clé corrigée par la PR #440). L'étape « Seed vcpkg downloads (mirrored patches, patient prefetch) » dépose
les copies de `.github/vcpkg-downloads/` dans `downloads/`. vcpkg les reprend (« Using cached ») après
avoir vérifié leur SHA512. Une copie que le vcpkg épinglé n'attend plus est ignorée avec un avertissement.
Le README du dossier décrit la mise à jour. L'API REST ne peut pas remplacer ces fichiers : ses correctifs
ont des index abrégés, donc une autre empreinte.

Le même jour, le correctif `commit/f0888a1….diff` d'opencv4 a renvoyé 503 de façon persistante, même
depuis un poste personnel. Sa copie a été reconstruite avec `git diff --full-index <parent> <sha>`, qui
redonne exactement les octets attendus. Les archives de libtiff (gitlab.com) et de freetype
(gitlab.freedesktop.org) ont aussi renvoyé 429 : `PREFETCH` les fait précharger par curl, qui respecte
`Retry-After`, et l'archive n'est déposée que si son SHA512 est celui du portfile. Depuis que le cache
`vcpkg-linux-v2-…` est rempli (209 Mo le 2026-11-20), ce dispositif ne sert qu'aux paquets absents du
cache : changement de `vcpkg.json` ou de `VCPKG_COMMIT`, ou éviction après 7 jours sans accès.

## 5. Pistes non implémentées

| Piste | Gain attendu | Pourquoi pas ici |
|---|---|---|
| Triplet Release seul en CI (`VCPKG_BUILD_TYPE release`, triplet overlay) | Évite la construction `-dbg` des 26 paquets : de l'ordre de la moitié d'une construction à froid (non mesuré) | Touche `CMakePresets.json`/`build.ps1` (autres sessions, notamment les presets 01.2) ; le Debug local en a besoin |
| `opencv4` : réduire les modules via un port overlay | Jusqu'à plusieurs minutes à froid | Porte sur une dépendance de la couche `image` ; à mesurer d'abord (build OpenCV minimal) |
| `tiff[lzma,zip]`, `minizip-ng` (bzip2, lzma, zstd…) : features par défaut | `liblzma` 24 s + `zstd` 13 s + `bzip2` 5 s à froid | Gain faible face au cache ; changer les features modifie le comportement d'import/export |
| `catch2` hors feature `tests` pour la release | 44 s à froid sur Windows | Le job Windows lance les tests ; sans intérêt tant que le cache fonctionne |
| Aspect Qt (122 s dans 44324516933 ; 62-90 s ailleurs) | Peu | Hors périmètre vcpkg ; cache `install-qt-action` à étudier par 01.7 |

## 6. Limites et vérifications à faire

- Les journaux avant le 2026-11-17 n'ont pas été lus : on ignore depuis quand `x-gha` est inopérant.
- L'état « cache chaud » n'est pas mesuré ; le premier run de `main` après fusion alimente le cache, le
  deuxième donne la mesure. Comparer alors `Configuring done` à 480-760 s.
- Vérifier au premier run que `bootstrap-vcpkg` fonctionne avec l'outil `2026-07-13` du commit épinglé
  et que la ligne de warning `x-gha` a disparu. Si le bootstrap échoue, retirer l'étape `checkout --detach`
  (le cache `files` fonctionne aussi avec le `master` cloné, mais moins stable).
- Actions/cache impose 10 Go par dépôt et une éviction à 7 jours sans accès ; un `main` inactif plus d'une
  semaine repartira à froid.
