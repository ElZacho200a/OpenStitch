#!/usr/bin/env bash
# Compile le CLI OpenStitch (sans Qt) sous Linux SANS vcpkg : dépendances système via apt, Clipper2 et
# minizip-ng depuis leurs dépôts git, cale CMake pour Boost.Polygon. Pour les environnements où le
# téléchargement d'archives GitHub (utilisé par vcpkg) est bloqué, ou sans vcpkg (agent, conteneur).
#
#   scripts/linux-core-setup.sh [--prefix DIR] [--build DIR] [--jobs N] [--no-apt] [--tests]
#
# Défauts : --prefix $HOME/.cache/openstitch-deps  --build build/linux-apt  --jobs $(nproc)
# Idempotent : les étapes déjà faites sont sautées. Résultat : <build>/apps/cli/openstitch-cli.
# Vérifié sur Ubuntu 24.04 (g++ 13, OpenCV 4.6, fmt 9, Boost 1.83). Les versions d'apt diffèrent de
# celles de vcpkg : les mesures sont identiques, mais le test `cli_help` échoue (CLI11 2.4 écrit
# « Positionals », CLI11 2.6 « POSITIONALS »).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${OPENSTITCH_DEPS_PREFIX:-$HOME/.cache/openstitch-deps}
build=$root/build/linux-apt
jobs=$(nproc 2>/dev/null || echo 2)
use_apt=1
run_tests=0
while [ $# -gt 0 ]; do
    case $1 in
        --prefix) prefix=$2; shift 2 ;;
        --build) build=$2; shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --no-apt) use_apt=0; shift ;;
        --tests) run_tests=1; shift ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "option inconnue : $1" >&2; exit 1 ;;
    esac
done
mkdir -p "$prefix"
sudo_cmd=""
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null && sudo_cmd=sudo

if [ "$use_apt" -eq 1 ]; then
    echo "== paquets système"
    $sudo_cmd apt-get update -qq
    DEBIAN_FRONTEND=noninteractive $sudo_cmd apt-get install -y -qq \
        build-essential cmake ninja-build git pkg-config python3 \
        libopencv-dev libspdlog-dev libfmt-dev nlohmann-json3-dev libcli11-dev libpugixml-dev \
        catch2 libfreetype-dev zlib1g-dev libzstd-dev liblzma-dev libbz2-dev libssl-dev libboost-dev
fi

# Clipper2 et minizip-ng ne sont pas dans apt : clone git (pas d'archive) puis installation locale.
src=$prefix/src
mkdir -p "$src"
if [ ! -f "$prefix/lib/cmake/clipper2/Clipper2Config.cmake" ] && [ ! -d "$prefix/lib/cmake/clipper2" ]; then
    echo "== Clipper2"
    [ -d "$src/Clipper2" ] || git clone -q --depth 1 --branch Clipper2_2.0.1 \
        https://github.com/AngusJohnson/Clipper2 "$src/Clipper2"
    cmake -S "$src/Clipper2/CPP" -B "$src/clipper2-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCLIPPER2_UTILS=OFF -DCLIPPER2_EXAMPLES=OFF \
        -DCLIPPER2_TESTS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON > /dev/null
    cmake --build "$src/clipper2-build" -j "$jobs" > /dev/null
    cmake --install "$src/clipper2-build" > /dev/null
fi
if [ ! -d "$prefix/lib/cmake/minizip-ng" ]; then
    echo "== minizip-ng"
    [ -d "$src/minizip-ng" ] || git clone -q --depth 1 --branch 4.1.0 \
        https://github.com/zlib-ng/minizip-ng "$src/minizip-ng"
    cmake -S "$src/minizip-ng" -B "$src/minizip-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DMZ_COMPAT=OFF -DMZ_FETCH_LIBS=OFF -DMZ_LIBCOMP=OFF \
        -DMZ_ICONV=OFF -DMZ_OPENSSL=OFF -DMZ_BUILD_TESTS=OFF > /dev/null
    cmake --build "$src/minizip-build" -j "$jobs" > /dev/null
    cmake --install "$src/minizip-build" > /dev/null
fi

# Boost.Polygon : apt fournit les en-têtes mais pas le fichier de configuration CMake par composant
# que vcpkg installe (`find_package(boost_polygon CONFIG)` dans libs/auto_satin).
shim=$prefix/lib/cmake/boost_polygon
if [ ! -f "$shim/boost_polygon-config.cmake" ]; then
    echo "== cale boost_polygon"
    mkdir -p "$shim"
    cat > "$shim/boost_polygon-config.cmake" <<'CMAKE'
if(NOT TARGET Boost::polygon)
  add_library(Boost::polygon INTERFACE IMPORTED)
  set_target_properties(Boost::polygon PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "/usr/include")
endif()
set(boost_polygon_FOUND TRUE)
CMAKE
fi

echo "== configuration et compilation (cible openstitch-cli)"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENSTITCH_BUILD_DESKTOP=OFF \
    -DCMAKE_PREFIX_PATH="$prefix" > /dev/null
cmake --build "$build" --target openstitch-cli -j "$jobs"
echo "CLI : $build/apps/cli/openstitch-cli"

if [ "$run_tests" -eq 1 ]; then
    echo "== tests (en série ; cli_help échoue avec le CLI11 d'apt)"
    cmake --build "$build" -j "$jobs"
    (cd "$build" && ctest --output-on-failure) || true
fi
