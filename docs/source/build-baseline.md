# Coûts réels de compilation : protocole et mesures de base (BUILD-OPT-01.1)

Ce chapitre fixe **où part le temps** avant toute optimisation (chantier Notion BUILD-OPT-01).
Il contient un protocole reproductible, le script qui l'applique, et les mesures datées. Aucun
gain n'est promis ici : les chiffres servent de référence « avant ».

- Script : `scripts/measure-build.ps1` (PowerShell 5.1+, n'écrit que dans `build/measure/` et ne
  modifie jamais le contenu des sources).
- Données brutes : [`build-baseline-data.csv`](build-baseline-data.csv) (build local, une ligne par
  étape) et [`build-baseline-ci-data.csv`](build-baseline-ci-data.csv) (CI, une ligne par étape de job).

## 1. Environnement des mesures locales

| Élément | Valeur |
|---|---|
| Date | 2026-11-19 (09:46 – 10:40 heure locale) |
| Commit | `bbe66c36` (`main` + aucun changement de code ; seul le script de mesure est ajouté) |
| Machine | SHARK32 : AMD Ryzen 7 7700X (8 cœurs / 16 threads), 15,1 Go de RAM, Windows 11 Pro 10.0.26200 |
| Qt | 6.8.3 (`msvc2022_64`) |
| MSVC | `cl` 19.50.35728.0 (Visual Studio 18 Community / générateur « Visual Studio 18 2026 ») |
| CMake | 4.4.0-rc3 |
| Dépendances vcpkg | déjà installées (`vcpkg_installed` du dépôt principal réutilisé via `-DVCPKG_INSTALLED_DIR`, 502 Mo, jamais copié ni modifié) : **le coût de compilation d'OpenCV & co n'est donc pas mesuré** |
| Parallélisme | `--parallel 2` + `UseMultiToolTask`, `MultiProcMaxCount=2`, `CL_MPCount=2` (mêmes propriétés que `scripts/build.ps1`, avec 2 au lieu de 16) |
| Cible construite | `openstitch_v2_studio` (Studio + ses dépendances, windeployqt inclus), pas `ALL_BUILD` |

## 2. Protocole de mesure

Principe : une étape = un processus (`cmake --preset msvc`, `cmake --build … --target …`, `ctest`).
Pour chaque étape, le script relève :

- le **temps mur** ;
- le **pic de RAM de l'arbre de processus** lancé (cmake, MSBuild, `cl`, `link`, windeployqt…),
  somme des working sets échantillonnée toutes les 500 ms ;
- le **pic de mémoire physique utilisée sur toute la machine** (colonne `peak_sys_used_mb`) :
  inclut le bruit des autres programmes ;
- la **charge CPU moyenne sur 2 s et le nombre de `cl.exe`/`link.exe` hors mesure** juste avant
  l'étape (indicateur de bruit) ;
- les temps **cumulés** MSBuild des tâches compilateur (`MultiToolTask`/`CL`), `Link` et `Exec`
  (windeployqt, rcc…). Ils sont additionnés sur les nœuds parallèles : ils ne s'additionnent pas au temps mur.

Scénarios (chacun : 3 runs, sauf mention) :

| Scénario | Ce qui est fait avant l'étape chronométrée |
|---|---|
| `configure-fresh` / `build-cold` | suppression de `build/msvc`, puis configure (+ build) |
| `configure-noop` | reconfiguration avec cache existant |
| `build-noop` | second build sans aucun changement |
| `touch-qml` | `apps/desktop_v2/studio/qml/ActionPalette.qml` : date de modification mise à « maintenant » |
| `touch-studio-cpp` | idem pour `apps/desktop_v2/studio/edit_controller.cpp` |
| `touch-core-cpp` | idem pour `libs/core/src/log.cpp` |
| `touch-core-hpp` | idem pour `libs/core/include/openstitch/core/units.hpp` (header central inclus par de nombreux fichiers) |
| `build-tests` / `tests` | construction de `test_core` + `test_desktop_v2_studio_shell`, puis `ctest -R <filtre>` |
| `ci` | lecture seule `gh run list` / `gh run view` : durée de chaque job et étape des derniers runs `ci.yml` réussis |

Les « touch » ne changent que l'horodatage (restauré à la fin) : MSBuild raisonne sur les dates, donc
le résultat reproduit une modification réelle sans polluer `git`.

Commandes (depuis la racine du dépôt) :

