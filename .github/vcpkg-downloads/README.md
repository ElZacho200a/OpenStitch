# Téléchargements vcpkg préparés pour la CI

Quand le cache binaire ne fournit pas un paquet, vcpkg retélécharge ses sources. Le 2026-11-20, deux
familles d'adresses ont fait échouer `linux-core` trois configurations de suite :

- `https://github.com/<dépôt>/commit/<sha>.patch|.diff?full_index=1` : GitHub répond 503, par
  intermittence pour certains correctifs, durablement pour d'autres (le `.diff` d'opencv4 renvoyait
  503 même depuis un poste personnel) ;
- les archives `…/-/archive/…` de gitlab.com et gitlab.freedesktop.org : réponses 429.

`seed.sh` prépare `downloads/` de vcpkg avant la configuration (étape « Seed vcpkg downloads » de
`.github/workflows/ci.yml`) :

1. il dépose les copies de ce dossier, listées dans `SHA512SUMS` ;
2. il précharge les archives de `PREFETCH` avec curl, qui réessaie en respectant `Retry-After`.

Rien n'est déposé si le SHA512 ne figure pas dans un portfile du vcpkg épinglé. Une copie altérée
fait échouer l'étape ; une archive préchargée dont l'empreinte diffère est écartée. vcpkg vérifie
encore lui-même chaque empreinte.

Ce dossier ne sert que lorsqu'un paquet manque au cache binaire, par exemple après un changement de
`vcpkg.json` ou de `VCPKG_COMMIT`, ou une éviction du cache après 7 jours sans accès.

| Copie (nom attendu par le port) | Port (baseline `4e82e29f`) | Origine | Licence |
|---|---|---|---|
| `468de9b36740b3355f0d5cd8be2ce28b340df120.patch` | opencv4 4.12.0#5 | opencv/opencv@468de9b | Apache-2.0 |
| `opencv4-support-cuda-13-f0888a10e8266b2202d930c6974433a421e6f9a7.diff` | opencv4 4.12.0#5 | opencv/opencv@f0888a1 | Apache-2.0 |
| `spdlog-backport-3541.patch` | spdlog 1.17.0#1 | gabime/spdlog@1685e69 | MIT |
| `spdlog-backport-3543.patch` | spdlog 1.17.0#1 | gabime/spdlog@d5af52d | MIT |

## Mise à jour (montée de `builtin-baseline` / `VCPKG_COMMIT`)

`seed.sh` ignore, avec un avertissement, tout fichier que les ports du vcpkg épinglé n'attendent plus :
une entrée périmée ne casse jamais le build. Pour en ajouter une :

- **correctif `.patch`** : `curl -L "<url du portfile>" -o <FILENAME>` ;
- **correctif `.diff`** (si l'adresse renvoie 503) : le reconstruire avec git, qui donne les mêmes
  octets. `git fetch --depth=2 --filter=blob:none https://github.com/<dépôt>.git <sha>` puis
  `git diff --full-index <parent> <sha> > <FILENAME>` ;
- **archive d'un hôte limité** : une ligne `<SHA512 du portfile> <FILENAME> <URL exacte>` dans
  `PREFETCH`, sans copie dans le dépôt.

Pour une copie, vérifier que `sha512sum` donne le SHA512 du portfile, puis l'ajouter à `SHA512SUMS`
(deux espaces entre l'empreinte et le nom). Les journaux CI montrent les noms attendus
(`Downloading <url> -> <FILENAME>`).
