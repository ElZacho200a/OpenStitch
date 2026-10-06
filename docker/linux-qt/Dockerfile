# Image de développement Linux : cœur + CLI + desktop Qt, tests QTest en headless.
# But : un environnement reproductible pour la CI rapide et les agents (pas pour livrer
# l'application, qui reste un binaire Windows MSVC — voir docker/windows/).
# Qt vient d'apt (6.4) : suffisant pour compiler et tester, mais ce n'est pas le Qt 6.8.3
# de la CI Windows ; tout écart de comportement se vérifie sur la CI Windows.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates clang-format \
        libopencv-dev catch2 libfmt-dev libspdlog-dev libcli11-dev \
        nlohmann-json3-dev libpugixml-dev zlib1g-dev libzstd-dev liblzma-dev libbz2-dev \
        qt6-base-dev qt6-base-dev-tools qt6-svg-dev qt6-image-formats-plugins \
        libgl1-mesa-dev libxkbcommon-dev \
    && rm -rf /var/lib/apt/lists/*

# Clipper2 et minizip-ng ne sont pas dans apt : compilés en statique à des commits figés
# (ceux validés le 2026-10-06).
ARG CLIPPER2_REV=f9c5eb6e14a59f6f5d65fbfb3564519a561cf4fd
ARG MINIZIP_REV=72d3b398c420f01f1351b1fc9ddb8fc53eb7faf4
ENV DEPS_PREFIX=/opt/deps
RUN git clone https://github.com/AngusJohnson/Clipper2.git /tmp/Clipper2 \
    && git -C /tmp/Clipper2 checkout ${CLIPPER2_REV} \
    && cmake -S /tmp/Clipper2/CPP -B /tmp/b-clip -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCLIPPER2_EXAMPLES=OFF -DCLIPPER2_TESTS=OFF -DCLIPPER2_UTILS=OFF \
        -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX} \
    && cmake --build /tmp/b-clip -j"$(nproc)" && cmake --install /tmp/b-clip \
    && git clone https://github.com/zlib-ng/minizip-ng.git /tmp/minizip-ng \
    && git -C /tmp/minizip-ng checkout ${MINIZIP_REV} \
    && cmake -S /tmp/minizip-ng -B /tmp/b-mz -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DMZ_BUILD_TESTS=OFF -DMZ_BUILD_UNIT_TESTS=OFF -DMZ_FETCH_LIBS=OFF \
        -DMZ_OPENSSL=OFF -DMZ_ICONV=OFF -DMZ_LZMA=OFF \
        -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX} \
    && cmake --build /tmp/b-mz -j"$(nproc)" && cmake --install /tmp/b-mz \
    && rm -rf /tmp/Clipper2 /tmp/minizip-ng /tmp/b-clip /tmp/b-mz

# Les tests desktop tournent sans écran (aucune comparaison de pixels, cf. CLAUDE.md).
ENV QT_QPA_PLATFORM=offscreen

WORKDIR /src
# Le dépôt est monté dans /src : docker run --rm -v "$PWD":/src openstitch-linux-qt
CMD ["bash", "-lc", "cmake -S . -B build/docker -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPENSTITCH_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH=${DEPS_PREFIX} \
  && cmake --build build/docker -j\"$(nproc)\" \
  && ctest --test-dir build/docker --output-on-failure -j\"$(nproc)\""]
