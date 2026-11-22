#!/usr/bin/env bash
# Prépare <racine vcpkg>/downloads/ avant la configuration (README.md de ce dossier).
# Usage : bash .github/vcpkg-downloads/seed.sh <racine vcpkg>
#
# 1. Copies conservées ici (SHA512SUMS) : correctifs « commit/<sha>.patch|.diff » auxquels GitHub
#    répond 503, par intermittence ou durablement. Copie altérée : échec de l'étape, jamais de repli
#    silencieux sur le réseau.
# 2. Préchargement patient (PREFETCH) : archives d'hôtes qui répondent 429 (GitLab). curl réessaie en
#    respectant Retry-After ; un échec n'est qu'un avertissement (vcpkg réessaiera lui-même).
# Rien n'est déposé si le SHA512 ne figure pas dans un portfile du vcpkg épinglé : après une montée
# de baseline, une entrée devenue inutile est ignorée (avertissement) et vcpkg télécharge comme
# avant. vcpkg recontrôle lui-même l'empreinte de chaque fichier de downloads/.
set -euo pipefail

vcpkg_root=${1:?usage: seed.sh <racine vcpkg>}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
downloads="$vcpkg_root/downloads"
mkdir -p "$downloads"

expected_by_ports() { grep -rqsF --include=portfile.cmake "$1" "$vcpkg_root/ports"; }

(cd "$here" && sha512sum --check --strict SHA512SUMS)

while read -r hash name; do
  [ -n "$hash" ] || continue
  if expected_by_ports "$hash"; then
    cp "$here/$name" "$downloads/$name"
    echo "downloads/$name : copie locale déposée"
  else
    echo "::warning::$name n'est plus attendu par les ports du vcpkg épinglé : copie ignorée (à retirer de SHA512SUMS)"
  fi
done < "$here/SHA512SUMS"

while read -r hash name url; do
  case "$hash" in '' | '#'*) continue ;; esac
  if ! expected_by_ports "$hash"; then
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
