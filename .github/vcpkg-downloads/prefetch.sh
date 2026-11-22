#!/usr/bin/env bash
# Précharge dans <racine vcpkg>/downloads/ les archives listées dans PREFETCH (README.md).
# Usage : bash .github/vcpkg-downloads/prefetch.sh <racine vcpkg>
#
# Appelé par l'étape Configure de la CI après une configuration ratée seulement : quand le cache
# binaire fournit tous les paquets, aucune requête n'est faite.
# - Hôtes qui répondent 429 (GitLab) : curl réessaie patiemment en respectant Retry-After.
# - Une entrée dont le SHA512 ne figure dans aucun portfile du vcpkg épinglé est ignorée sans
#   requête ; une archive dont l'empreinte diffère est écartée.
# - Tout échec n'est qu'un avertissement : vcpkg réessaiera lui-même et revérifiera l'empreinte.
set -euo pipefail

vcpkg_root=${1:?usage: prefetch.sh <racine vcpkg>}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
downloads="$vcpkg_root/downloads"
mkdir -p "$downloads"

while read -r hash name url; do
  case "$hash" in '' | '#'*) continue ;; esac
  if ! grep -rqsF --include=portfile.cmake "$hash" "$vcpkg_root/ports"; then
    echo "::warning::$name n'est plus attendu par les ports du vcpkg épinglé : préchargement ignoré (à retirer de PREFETCH)"
    continue
  fi
  target="$downloads/$name"
  [ -f "$target" ] && continue
  if curl --fail --location --silent --show-error --retry 6 --retry-all-errors \
    --retry-max-time 240 --max-time 120 --output "$target.part" "$url" &&
    [ "$(sha512sum "$target.part" | cut -d' ' -f1)" = "$hash" ]; then
    mv "$target.part" "$target"
    echo "downloads/$name : préchargé"
  else
    rm -f "$target.part"
    echo "::warning::$name : préchargement impossible ou empreinte inattendue, vcpkg réessaiera"
  fi
done < "$here/PREFETCH"