```powershell
# Plusieurs scénarios : passer par -Command (avec -File, la liste est lue comme une seule chaîne)
powershell -NoProfile -Command "& ./scripts/measure-build.ps1 -Scenario build-cold -Runs 1 -Configuration Release -VcpkgInstalledDir <chemin vcpkg_installed>"
powershell -NoProfile -Command "& ./scripts/measure-build.ps1 -Scenario build-noop,touch-qml,touch-studio-cpp,touch-core-cpp,touch-core-hpp -Runs 3 -Configuration Release"
powershell -NoProfile -Command "& ./scripts/measure-build.ps1 -Scenario build-tests -Runs 1 -Target test_core,test_desktop_v2_studio_shell"
powershell -NoProfile -Command "& ./scripts/measure-build.ps1 -Scenario tests -Runs 3 -TestRegex 'check_|atomic|DST|parallel'"
powershell -NoProfile -File scripts/measure-build.ps1 -Scenario ci -CiRuns 6
```

Pour comparer « avant / après » une optimisation : mêmes scénarios, même machine **au repos**, et ajouter
les lignes au CSV (colonne `sha`).

## 3. Mesures locales (Release et Debug)

Secondes de temps mur ; « min / médiane / max » sur les runs valides. RAM = pic de l'arbre de processus
(max sur les runs). Temps cumulés MSBuild : médiane des runs, secondes de CPU-tâche.

### Release (`openstitch_v2_studio`)

| Scénario | Runs | min / médiane / max (s) | Pic RAM arbre | Compilo cumulé | Link cumulé | Exec (windeployqt…) cumulé |
|---|---|---|---|---|---|---|
| configure (cache supprimé) | 1 (+1 run partiel, voir §5) | 60,8 | 161 Mo | – | – | – |
| **build à froid** | **1** | **492,6** | 920 Mo | 714,2 s | 7,4 s | 7,5 s |
| configure sans changement | 3 | 27,3 / 27,5 / 44,1 | 156 Mo | – | – | – |
| build sans changement | 3 | 6,1 / 6,7 / 11,7 | 174 Mo | 0,8 s | 0 s | 2,4 s |
| modification QML | 3 | 6,8 / 8,0 / 18,1 | 989 Mo | 1,0 s | 1,7 s | 1,5 s |
| modification d'un CPP Studio | 3 | 11,4 / 11,7 / 13,3 | 975 Mo | 5,1 s | 1,5 s | 1,7 s |
| modification d'un CPP `libs/core` | 3 | 8,9 / 9,6 / 10,6 | 1 033 Mo | 2,5 s | 2,7 s | 1,6 s |
| **modification du header `libs/core`** | 3 | **368,5 / 375,1 / 379,4** | 804 Mo | 493,1 s | 4,1 s | 2,9 s |
| construction de `test_core` + `test_desktop_v2_studio_shell` | 1 | 26,1 | 725 Mo | 6,8 s | 2,6 s | 1,1 s |
| `ctest` (16 tests `check_*`/atomic/DST/parallel) | 3 | 8,3 / 9,1 / 13,6 | 63 Mo | – | – | – |

### Debug (`openstitch_v2_studio`)

| Scénario | Runs | min / médiane / max (s) | Pic RAM arbre | Compilo cumulé | Link cumulé | Exec cumulé |
|---|---|---|---|---|---|---|
| configure (cache supprimé) | 1 | 39,2 | 162 Mo | – | – | – |
| **build à froid** | **1** | **343,4** | 1 632 Mo | 532,0 s | 7,7 s | 6,3 s |
| build sans changement | 3 | 4,1 / 4,9 / 5,2 | 147 Mo | 0,5 s | 0 s | 1,3 s |
| modification QML | 3 | 4,7 / 4,7 / 4,7 | 161 Mo | 0,6 s | 0,5 s | 1,2 s |
| modification d'un CPP Studio | 3 | 8,7 / 8,7 / 8,8 | 164 Mo | 3,8 s | 0,6 s | 1,2 s |
| modification d'un CPP `libs/core` | 3 | 5,6 / 5,7 / 6,9 | 228 Mo | 2,0 s | 0,4 s | 1,3 s |

Lecture (faits, pas des recommandations) :

- Le coût dominant est **la recompilation en cascade d'un header central** : ~6 min en Release pour
  `units.hpp`, contre ~10 s pour un `.cpp` du même module. Un build à froid de Studio seul prend ~8 min (Release).
- Une modification de QML ou d'un `.cpp` isolé coûte 5–20 s ; le link et windeployqt pèsent chacun quelques secondes cumulées.
- Le temps de configuration CMake (~27–60 s) est comparable à un rebuild incrémental complet : il est payé
  à chaque ajout/suppression de fichier ou changement de `CMakeLists.txt`.
- Le pic de RAM de l'arbre reste ≤ 1,7 Go à `--parallel 2`.

