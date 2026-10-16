// SPDX-License-Identifier: Apache-2.0
#include "debug_dump.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "thread_model.hpp"

namespace openstitch::project_ops::detail {

namespace {

using nlohmann::json;

Result<void> write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return std::unexpected(Error{
            ErrorCategory::OperationImpossible, "écriture impossible : " + path.string(), {}});
    }
    return {};
}

Result<void> write_png(const std::filesystem::path& path, const image::Image& img) {
    auto png = image::encode_png(img);
    if (!png) {
        return std::unexpected(png.error());
    }
    return write_file(path, *png);
}

void set_px(image::Image& img, std::size_t i, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    img.rgba[i * 4] = r;
    img.rgba[i * 4 + 1] = g;
    img.rgba[i * 4 + 2] = b;
    img.rgba[i * 4 + 3] = 255;
}

} // namespace

Result<void> write_debug_dir(const std::filesystem::path& dir, const document::Project& project,
                             const DigitizeReport& report, const DigitizeSettings& settings,
                             const autodigitize::LineLayerMasks& masks) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "dossier de diagnostic impossible : " + dir.string(),
                                     {}});
    }
    const image::Image& src = project.original;
    if (src.empty()) {
        return {};
    }
    const std::size_t n = static_cast<std::size_t>(src.width) * src.height;

    // Régions de la segmentation.
    if (project.segmentation && project.segmentation->labels.size() == n) {
        image::Image regions;
        regions.width = src.width;
        regions.height = src.height;
        regions.rgba.assign(n * 4, 255);
        const auto& seg = *project.segmentation;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t l = seg.labels[i];
            if (l != 0 && l <= seg.region_slots.size() && seg.region_slots[l - 1]) {
                const auto& c = seg.region_slots[l - 1]->rgb;
                set_px(regions, i, c[0], c[1], c[2]);
            } else {
                set_px(regions, i, 240, 240, 240);
            }
        }
        if (auto w = write_png(dir / "regions.png", regions); !w) {
            return w;
        }
    }

    // Décisions de la couche de traits sur la source atténuée.
    if (masks.width == src.width && masks.height == src.height) {
        image::Image traits;
        traits.width = src.width;
        traits.height = src.height;
        traits.rgba.assign(n * 4, 255);
        const auto on = [&](const std::vector<char>& m, std::size_t i) {
            return i < m.size() && m[i] != 0;
        };
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t* p = src.rgba.data() + i * 4;
            auto dim = [](std::uint8_t v) {
                return static_cast<std::uint8_t>(128 + v / 2); // 50 % de blanc
            };
            set_px(traits, i, dim(p[0]), dim(p[1]), dim(p[2]));
            if (on(masks.stitched, i) && !on(masks.bars, i)) {
                set_px(traits, i, 220, 30, 30);
            } else if (on(masks.bars, i)) {
                set_px(traits, i, 30, 60, 220);
            } else if (on(masks.short_frag, i)) {
                set_px(traits, i, 255, 150, 0);
            } else if (on(masks.separation, i) && !on(masks.stitched, i)) {
                set_px(traits, i, 30, 170, 60);
            }
        }
        if (auto w = write_png(dir / "traits.png", traits); !w) {
            return w;
        }
    }

    // Couverture réelle des fils et carte d'erreurs (si des points sont cousus).
    if (auto model = build_thread_model(project)) {
        const auto raster =
            rasterize_threads(*model, src.width, src.height, project.mm_per_px.value, 0.55);
        image::Image cover;
        cover.width = src.width;
        cover.height = src.height;
        cover.rgba.assign(n * 4, 255);
        for (std::size_t i = 0; i < n; ++i) {
            if (raster.covered[i] != 0) {
                const auto& c = model->palette[raster.colour[i] - 1];
                set_px(cover, i, c[0], c[1], c[2]);
            } else {
                set_px(cover, i, 0, 255, 0);
            }
        }
        if (auto w = write_png(dir / "couverture.png", cover); !w) {
            return w;
        }
    }
    FidelityOptions fo;
    fo.error_map = true;
    if (auto fid = measure_fidelity(project, fo); fid && !fid->error_map_png.empty()) {
        if (auto w = write_file(dir / "fidelite.png", fid->error_map_png); !w) {
            return w;
        }
    }

    json d;
    d["image"] = {
        {"width_px", src.width}, {"height_px", src.height}, {"mm_per_px", project.mm_per_px.value}};
    d["regions"] = report.regions;
    d["background_skipped"] = report.background_skipped;
    d["background_auto"] = report.background_auto;
    if (report.background_candidate) {
        d["background_candidate"] = {{"rgb", rgb_to_hex(report.background_candidate->rgb)}};
    }
    d["objects"] = {{"embroidery", report.embroidery_objects},
                    {"satin", report.satin},
                    {"tatami", report.tatami},
                    {"running", report.running}};
    if (report.dark_lines) {
        const auto& m = *report.dark_lines;
        d["dark_lines"] = {{"effective_lightness", m.line_lightness},
                           {"max_lightness_setting", settings.dark_lines_lightness},
                           {"thread_rgb", rgb_to_hex(m.line_rgb)},
                           {"line_pixels", m.line_pixels},
                           {"reassigned_pixels", m.reassigned_pixels},
                           {"separation_pixels", m.separation_pixels},
                           {"short_fragment_pixels", m.short_fragment_pixels},
                           {"bar_pixels", m.bar_pixels},
                           {"bar_objects", m.bar_objects},
                           {"line_objects", m.line_objects}};
    }
    d["warnings"] = report.warnings;
    std::ofstream out(dir / "decisions.json", std::ios::binary | std::ios::trunc);
    out << d.dump(2) << "\n";
    if (!out) {
        return std::unexpected(
            Error{ErrorCategory::OperationImpossible, "écriture impossible : decisions.json", {}});
    }
    return {};
}

} // namespace openstitch::project_ops::detail
