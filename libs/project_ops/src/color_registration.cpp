// SPDX-License-Identifier: Apache-2.0
// Décalage entre couleurs et contre-compensation (solveur de la feuille tissu, par bloc de
// couleur). Estimation NON calibrée.
#include "openstitch/project_ops/color_registration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "openstitch/formats/dst.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_ops {

namespace {

constexpr const char* kWarning =
    "Estimation NON calibree : rigidites, tension du fil et seuil de decalage sont des ordres de "
    "grandeur empiriques, et le modele est quasi statique (pas de superposition des couches ni de "
    "plis). La compensation n'est fiable qu'apres calibration sur une piece-test cousue ; sans "
    "elle, compare avant/apres et garde la force basse.";

std::string hex_color(const std::array<std::uint8_t, 3>& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c[0], c[1], c[2]);
    return buf;
}

std::string fixed(double v, int digits = 2) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", digits, v);
    return buf;
}

} // namespace

Result<ColorRegistrationReport>
predict_color_registration(const document::Project& project,
                           const ColorRegistrationOptions& options) {
    if (!std::isfinite(options.cell_mm) || options.cell_mm < 0.2 || options.cell_mm > 10.0 ||
        options.max_cells_per_side < 8 || options.max_cells_per_side > 256 ||
        !std::isfinite(options.margin_mm) || options.margin_mm < 0.0 || options.margin_mm > 200.0 ||
        !std::isfinite(options.thread_tension_n) || !std::isfinite(options.visible_gap_mm) ||
        !std::isfinite(options.own_block_fraction) || !std::isfinite(options.strength) ||
        options.strength < 0.0 || options.strength > 1.0 || options.iterations < 0 ||
        options.iterations > 6 || !std::isfinite(options.heatmap_px_per_cell) ||
        options.heatmap_px_per_cell < 1.0 || options.heatmap_px_per_cell > 16.0) {
        return std::unexpected(
            Error{ErrorCategory::UserInput, "repérage des couleurs : parametres invalides", {}});
    }
    const bool any_visible =
        std::any_of(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                    [](const auto& e) { return e.visible; });
    if (!any_visible) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "repérage des couleurs : aucun objet visible a analyser",
                                     {}});
    }
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }

    const std::string material =
        options.material.empty() ? project.fabric.material : options.material;
    const std::string stabilizer =
        options.stabilizer.empty() ? project.fabric.stabilizer : options.stabilizer;
    stitch_analysis::SheetParams sheet = stitch_analysis::sheet_preset(material, stabilizer);
    sheet.grain_angle_rad = project.fabric.grain_angle.radians;
    sheet.stiffness_weft_n_per_mm /= std::clamp(project.fabric.weft_softness, 0.25, 4.0);
    if (options.thread_tension_n > 0.0) {
        sheet.thread_tension_n = options.thread_tension_n;
    }
    stitch_analysis::RegistrationParams reg;
    if (options.visible_gap_mm > 0.0) {
        reg.visible_gap_mm = options.visible_gap_mm;
    }
    if (options.own_block_fraction >= 0.0) {
        reg.own_block_fraction = std::min(1.0, options.own_block_fraction);
    }

    stitch_analysis::SheetDomain domain;
    if (options.hoop_is_canvas) {
        const auto hw = static_cast<std::int32_t>(project.canvas.width.value / 2);
        const auto hh = static_cast<std::int32_t>(project.canvas.height.value / 2);
        domain = {{{-hw}, {-hh}}, {{hw}, {hh}}};
    } else {
        domain = stitch_analysis::domain_around_stitches(
            *sequence, to_micrometers(Millimeters{options.margin_mm}));
    }
    const Micrometers cell = to_micrometers(Millimeters{options.cell_mm});
    if (domain.max.x.value <= domain.min.x.value || domain.max.y.value <= domain.min.y.value) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "repérage des couleurs : aucun segment cousu a analyser",
                                     {}});
    }

    ColorRegistrationReport report;
    report.registration = stitch_analysis::measure_registration(*sequence, sheet, reg, domain, cell,
                                                                options.max_cells_per_side);
    const stitch_analysis::SheetRegistration& r = report.registration;
    if (r.empty()) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "repérage des couleurs : aucun segment cousu a analyser",
                                     {}});
    }
    stitch_analysis::RegistrationCompensation comp;
    if (options.compensate) {
        comp = stitch_analysis::compensate_registration(*sequence, sheet, reg, domain, cell,
                                                        options.strength, options.iterations,
                                                        options.max_cells_per_side);
        report.compensated = comp.sequence;
        auto dst = formats::encode_dst(report.compensated);
        if (!dst) {
            return std::unexpected(dst.error());
        }
        report.compensated_dst = std::move(*dst);
    }

    const auto mm = [](Vec2um p) {
        return nlohmann::json{{"x", to_millimeters(p.x).value}, {"y", to_millimeters(p.y).value}};
    };
    const auto find_object = [&](ObjectId id) -> const document::EmbroideryObject* {
        for (const auto& o : project.embroidery_objects) {
            if (o.id == id) {
                return &o;
            }
        }
        return nullptr;
    };
    nlohmann::json blocks = nlohmann::json::array();
    std::string lines;
    for (std::size_t k = 0; k < r.blocks.size(); ++k) {
        const stitch_analysis::ColorBlock& b = r.blocks[k];
        const document::EmbroideryObject* obj = find_object(b.first_object);
        nlohmann::json j = {
            {"index", k + 1},
            {"first_command", b.first_command},
            {"end_command", b.end_command},
            {"stitches", b.stitch_count},
            {"mean_offset_mm", {{"x", b.mean_offset_x_mm}, {"y", b.mean_offset_y_mm}}},
            {"max_offset_mm", b.max_offset_mm}};
        std::string label = "bloc " + std::to_string(k + 1);
        if (obj != nullptr) {
            j["first_object"] = {
                {"id", obj->id.value}, {"name", obj->name}, {"rgb", hex_color(obj->rgb)}};
            label += " (" + hex_color(obj->rgb) + (obj->name.empty() ? "" : " " + obj->name) + ")";
        }
        blocks.push_back(std::move(j));
        if (k < 30) {
            lines += "    " + label + " : " + std::to_string(b.stitch_count) +
                     " points, ecart moyen (" + fixed(b.mean_offset_x_mm) + " ; " +
                     fixed(b.mean_offset_y_mm) + ") mm, max " + fixed(b.max_offset_mm) + " mm\n";
        }
    }
    if (r.blocks.size() > 30) {
        lines +=
            "    ... " + std::to_string(r.blocks.size() - 30) + " blocs de plus (voir le JSON)\n";
    }

    nlohmann::json doc = {{"schema", kColorRegistrationSchema},
                          {"method", "plane_stress_fem_q4_sequential_v1"},
                          {"calibrated", r.calibrated},
                          {"material", material},
                          {"stabilizer", stabilizer},
                          {"grid",
                           {{"columns", r.final_state.columns},
                            {"rows", r.final_state.rows},
                            {"cell_mm", to_millimeters(r.final_state.cell).value},
                            {"origin_mm", mm(r.final_state.origin)}}},
                          {"blocks", blocks},
                          {"block_count", r.blocks.size()},
                          {"visible_gap_mm", r.visible_gap_mm},
                          {"max_gap_mm", r.max_gap_mm},
                          {"max_gap_at_mm", mm(r.max_gap_at)},
                          {"gap_area_mm2", r.gap_area_mm2},
                          {"max_offset_mm", r.max_offset_mm},
                          {"warning", kWarning}};
    if (options.compensate) {
        doc["compensation"] = {{"strength", options.strength},
                               {"iterations", comp.iterations},
                               {"max_offset_before_mm", comp.max_offset_before_mm},
                               {"max_offset_after_mm", comp.max_offset_after_mm},
                               {"max_gap_before_mm", comp.max_gap_before_mm},
                               {"max_gap_after_mm", comp.max_gap_after_mm}};
    }
    report.json = doc.dump(2);

    report.text = std::string("Decalage prevu entre couleurs (estimation NON calibree)\n") +
                  "  tissu : " + material + (stabilizer.empty() ? "" : " + " + stabilizer) + "\n" +
                  "  blocs de couleur : " + std::to_string(r.blocks.size()) + "\n" +
                  "  decalage max entre deux couleurs voisines : " + fixed(r.max_gap_mm) +
                  " mm (visible a partir de " + fixed(r.visible_gap_mm) + " mm)\n" +
                  "  surface decalee de facon visible : " + fixed(r.gap_area_mm2, 0) + " mm2\n" +
                  "  ecart max d'un point a sa position nominale : " + fixed(r.max_offset_mm) +
                  " mm\n" + "  par bloc (ordre de couture) :\n" + lines;
    if (r.blocks.size() < 2) {
        report.text += "  Un seul bloc de couleur : aucun decalage entre couleurs a prevoir.\n";
    }
    if (options.compensate) {
        report.text += "  compensation (force " + fixed(options.strength) + ", " +
                       std::to_string(comp.iterations) + " iterations) : ecart max " +
                       fixed(comp.max_offset_before_mm) + " -> " + fixed(comp.max_offset_after_mm) +
                       " mm ; decalage entre couleurs " + fixed(comp.max_gap_before_mm) + " -> " +
                       fixed(comp.max_gap_after_mm) + " mm (prevu par le modele)\n";
    }
    report.text +=
        "  Attention : ordres de grandeur empiriques ; verifie sur une piece-test cousue.\n";

    if (options.heatmap) {
        // Échelle : le seuil visible, ou le pire décalage s'il le dépasse (rouge = pire endroit).
        const stitch_analysis::SheetRaster raster = stitch_analysis::rasterize_registration_gap(
            r, std::max(r.visible_gap_mm, r.max_gap_mm));
        const int scale = static_cast<int>(std::lround(options.heatmap_px_per_cell));
        image::Image map;
        map.width = raster.width * scale;
        map.height = raster.height * scale;
        map.rgba.assign(static_cast<std::size_t>(map.width) * map.height * 4, 255);
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const std::uint8_t* src =
                    raster.rgba.data() +
                    (static_cast<std::size_t>(y / scale) * raster.width + x / scale) * 4;
                std::uint8_t* dst =
                    map.rgba.data() + (static_cast<std::size_t>(y) * map.width + x) * 4;
                const double a = src[3] / 255.0;
                for (int ch = 0; ch < 3; ++ch) {
                    dst[ch] = static_cast<std::uint8_t>(235.0 * (1.0 - a) + src[ch] * a);
                }
            }
        }
        auto png = image::encode_png(map);
        if (!png) {
            return std::unexpected(png.error());
        }
        report.heatmap_png = std::move(*png);
    }
    return report;
}

} // namespace openstitch::project_ops
