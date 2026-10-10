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
                                 const std::filesystem::path& path,
                                 const formats::MachineExportOptions& options) {
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
    formats::MachineExportOptions effective = options;
    if (effective.block_colors.empty() && info->carries_colors) {
        for (const auto& block : stitch_analysis::color_blocks(project, *sequence)) {
            effective.block_colors.push_back(block.rgb);
        }
    }
    auto bytes = info->encode_ex != nullptr ? info->encode_ex(*sequence, effective)
                                            : info->encode(*sequence);
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
    formats::DecodedDesign design;
    if (info->decode_ex != nullptr) {
        auto decoded = info->decode_ex(bytes);
        if (!decoded) {
            return std::unexpected(decoded.error());
        }
        design = std::move(*decoded);
    } else {
        auto sequence = info->decode(bytes);
        if (!sequence) {
            return std::unexpected(sequence.error());
        }
        design.sequence = std::move(*sequence);
    }

    document::ImportedDesign imported;
    imported.source_format = info->id;
    imported.color_blocks = stitch_analysis::color_blocks(document::Project{}, design.sequence);
    // Couleurs lues dans le fichier (PES, JEF) : le bloc dont la première commande suit k
    // changements de couleur/arrêts reçoit la couleur du k-ième segment.
    if (!design.block_colors.empty()) {
        for (auto& block : imported.color_blocks) {
            std::size_t segment = 0;
            for (std::size_t i = 0; i < block.start && i < design.sequence.commands.size(); ++i) {
                const auto type = design.sequence.commands[i].type;
                segment +=
                    (type == stitch::CommandType::ColorChange || type == stitch::CommandType::Stop)
                        ? 1
                        : 0;
            }
            block.rgb = design.block_colors[std::min(segment, design.block_colors.size() - 1)];
        }
    }
    imported.sequence = std::move(design.sequence);
    return imported;
}

} // namespace openstitch::project_io
