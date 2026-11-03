#!/usr/bin/env bash
# Package the Release Linux CLI for GitHub Actions artifacts and GitHub Releases.
# Usage: bash packaging/linux/package-cli.sh PATH_TO_BINARY OUTPUT_DIR
set -euo pipefail

if (( $# != 2 )); then
  echo 'Usage: package-cli.sh PATH_TO_BINARY OUTPUT_DIR' >&2
  exit 2
fi

binary=$1
out_dir=$2
if [[ ! -f "$binary" || ! -x "$binary" ]]; then
  echo "Missing or non-executable OpenStitch CLI: $binary" >&2
  exit 1
fi

# Do not distribute an untested binary: verify startup and command parsing.
"$binary" --version
"$binary" --help >/dev/null

mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
bundle="$stage/openstitch-cli-linux-x86_64"
mkdir -p "$bundle"
cp "$binary" "$bundle/openstitch-cli"
chmod 755 "$bundle/openstitch-cli"

# Runtime libraries are system-provided: retain the build runner's dependency list.
ldd "$bundle/openstitch-cli" > "$bundle/dependencies.txt"
if grep -q 'not found' "$bundle/dependencies.txt"; then
  cat "$bundle/dependencies.txt" >&2
  echo 'Unresolved Linux runtime dependency; refusing to publish the CLI.' >&2
  exit 1
fi

cp LICENSE THIRD_PARTY_LICENSES.md "$bundle/"
cat > "$bundle/README.txt" <<'README'
OpenStitch CLI for Linux x86-64
================================
Built by GitHub Actions on Ubuntu 24.04 (glibc-based Linux).
Contains the command-line application only; no desktop graphical interface.

Usage after extraction:
  ./openstitch-cli --version
  ./openstitch-cli --help
  ./openstitch-cli project digitize picture.png -o design.osp --dpi 96

This is a dynamically linked binary. It is not guaranteed to run on every
Linux distribution (especially older glibc versions). Run 'ldd ./openstitch-cli'
to check for unavailable shared libraries; see dependencies.txt for the build
runner's dependency list. Compatibility with Debian and other distributions
must be checked on the destination environment.

Source and CLI documentation:
  https://github.com/ElZacho200a/OpenStitch
  https://github.com/ElZacho200a/OpenStitch/blob/main/docs/source/cli-project.md
README

archive="$out_dir/openstitch-cli-linux-x86_64.tar.gz"
tar -C "$stage" -czf "$archive" openstitch-cli-linux-x86_64
(cd "$out_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "Built $archive"
