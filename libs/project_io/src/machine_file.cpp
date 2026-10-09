// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/machine_file.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "archive.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/stitch_analysis/color_blocks.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_io {

namespace {

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

Result<void> export_machine_file(const document::Project& project, const std::string& format_id,
                                 const std::filesystem::path& path) {
    const auto* info = formats::find_format(format_id);
    if (info == nullptr || !info->can_write || info->encode == nullptr) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Format d'export non pris en charge en écriture : " + format_id);
    }
    // Point d'entrée de production unique (jamais generate_sequence directement) :
    // retouches manuelles incluses.
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    auto bytes = info->encode(*sequence);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    // Écriture atomique : fichier temporaire puis renommage, pour ne jamais
    // détruire un export existant si l'écriture échoue en cours de route.
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return fail(ErrorCategory::UserInput,
                        "Impossible d'écrire le fichier : " + detail::path_utf8(path));
        }
        file.write(reinterpret_cast<const char*>(bytes->data()),
                   static_cast<std::streamsize>(bytes->size()));
        file.flush();
        if (!file) {
            file.close();
            std::error_code rmEc;
            std::filesystem::remove(tmp, rmEc);
            return fail(ErrorCategory::Internal, "Échec d'écriture : " + detail::path_utf8(path) +
                                                     " (disque plein ou fichier verrouillé ?)");
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // rename remplace normalement la cible ; sinon la cible est peut-être
        // verrouillée : on garde le .tmp (données complètes) et on le cite.
        return fail(ErrorCategory::Internal,
                    "Impossible de finaliser l'export : " + detail::path_utf8(path) +
                        " (le fichier est peut-être ouvert dans un autre programme). Les données "
                        "complètes sont conservées dans " +
                        detail::path_utf8(tmp),
                    ec.message());
    }
    return {};
}

Result<document::ImportedDesign> import_machine_file(const std::filesystem::path& path,
                                                     std::string format_id) {
    if (format_id.empty()) {
        format_id = lower_extension(path);
    }
    const auto* info = formats::find_format(format_id);
    if (info == nullptr || !info->can_read || info->decode == nullptr) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Format d'import non pris en charge en lecture : " + format_id);
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput,
                    "Fichier introuvable ou illisible : " + detail::path_utf8(path));
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    auto sequence = info->decode(bytes);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }

    document::ImportedDesign imported;
    imported.source_format = info->id;
    imported.color_blocks = stitch_analysis::color_blocks(document::Project{}, *sequence);
    imported.sequence = std::move(*sequence);
    return imported;
}

} // namespace openstitch::project_io
