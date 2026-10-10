# Licences des dépendances tierces

Ce fichier est mis à jour à **chaque** ajout ou retrait de dépendance.

| Dépendance | Version (baseline vcpkg) | Licence | Rôle | Lien |
|---|---|---|---|---|
| fmt | via vcpkg | MIT | Formatage de chaînes | https://github.com/fmtlib/fmt |
| spdlog | via vcpkg | MIT | Journalisation | https://github.com/gabime/spdlog |
| CLI11 | via vcpkg | BSD-3-Clause | Arguments de la CLI | https://github.com/CLIUtils/CLI11 |
| OpenCV (core, imgproc, imgcodecs) | via vcpkg | Apache-2.0 | Chargement et traitement d'images | https://opencv.org |
| libpng, libjpeg-turbo, libtiff, zlib | transitives (OpenCV) | zlib / IJG / libtiff / zlib | Codecs d'images | — |
| Clipper2 | via vcpkg | BSL-1.0 | Opérations booléennes et offsets de polygones (encapsulée dans libs/geometry) | https://github.com/AngusJohnson/Clipper2 |
| nlohmann/json | via vcpkg | MIT | Sérialisation du format projet (encapsulée dans libs/project_io) | https://github.com/nlohmann/json |
| minizip-ng | via vcpkg | zlib | Archive ZIP du format projet .osp (encapsulée dans libs/project_io) | https://github.com/zlib-ng/minizip-ng |
| Catch2 v3 | via vcpkg | BSL-1.0 | Tests (dev uniquement) | https://github.com/catchorg/Catch2 |
| Qt 6.8 LTS (Widgets, Gui, Core) | binaires officiels | **LGPL-3.0** | Interface graphique | https://www.qt.io |

## Note sur Qt (LGPL-3.0)

Qt est utilisé en **liaison dynamique** avec les DLL officielles non modifiées, conformément à la LGPLv3 :

- les DLL Qt distribuées avec l'application sont remplaçables par l'utilisateur ;
- aucune modification n'est apportée aux sources de Qt ;
- seuls des modules Qt sous licence LGPL sont utilisés (pas de module GPL-only ni commercial).

## Nuanciers de fils

Ce ne sont pas des dépendances logicielles (pas de code ni de licence
logicielle tierce liée) mais des **données de fait** (code fabricant ↔ nom ↔
approximation RGB publiée) chargées en mémoire par `libs/thread_palette`
(C-S1-02 : constantes C++ compilées, pas de fichier lu à l'exécution). Chaque
ligne documente la provenance exigée par la procédure de sourçage
(S1-POLICY-1/2) : une ligne par nuancier, jamais fusionnée.

| Fabricant | Gamme | Source (URL) | Date de consultation |
|---|---|---|---|
| Madeira | Polyneon 40 | **DONNÉES PLACEHOLDER — non sourcées** : à transcrire depuis https://www.madeira.co.uk (fiche couleurs Polyneon 40) | — (pas encore consultée ; voir `libs/thread_palette/data/madeira_polyneon.cpp`) |
| Isacord | Isacord 40 | **DONNÉES PLACEHOLDER — non sourcées** : à transcrire depuis https://www.isacord.com (fiche couleurs Isacord 40) | — (pas encore consultée ; voir `libs/thread_palette/data/isacord_40.cpp`) |

**Important** : les deux fichiers de données ci-dessus contiennent
actuellement des codes/noms/RGB **inventés** (forme plausible d'un nuancier
réel, pas une transcription) car l'environnement d'implémentation initial
n'avait pas d'accès web pour sourcer les valeurs réelles. À remplacer par une
vraie transcription avant toute utilisation hors développement/test — voir
`docs/source/palettes-and-threads.md` (section *Catalogue*) et
`specs/plans/thread-palette-implementation.md` (section 4).

## Dépendances prévues (non encore intégrées)

_(Toutes les dépendances prévues en Phase 0 sont désormais intégrées.)_

## Tables de fils des formats machine (PES, JEF)

`libs/formats/src/machine_palettes.cpp` : tables de 64 fils PEC (Brother) et de 78 fils JEF
(Janome), valeurs RGB factuelles transcrites depuis **pyembroidery** (EmbThreadPec.py,
EmbThreadJef.py), licence MIT, (c) The Embroidermodder Team / contributeurs pyembroidery.
Aucun code n'est repris : les codecs PES/JEF/EXP sont réécrits d'après la structure des formats.
Voir `docs/source/formats-pes-jef-exp.md`.
