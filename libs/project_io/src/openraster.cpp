// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/openraster.hpp"

#include <algorithm>
#include <string_view>

#include <pugixml.hpp>

#include "archive.hpp"
#include "openstitch/core/input_limits.hpp"

namespace openstitch::project_io {
namespace {

struct OraReader {
    const std::map<std::string, detail::Blob>& files;
    int width;
    int height;
    std::vector<OpenRasterLayer> layers;

    Result<void> readStack(pugi::xml_node stack, bool visible, int depth) {
        if (depth > 64) {
            return fail(ErrorCategory::InvalidFile, "Trop de groupes OpenRaster imbriqués");
        }
        for (auto node = stack.last_child(); node; node = node.previous_sibling()) {
            if (node.type() != pugi::node_element) {
                continue;
            }
            const std::string_view kind = node.name();
            const bool shown = visible &&
                               std::string_view(node.attribute("visibility").value()) != "hidden" &&
                               node.attribute("opacity").as_double(1.0) > 0;
            auto read = kind == "stack" ? readStack(node, shown, depth + 1)
                        : kind == "layer"
                            ? readLayer(node, shown)
                            : fail(ErrorCategory::UnsupportedFormat,
                                   "Élément OpenRaster non raster : " + std::string(kind));
            if (!read) {
                return read;
            }
        }
        return {};
    }

    Result<void> readLayer(pugi::xml_node node, bool visible) {
        const std::string path = node.attribute("src").value();
        const auto file = files.find(path);
        if (file == files.end()) {
            return fail(ErrorCategory::InvalidFile, "Image du calque OpenRaster introuvable", path);
        }
        const auto canvasBytes = static_cast<std::int64_t>(width) * height * 4;
        if (static_cast<std::int64_t>(layers.size() + 1) >
            limits::kMaxZipTotalBytes / canvasBytes) {
            return fail(ErrorCategory::InvalidFile,
                        "Les calques OpenRaster occupent trop de mémoire");
        }
        constexpr std::uint8_t pngSignature[]{137, 80, 78, 71, 13, 10, 26, 10};
        if (file->second.size() < sizeof(pngSignature) ||
            !std::equal(std::begin(pngSignature), std::end(pngSignature), file->second.begin())) {
            return fail(ErrorCategory::UnsupportedFormat, "Calque OpenRaster attendu au format PNG",
                        path);
        }
        auto decoded = image::decode_image(file->second);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }

        const auto x = static_cast<std::int64_t>(node.attribute("x").as_int());
        const auto y = static_cast<std::int64_t>(node.attribute("y").as_int());
        if (x != 0 || y != 0 || width != decoded->width || height != decoded->height) {
            image::Image aligned{width, height, true,
                                 std::vector<std::uint8_t>(static_cast<std::size_t>(canvasBytes))};
            const auto left = std::max<std::int64_t>(0, x);
            const auto top = std::max<std::int64_t>(0, y);
            const auto right = std::min<std::int64_t>(width, x + decoded->width);
            const auto bottom = std::min<std::int64_t>(height, y + decoded->height);
            if (left < right) {
                for (auto row = top; row < bottom; ++row) {
                    const auto source = ((row - y) * decoded->width + left - x) * 4;
                    const auto target = (row * width + left) * 4;
                    std::copy_n(decoded->rgba.data() + source, (right - left) * 4,
                                aligned.rgba.data() + target);
                }
            }
            *decoded = std::move(aligned);
        }
        const std::string name = node.attribute("name").value();
        layers.push_back({name.empty() ? path : name, std::move(*decoded), visible});
        return {};
    }
};

} // namespace

Result<std::vector<OpenRasterLayer>> load_openraster(const std::filesystem::path& path) {
    auto files = detail::read_zip(path);
    if (!files) {
        return std::unexpected(files.error());
    }
    const auto mimetype = files->find("mimetype");
    const auto stackFile = files->find("stack.xml");
    constexpr std::string_view oraType = "image/openraster";
    if (mimetype == files->end() || !std::ranges::equal(mimetype->second, oraType) ||
        stackFile == files->end()) {
        return fail(ErrorCategory::InvalidFile,
                    "Archive OpenRaster invalide (mimetype ou stack.xml)");
    }
    pugi::xml_document xml;
    const auto& bytes = stackFile->second;
    if (!xml.load_buffer(bytes.data(), bytes.size())) {
        return fail(ErrorCategory::InvalidFile, "Pile de calques OpenRaster illisible");
    }
    const auto root = xml.child("image");
    const auto stack = root.child("stack");
    const int width = root.attribute("w").as_int();
    const int height = root.attribute("h").as_int();
    if (!stack || width <= 0 || height <= 0 ||
        static_cast<std::int64_t>(width) * height > limits::kMaxImagePixels) {
        return fail(ErrorCategory::InvalidFile,
                    "Dimensions ou pile de calques OpenRaster invalides");
    }
    OraReader reader{*files, width, height, {}};
    if (auto read = reader.readStack(stack, true, 0); !read) {
        return std::unexpected(read.error());
    }
    if (reader.layers.empty()) {
        return fail(ErrorCategory::InvalidFile, "L'archive OpenRaster ne contient aucun calque");
    }
    return std::move(reader.layers);
}

} // namespace openstitch::project_io
