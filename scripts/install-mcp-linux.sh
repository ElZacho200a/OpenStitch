#!/usr/bin/env bash
# Install the official OpenStitch Linux MCP server from the persistent GitHub release.
# Fallback: a successful, trusted CI push to main (never arbitrary PR artifacts).
set -euo pipefail

REPO="ElZacho200a/OpenStitch"
ARCHIVE="openstitch-mcp-linux-x86_64.tar.gz"
ARTIFACT="openstitch-mcp-linux-x86_64"
RELEASE_URL="https://github.com/$REPO/releases/download/latest"
DEST="${HOME}/.local/bin"

usage() {
  cat <<'HELP'
Usage: bash scripts/install-cli-linux.sh [--install-dir DIRECTORY]

Downloads, checks SHA-256 and installs the OpenStitch Linux x86-64 MCP.
Default: ~/.local/bin/openstitch-mcp (no sudo).

Primary: persistent GitHub release 'latest'.
Fallback: successful main-branch push CI artifact via authenticated gh MCP.
Run 'gh auth login' if required. If both are unavailable, build from source.
HELP
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install-dir)
      if [[ $# -lt 2 || -z "$2" ]]; then echo 'Missing argument for --install-dir' >&2; exit 2; fi
      DEST="$2"
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo 'Only Linux x86-64 with glibc is supported. Build from source elsewhere.' >&2
  exit 1
fi
for cmd in tar sha256sum mktemp install; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command: $cmd" >&2
    exit 1
  fi
done

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

verify_archive() {
  (cd "$workdir" && sha256sum -c "$ARCHIVE.sha256" >/dev/null)
}

fetch_release() {
  if ! command -v curl >/dev/null 2>&1; then
    echo 'curl not installed; trying authenticated GitHub MCP fallback.' >&2
    return 1
  fi
  echo "Checking release latest ($REPO)..." >&2
  if ! curl --fail --location --silent --show-error --retry 2 \
    "$RELEASE_URL/$ARCHIVE" -o "$workdir/$ARCHIVE"; then
    return 1
  fi
  if ! curl --fail --location --silent --show-error --retry 2 \
    "$RELEASE_URL/$ARCHIVE.sha256" -o "$workdir/$ARCHIVE.sha256"; then
    return 1
  fi
  # Never downgrade integrity by using a fallback after a checksum failure.
  verify_archive || { echo 'SHA-256 mismatch in release; refusing installation.' >&2; exit 1; }
  return 0
}

fetch_main_ci_artifact() {
  if ! command -v gh >/dev/null 2>&1; then
    echo 'GitHub MCP (gh) not installed; no Actions fallback available.' >&2
    return 1
  fi
  if ! gh auth status >/dev/null 2>&1; then
    echo 'GitHub MCP not authenticated; run gh auth login for Actions fallback.' >&2
    return 1
  fi
  echo 'Release absent: searching successful CI runs from main push events only...' >&2
  local runs run_id
  runs=$(gh run list --repo "$REPO" --workflow ci.yml --branch main --event push \
    --limit 30 --json databaseId,conclusion \
    --jq '.[] | select(.conclusion == "success") | .databaseId') || return 1
  while IFS= read -r run_id; do
    [[ "$run_id" =~ ^[0-9]+$ ]] || continue
    rm -rf "$workdir/actions"
    mkdir -p "$workdir/actions"
    if gh run download "$run_id" --repo "$REPO" --name "$ARTIFACT" \
      --dir "$workdir/actions" >/dev/null 2>&1; then
      if [[ -f "$workdir/actions/$ARCHIVE" && -f "$workdir/actions/$ARCHIVE.sha256" ]]; then
        mv "$workdir/actions/$ARCHIVE" "$workdir/$ARCHIVE"
        mv "$workdir/actions/$ARCHIVE.sha256" "$workdir/$ARCHIVE.sha256"
        verify_archive || { echo 'SHA-256 mismatch in CI artifact; refusing installation.' >&2; exit 1; }
        echo "Using trusted main CI run $run_id." >&2
        return 0
      fi
    fi
  done <<< "$runs"
  return 1
}

if ! fetch_release; then
  rm -f "$workdir/$ARCHIVE" "$workdir/$ARCHIVE.sha256"
  if ! fetch_main_ci_artifact; then
    echo 'No verified Linux MCP available. Check Releases or build from source.' >&2
    echo "Releases: https://github.com/$REPO/releases/tag/latest" >&2
    echo "Build: https://github.com/$REPO/blob/main/docs/source/installation.md" >&2
    exit 1
  fi
fi

# Extract in isolation, not into the destination. Reject unexpected paths.
mkdir -p "$workdir/unpack"
member="$ARTIFACT/openstitch-mcp"
if ! tar -tzf "$workdir/$ARCHIVE" | grep -Fxq "$member"; then
  echo "Archive does not contain $member" >&2; exit 1
fi
tar -xzf "$workdir/$ARCHIVE" -C "$workdir/unpack" --no-same-owner
binary="$workdir/unpack/$member"
if [[ ! -f "$binary" || -L "$binary" ]]; then
  echo 'Invalid or missing OpenStitch executable in archive.' >&2; exit 1
fi
chmod u+x "$binary"
if command -v ldd >/dev/null 2>&1; then
  dependencies=$(ldd "$binary" 2>&1) || {
    echo "Unable to inspect Linux dependencies: $dependencies" >&2; exit 1;
  }
  if grep -q 'not found' <<< "$dependencies"; then
    echo "$dependencies" >&2
    echo 'Missing runtime libraries. Install them or build from source.' >&2
    exit 1
  fi
fi
# MCP speaks JSON-RPC over stdio; verify initialize handshake instead of CLI flags.
response=$(printf '%s\\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"installer-smoke","version":"1"}}}' | timeout 10 "$binary" 2>/dev/null) || { echo 'MCP initialize failed' >&2; exit 1; }
grep -q '"result"' <<< "$response" || { echo 'MCP initialize returned no result' >&2; exit 1; }
mkdir -p "$DEST"
install -m 0755 "$binary" "$DEST/.openstitch-mcp.new.$$"
mv -f "$DEST/.openstitch-mcp.new.$$" "$DEST/openstitch-mcp"
echo "Installed: $DEST/openstitch-mcp" >&2
echo "MCP executable ready: $DEST/openstitch-mcp"
