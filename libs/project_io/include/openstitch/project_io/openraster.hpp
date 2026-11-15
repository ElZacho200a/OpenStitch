// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/image/image.hpp"

namespace openstitch::project_io {

struct OpenRasterLayer {
    std::string name;
    image::Image image;
    bool visible{true};
};

// Calques raster du fond vers le dessus, alignés sur la toile de l'archive.
[[nodiscard]] Result<std::vector<OpenRasterLayer>>
load_openraster(const std::filesystem::path& path);

} // namespace openstitch::project_io
