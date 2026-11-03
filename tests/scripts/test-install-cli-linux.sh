#!/usr/bin/env bash
# Network-free smoke tests for the Linux CLI installer.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
installer="$repo_root/scripts/install-cli-linux.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/mockbin" "$tmp/assets" "$tmp/package/openstitch-cli-linux-x86_64"

# System ELF supports --version/--help; no source build needed in smoke tests.
cp /usr/bin/env "$tmp/package/openstitch-cli-linux-x86_64/openstitch-cli"
tar -C "$tmp/package" -czf "$tmp/assets/openstitch-cli-linux-x86_64.tar.gz" openstitch-cli-linux-x86_64
(cd "$tmp/assets" && sha256sum openstitch-cli-linux-x86_64.tar.gz > openstitch-cli-linux-x86_64.tar.gz.sha256)

cat > "$tmp/mockbin/curl" <<'MOCK_CURL'
#!/usr/bin/env bash
set -euo pipefail
url='' output=''
while (($#)); do
  case "$1" in
    -o) output=$2; shift 2 ;;
    http*) url=$1; shift ;;
    *) shift ;;
  esac
done
if [[ "${MOCK_CURL_DOWN:-0}" == 1 ]]; then exit 22; fi
cp "$MOCK_ASSET_DIR/$(basename "$url")" "$output"
MOCK_CURL

cat > "$tmp/mockbin/gh" <<'MOCK_GH'
#!/usr/bin/env bash
set -euo pipefail
case "$1 $2" in
  'auth status') exit "${MOCK_GH_AUTH_FAIL:-0}" ;;
  'run list') echo 123456; exit 0 ;;
  'run download')
    shift 2
    while (($#)); do
      case "$1" in
        --dir) dest=$2; shift 2 ;;
        *) shift ;;
      esac
    done
    cp "$MOCK_ASSET_DIR/openstitch-cli-linux-x86_64.tar.gz" "$dest/"
    cp "$MOCK_ASSET_DIR/openstitch-cli-linux-x86_64.tar.gz.sha256" "$dest/"
    ;;
  *) exit 1 ;;
esac
MOCK_GH
chmod +x "$tmp/mockbin/curl" "$tmp/mockbin/gh"

export MOCK_ASSET_DIR="$tmp/assets"
export PATH="$tmp/mockbin:$PATH"

# Release download, checksum, executable and installation.
bash "$installer" --install-dir "$tmp/bin" > "$tmp/success.out"
test -x "$tmp/bin/openstitch-cli"
"$tmp/bin/openstitch-cli" --version >/dev/null

# Missing release => authenticated trusted main CI artifact.
MOCK_CURL_DOWN=1 bash "$installer" --install-dir "$tmp/fallback" > "$tmp/fallback.out"
test -x "$tmp/fallback/openstitch-cli"

# Mismatch must fail without installation or fallback to a weaker source.
printf '\ncorruption\n' >> "$tmp/assets/openstitch-cli-linux-x86_64.tar.gz"
if bash "$installer" --install-dir "$tmp/rejected" > /dev/null 2> "$tmp/checksum-error"; then
  echo 'FAIL: corrupt archive installed successfully' >&2
  exit 1
fi
grep -q 'SHA-256 mismatch' "$tmp/checksum-error"
test ! -e "$tmp/rejected/openstitch-cli"

echo 'Installer smoke tests passed (release, CI fallback, checksum rejection).'
