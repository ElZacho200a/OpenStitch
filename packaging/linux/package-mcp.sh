#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then echo 'Usage: package-mcp.sh BINARY OUTPUT_DIR' >&2; exit 2; fi
binary=$1
out_dir=$2
[[ -f "$binary" && -x "$binary" ]] || { echo 'Missing MCP executable' >&2; exit 1; }
mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
bundle="$stage/openstitch-mcp-linux-x86_64"
mkdir -p "$bundle"
cp "$binary" "$bundle/openstitch-mcp"
chmod 755 "$bundle/openstitch-mcp"
ldd "$bundle/openstitch-mcp" > "$bundle/dependencies.txt"
if grep -q 'not found' "$bundle/dependencies.txt"; then cat "$bundle/dependencies.txt" >&2; exit 1; fi
cp LICENSE THIRD_PARTY_LICENSES.md "$bundle/"
cat > "$bundle/README.txt" <<'EOF'
OpenStitch MCP server for Linux x86-64, built on Ubuntu 24.04.
Uses JSON-RPC 2.0 over stdio. Configure your MCP client to launch
the extracted openstitch-mcp executable; do not expose network ports.
Requires compatible glibc and shared libraries (see dependencies.txt).
Documentation: https://github.com/ElZacho200a/OpenStitch/blob/main/docs/source/mcp.md
EOF
archive="$out_dir/openstitch-mcp-linux-x86_64.tar.gz"
tar -C "$stage" -czf "$archive" openstitch-mcp-linux-x86_64
(cd "$out_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "Built $archive"
