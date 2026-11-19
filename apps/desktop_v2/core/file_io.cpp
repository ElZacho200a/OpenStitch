// SPDX-License-Identifier: Apache-2.0
#include "file_io.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>

#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/insert_design_command.hpp"
#include "openstitch/document/imported_design.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/format_registry.hpp"
#include <fstream>

#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/thread_palette/thread_library.hpp"

namespace openstitch::desktop_v2 {

std::vector<FileFilter> readableMachineFormats() {
    std::vector<FileFilter> each;
    FileFilter all{"Broderies machine", {}};
    for (const auto& f : formats::registered_formats()) {
        if (!f.can_read || f.decode == nullptr) {
            continue;
        }
        FileFilter one{f.display_name, {}};
        for (const auto& e : f.extensions) {
            one.patterns.push_back("*." + e);
            all.patterns.push_back("*." + e);
        }
        each.push_back(std::move(one));
    }
    each.insert(each.begin(), std::move(all));
    return each;
}

bool isMachineDesignFile(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto* format = formats::find_format_for_extension(ext);
    return format != nullptr && format->can_read && format->decode != nullptr;
}

Result<document::Project> loadMachineProject(const std::filesystem::path& file) {
    auto imported = project_io::import_machine_file(file);
    if (!imported) {
        return std::unexpected(imported.error());
    }
    document::center_imported_design(*imported); // le design arrive centré dans le cadre
    document::Project project;
    commands::SetImportedDesignCommand command(std::move(*imported));
    command.apply(project); // la même commande que la V1 : le design importé EST l'état de départ
    return project;
}

namespace {

// Emprise entière (µm) accumulée point par point ; vide tant qu'aucun point n'a été vu.
struct Extent {
    std::int32_t minX{std::numeric_limits<std::int32_t>::max()};
    std::int32_t minY{std::numeric_limits<std::int32_t>::max()};
    std::int32_t maxX{std::numeric_limits<std::int32_t>::min()};
    std::int32_t maxY{std::numeric_limits<std::int32_t>::min()};
    void add(Vec2um p) {
        minX = std::min(minX, p.x.value);
        minY = std::min(minY, p.y.value);
        maxX = std::max(maxX, p.x.value);
        maxY = std::max(maxY, p.y.value);
    }
    [[nodiscard]] bool empty() const { return minX > maxX; }
};

// Lit le design à insérer : projet .osp, ou broderie machine centrée dans le cadre.
Result<document::Project> loadInsertable(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".osp") {
        return project_io::load_project(file);
    }
    if (!isMachineDesignFile(file)) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "seuls un projet .osp ou une broderie machine s'insèrent");
    }
    auto imported = project_io::import_machine_file(file);
    if (!imported) {
        return std::unexpected(imported.error());
    }
    document::center_imported_design(*imported);
    document::Project incoming;
    incoming.imported_design = std::move(*imported);
    return incoming;
}

std::string fileName(const std::filesystem::path& file) {
    const auto name = file.filename().u8string();
    return {name.begin(), name.end()};
}

} // namespace

DesignInsertPreview previewDesignInsert(const document::Project& target,
                                        const std::filesystem::path& file) {
    DesignInsertPreview out;
    auto incoming = loadInsertable(file);
    if (!incoming) {
        out.message = "« " + fileName(file) + " » n'a pas pu être lu : " + incoming.error().message;
        return out;
    }
    Extent extent;
    out.machine = incoming->imported_design.has_value();
    if (out.machine) {
        out.stitches = incoming->imported_design->sequence.commands.size();
        out.colors = incoming->imported_design->color_blocks.size();
        for (const auto& c : incoming->imported_design->sequence.commands) {
            extent.add(c.pos);
        }
    }
    out.shapes = incoming->vector_objects.size();
    out.embroideries = incoming->embroidery_objects.size();
    out.texts = incoming->text_objects.size();
    for (const auto& object : incoming->vector_objects) {
        for (const auto& set : object.paths) {
            for (const auto& node : set.outer.nodes) {
                extent.add(node.pos);
            }
        }
    }
    if (!extent.empty()) {
        out.widthMm = to_millimeters(Micrometers{extent.maxX - extent.minX}).value;
        out.heightMm = to_millimeters(Micrometers{extent.maxY - extent.minY}).value;
    }
    std::string why;
    if (!commands::make_insert_design_command(target, std::move(*incoming),
                                              Vec2um{Micrometers{0}, Micrometers{0}}, &why)) {
        out.message = "Insertion impossible : " + why;
        return out;
    }
    out.ok = true;
    return out;
}

ServiceResult insertDesign(DocumentSession& session, const std::filesystem::path& file) {
    auto incoming = loadInsertable(file);
    if (!incoming) {
        return {false,
                "« " + fileName(file) + " » n'a pas pu être lu : " + incoming.error().message};
    }
    std::string why;
    auto command = commands::make_insert_design_command(
        session.project(), std::move(*incoming), Vec2um{Micrometers{0}, Micrometers{0}}, &why);
    if (!command) {
        return {false, "Insertion impossible : " + why};
    }
    session.execute(std::move(command));
    return {true, {}};
}

ServiceResult exportVectorsToDxf(const document::Project& project,
                                 const std::filesystem::path& file, std::size_t* count) {
    std::vector<geometry::Path> paths;
    for (const auto& object : project.vector_objects) {
        for (const auto& set : object.paths) {
            paths.push_back(set.outer);
            for (const auto& hole : set.holes) {
                paths.push_back(hole);
            }
        }
    }
    if (paths.empty()) {
        return {false, "Aucun objet vectoriel à exporter."};
    }
    const auto written = formats::write_dxf_file(file, paths);
    if (!written) {
        return {false, "Le fichier DXF n'a pas pu être écrit : " + written.error().message};
    }
    if (count != nullptr) {
        *count = paths.size();
    }
    return {true, {}};
}

ServiceResult exportThreadListCsv(const document::Project& project,
                                  const std::filesystem::path& file, std::size_t* rows) {
    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return {false, "Aucun point généré : " + sequence.error().message};
    }
    return exportThreadListCsv(project, *sequence, file, rows);
}

ServiceResult exportThreadListCsv(const document::Project& project,
                                  const stitch::StitchSequence& sequence_,
                                  const std::filesystem::path& file, std::size_t* rows) {
    const auto* sequence = &sequence_;
    if (sequence->commands.empty()) {
        return {false, "Aucun point généré : rien à exporter."};
    }
    const auto library = thread_palette::ThreadLibrary::with_builtin();
    const auto usage = stitch_analysis::thread_usage(project, *sequence, &library);
    const std::string csv = stitch_analysis::thread_usage_csv(usage);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(csv.data(), static_cast<std::streamsize>(csv.size()));
    if (!out) {
        return {false, "Impossible d'écrire le fichier : " + file.string()};
    }
    if (rows != nullptr) {
        *rows = usage.size();
    }
    return {true, {}};
}

} // namespace openstitch::desktop_v2
