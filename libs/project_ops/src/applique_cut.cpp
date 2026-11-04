// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/applique_cut.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "openstitch/formats/cut_svg.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/stitch_generation/applique.hpp"

namespace openstitch::project_ops {

Result<AppliqueCutReport> export_applique_cut(const document::Project& project,
                                              const std::filesystem::path& path,
                                              std::optional<ObjectId> only) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".svg" && ext != ".dxf") {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Format de découpe inconnu : « " + ext + " » (attendu : .svg ou .dxf)");
    }
    AppliqueCutReport report;
    std::vector<geometry::Path> cut;
    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        const auto* params = std::get_if<document::AppliqueParams>(&object.params);
        if (params == nullptr || !object.visible || (only && object.id != *only)) {
            continue;
        }
        const document::VectorObject* source = project.findObject(object.source_vector);
        if (source == nullptr) {
            continue;
        }
        const auto rings = stitch_generation::applique_cut_paths(source->paths, *params);
        if (rings.empty()) {
            continue;
        }
        cut.insert(cut.end(), rings.begin(), rings.end());
        ++report.objects;
    }
    if (report.objects == 0) {
        return fail(ErrorCategory::UserInput, "Aucun objet appliqué à découper");
    }
    report.paths = cut.size();
    const auto written =
        ext == ".svg" ? formats::write_cut_svg_file(path, cut) : formats::write_dxf_file(path, cut);
    if (!written) {
        return std::unexpected(written.error());
    }
    return report;
}

} // namespace openstitch::project_ops