## 4. Temps CI (GitHub Actions, lecture seule)

Source : `gh run list/view` sur `ci.yml`, 6 derniers runs **réussis** au 2026-11-19 (SHA `f6ef98d6`,
`a667d588`, `cba44220`, `babadf58`, `bbe66c36`, `b1017289`). Le workflow construit `ALL_BUILD`
(Studio, CLI, MCP, tests, V1 Widgets) via `scripts/build.ps1 -Configuration Release`. Durées en secondes.

| Job / étape | min | médiane | max |
|---|---|---|---|
| Durée totale du run | 2 565 | 3 540 | 4 169 |
| `windows-msvc` (job entier) | 2 562 | 3 233 | 3 890 |
| └ Install Qt | 62 | 76,5 | 81 |
| └ **Configure and build Release** (vcpkg + `ALL_BUILD`) | 1 907 | 2 353 | 3 053 |
| └ Build/capture Studio (cibles déjà construites) | 16 | 19,5 | 23 |
| └ **Test (Release)** (`ctest` complet) | 525 | 664 | 799 |
| `linux-core` (job entier) | 2 090 | 2 118 | 3 240 |
| └ Configure | 492 | 507 | 774 |
| └ Build | 458 | 468 | 711 |
| └ Test | 398 | 400 | 657 |
| └ Build Linux CLI (Release) | 639 | 646 | 1 028 |
| `format` (clang-format) | 13 | ~20 | 776 (1 run aberrant) |

Observations : le chemin critique est `windows-msvc` ; plus de la moitié part dans « Configure and build »
et un cinquième dans les tests. La variance (×1,6 sur le build Windows) suggère un effet de cache vcpkg/runner
partagé. Les temps par étape complets sont dans `build-baseline-ci-data.csv`. Les temps de **runners CI ne sont pas
comparables aux temps locaux** (matériel et parallélisme différents ; la CI compile en plus V1, CLI, MCP et tests).

## 5. Limites et honnêteté des chiffres

- **Bruit** : les mesures locales ont été prises **pendant que sept autres sessions compilaient sur la même
  machine** (15 Go de RAM presque saturés : `peak_sys_used_mb` ≈ 14,9–15,4 Go ; 2 à 7 `cl.exe`/`link.exe`
  tiers au moment des mesures). Les temps sont donc des **majorants bruités** ; ils doivent être refaits à vide
  avant de chiffrer un gain. Le build à froid Release (492,6 s, avec 4 à 7 compilateurs tiers actifs) a été mesuré
  sur une machine plus chargée que le Debug (343,4 s, ~2 tiers) : **ne pas comparer Debug et Release** sur ces seuls chiffres.
- **Un seul run à froid par configuration** (et non 3) : un build à froid à 2 jobs sur machine partagée dure ~6–8 min
  et bloque l'arbre ; le critère « 3 runs » n'est pas satisfait pour ces deux lignes et pour la construction des tests.
- **Charge CPU « avant » à 0 % pour tous les runs Release jusqu'au `tests` inclus** : le compteur utilisé
  (`Get-Counter`, noms localisés) échouait en silence sur Windows en français ; remplacé ensuite par WMI. Les
  colonnes `cpu_before_pct` valent donc 0 *par erreur* pour ces lignes ; seul le nombre de compilateurs tiers
  (`foreign_compilers`) y est fiable.
- **Lignes à écarter du CSV** : (a) la première ligne `build-cold Release configure` (77,3 s, 09:46) est un essai
  interrompu par un bug du script (échantillonnage de RAM) corrigé ensuite ; (b) les trois lignes `tests Release`
  de 10:27 (~0,5 s) n'ont exécuté aucun test (« No tests were found » : cibles de test pas encore construites) ;
  les vrais runs de tests sont ceux de 10:30–10:31.
- **Non mesuré** : `ALL_BUILD` (V1 Widgets, CLI, MCP, suite de tests complète) ; suite `ctest` complète (179 tests
  enregistrés ; seuls 16 ont été exécutés) ; header central en Debug ; compilation initiale des dépendances vcpkg ;
  runners CI en build à froid sans cache. Le temps de « link » n'est connu que comme temps cumulé de la tâche `Link`
  MSBuild (faible : ≤ 8 s cumulé à froid) ; `Exec` regroupe windeployqt et les étapes personnalisées sans les séparer.
- Les temps cumulés MSBuild (compilo, link, exec) sont des sommes de tâches parallèles, pas des durées murales.
- Les temps CI reflètent l'état du cache à la date indiquée ; seul 1 job (`format`) a une valeur aberrante, conservée dans le CSV.

