// SPDX-License-Identifier: Apache-2.0
#include "file_io.hpp"

#include <algorithm>
#include <cctype>

#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/document/imported_design.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/project_io/machine_file.hpp"

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

} // namespace openstitch::desktop_v2
