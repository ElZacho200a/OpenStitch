# Licences des dépendances tierces

Ce fichier est mis à jour à **chaque** ajout ou retrait de dépendance.

| Dépendance | Version (baseline vcpkg) | Licence | Rôle | Lien |
|---|---|---|---|---|
| fmt | via vcpkg | MIT | Formatage de chaînes | https://github.com/fmtlib/fmt |
| spdlog | via vcpkg | MIT | Journalisation | https://github.com/gabime/spdlog |
| CLI11 | via vcpkg | BSD-3-Clause | Arguments de la CLI | https://github.com/CLIUtils/CLI11 |
| OpenCV (core, imgproc, imgcodecs) | via vcpkg | Apache-2.0 | Chargement et traitement d'images | https://opencv.org |
| libpng, libjpeg-turbo, libtiff, zlib | transitives (OpenCV) | zlib / IJG / libtiff / zlib | Codecs d'images | — |
| Catch2 v3 | via vcpkg | BSL-1.0 | Tests (dev uniquement) | https://github.com/catchorg/Catch2 |
| Qt 6.8 LTS (Widgets, Gui, Core) | binaires officiels | **LGPL-3.0** | Interface graphique | https://www.qt.io |

## Note sur Qt (LGPL-3.0)

Qt est utilisé en **liaison dynamique** avec les DLL officielles non modifiées, conformément à la LGPLv3 :

- les DLL Qt distribuées avec l'application sont remplaçables par l'utilisateur ;
- aucune modification n'est apportée aux sources de Qt ;
- seuls des modules Qt sous licence LGPL sont utilisés (pas de module GPL-only ni commercial).

## Dépendances prévues (non encore intégrées)

| Dépendance | Licence | Phase |
|---|---|---|
| Clipper2 | BSL-1.0 | 5 (vectorisation) |
| nlohmann/json | MIT | 10 (format projet) |
| minizip-ng | zlib | 10 (format projet) |
