// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::image {

struct ImageInfo {
    int width_px{0};
    int height_px{0};
    int channels{0};       // tel que stocké dans le fichier (1, 2, 3 ou 4)
    bool has_alpha{false};
    std::string format;    // "PNG", "JPEG", "BMP", "TIFF", … (déduit de l'extension)
};

// Image de travail normalisée : RGBA 8 bits, ligne par ligne, indépendante
// de la bibliothèque de chargement. C'est le seul type d'image qui circule
// entre les modules.
struct Image {
    int width{0};
    int height{0};
    bool source_had_alpha{false};
    std::vector<std::uint8_t> rgba;  // width * height * 4 octets, ordre R,G,B,A

    [[nodiscard]] bool empty() const { return rgba.empty(); }
};

[[nodiscard]] Result<ImageInfo> read_image_info(const std::filesystem::path& path);
[[nodiscard]] Result<Image> load_image(const std::filesystem::path& path);

// Encode/décode en mémoire (format projet : image intégrée en PNG).
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_png(const Image& image);
[[nodiscard]] Result<Image> decode_image(std::span<const std::uint8_t> bytes);

}  // namespace openstitch::image
