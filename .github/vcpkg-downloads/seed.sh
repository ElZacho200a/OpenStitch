#!/usr/bin/env bash
# Dépose dans <racine vcpkg>/downloads/ les correctifs amont conservés dans ce dossier (README.md).
# Usage : bash .github/vcpkg-downloads/seed.sh <racine vcpkg>
#
# - Correctifs « commit/<sha>.patch|.diff » auxquels GitHub répond 503, par intermittence ou
#   durablement. Copie altérée (SHA512SUMS faux) : échec de l'étape, jamais de repli silencieux sur
#   le réseau.
# - Un fichier n'est déposé que si son SHA512 figure dans un portfile du vcpkg épinglé : après une
#   montée de baseline, un correctif devenu inutile ou régénéré est ignoré (avertissement) et vcpkg
#   le télécharge comme avant.
# - vcpkg recontrôle lui-même l'empreinte de chaque fichier de downloads/ : rien n'est contourné.
set -euo pipefail

vcpkg_root=${1:?usage: seed.sh <racine vcpkg>}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
downloads="$vcpkg_root/downloads"
mkdir -p "$downloads"

(cd "$here" && sha512sum --check --strict SHA512SUMS)

while read -r hash name; do
  [ -n "$hash" ] || continue
  if grep -rqsF --include=portfile.cmake "$hash" "$vcpkg_root/ports"; then
    cp "$here/$name" "$downloads/$name"
    echo "downloads/$name : copie locale déposée"
  else
    echo "::warning::$name n'est plus attendu par les ports du vcpkg épinglé : copie ignorée (à retirer de SHA512SUMS)"
  fi
done < "$here/SHA512SUMS"
