# Licences

Public : mainteneur, contributeur.

## Licence du logiciel

OpenStitch Studio est distribué sous **Apache-2.0** (permissive, avec clause de
brevets). Fichier : `LICENSE`. Chaque fichier source porte
`// SPDX-License-Identifier: Apache-2.0`.

Conséquence importante, formulée avec prudence : intégrer directement du code
sous licence **copyleft** (comme la GPL) dans ce projet imposerait
vraisemblablement de distribuer l'ensemble dérivé sous une licence compatible
avec cette licence copyleft, ce qui empêcherait probablement de conserver le
tout **uniquement** sous Apache-2.0. La compatibilité exacte dépend de la
version de la licence et de la manière dont le code est combiné.

En pratique, pour **conserver la distribution du projet sous Apache-2.0**, aucun
code sous licence copyleft incompatible avec cet objectif ne doit être intégré
directement. Les **idées algorithmiques** peuvent être réimplémentées
indépendamment à partir de sources publiques, sans reprise de code ni de
structure expressive protégée. **Toute réutilisation de code tiers doit faire
l'objet d'une vérification de licence spécifique.** Voir
`docs/stitch-engine-research.md`.

## Dépendances externes

| Bibliothèque | Version | Rôle | Licence | Intégration | URL |
|---|---|---|---|---|---|
| fmt | via vcpkg | formatage | MIT | vcpkg | github.com/fmtlib/fmt |
| spdlog | via vcpkg | journalisation | MIT | vcpkg | github.com/gabime/spdlog |
| CLI11 | via vcpkg | args CLI | BSD-3-Clause | vcpkg | github.com/CLIUtils/CLI11 |
| OpenCV (core/imgproc/imgcodecs) | via vcpkg | image | Apache-2.0 | vcpkg | opencv.org |
| libpng / libjpeg-turbo / libtiff / zlib | transitives | codecs | zlib / IJG / libtiff / zlib | via OpenCV | — |
| Clipper2 | via vcpkg | booléens/offsets | BSL-1.0 | vcpkg (encapsulée) | github.com/AngusJohnson/Clipper2 |
| nlohmann/json | via vcpkg | JSON projet | MIT | vcpkg (encapsulée) | github.com/nlohmann/json |
| minizip-ng | via vcpkg | ZIP projet | zlib | vcpkg (encapsulée) | github.com/zlib-ng/minizip-ng |
| Catch2 v3 | via vcpkg | tests | BSL-1.0 | vcpkg (dev) | github.com/catchorg/Catch2 |
| Qt 6.8 LTS (Widgets/Gui/Core) | binaires officiels | interface | **LGPL-3.0** | liaison dynamique | qt.io |

Les versions exactes sont verrouillées par la **baseline vcpkg** (`vcpkg.json`).

## Qt et la LGPL-3.0

Qt est utilisé en **liaison dynamique** avec les DLL officielles non modifiées :
elles sont remplaçables par l'utilisateur, aucune source Qt n'est modifiée, et
seuls des modules LGPL sont utilisés (pas de module GPL-only ni commercial). Ce
mode est compatible avec la distribution d'un logiciel Apache-2.0.

## Licences de la chaîne documentaire

La documentation est générée avec `python-markdown` (BSD), `xhtml2pdf` (Apache-2.0)
et `reportlab` (BSD), `svglib` (LGPL, utilisée comme outil externe, non liée au
produit). Ces outils ne sont pas distribués avec le logiciel.

## Implémentation associée

- `LICENSE`, `THIRD_PARTY_LICENSES.md`.
- `docs/phase0/03-bibliotheques-licences.md` (analyse d'origine, ADR-002).
- `docs/stitch-engine-research.md` (point juridique Ink/Stitch).
