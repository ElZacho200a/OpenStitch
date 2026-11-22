# Correctifs amont pour vcpkg (CI)

Copies à l'octet près de correctifs que des ports vcpkg téléchargent depuis
`https://github.com/<dépôt>/commit/<sha>.patch?full_index=1`. Depuis le 2026-11-20, GitHub répond par
intermittence 503 à ces adresses depuis les runners : le job `linux-core` échouait trois tentatives de
suite dès qu'opencv4 ou spdlog ne venaient pas du cache binaire. L'API REST ne remplace pas ces
fichiers : elle produit des index abrégés, donc une autre empreinte.

`seed.sh` les dépose dans `downloads/` de vcpkg avant la configuration (étape « Seed vcpkg
downloads » de `.github/workflows/ci.yml`). vcpkg vérifie lui-même le SHA512 de chaque fichier.

| Fichier (nom attendu par le port) | Port (baseline `4e82e29f`) | Origine | Licence |
|---|---|---|---|
| `468de9b36740b3355f0d5cd8be2ce28b340df120.patch` | opencv4 4.12.0#5 | opencv/opencv@468de9b | Apache-2.0 |
| `spdlog-backport-3541.patch` | spdlog 1.17.0#1 | gabime/spdlog@1685e69 | MIT |
| `spdlog-backport-3543.patch` | spdlog 1.17.0#1 | gabime/spdlog@d5af52d | MIT |

## Mise à jour (montée de `builtin-baseline` / `VCPKG_COMMIT`)

`seed.sh` ignore avec un avertissement tout fichier que les ports du vcpkg épinglé n'attendent plus :
une copie périmée ne casse jamais le build. Pour en ajouter une, télécharger l'URL exacte du portfile
(`curl -L "<url>" -o <FILENAME>`), vérifier que `sha512sum` donne le SHA512 du portfile, puis l'ajouter
à `SHA512SUMS` (deux espaces entre l'empreinte et le nom).
