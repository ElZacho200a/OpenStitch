// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/project_io.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

#include "archive.hpp"
#include "json_serialize.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::project_io {

namespace {

constexpr const char* kJsonEntry = "project.json";
constexpr const char* kImageEntry = "original.png";
constexpr const char* kLabelsEntry = "segmentation.u32";

// Labels de segmentation <-> octets bruts little-endian (indépendant de la
// plateforme : on écrit octet par octet).
detail::Blob labels_to_bytes(const std::vector<std::uint32_t>& labels) {
    detail::Blob out(labels.size() * 4);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const std::uint32_t v = labels[i];
        out[i * 4 + 0] = static_cast<std::uint8_t>(v & 0xFF);
        out[i * 4 + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        out[i * 4 + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        out[i * 4 + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    }
    return out;
}

std::vector<std::uint32_t> labels_from_bytes(const detail::Blob& bytes) {
    std::vector<std::uint32_t> out(bytes.size() / 4);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint32_t>(bytes[i * 4 + 0]) |
                 (static_cast<std::uint32_t>(bytes[i * 4 + 1]) << 8) |
                 (static_cast<std::uint32_t>(bytes[i * 4 + 2]) << 16) |
                 (static_cast<std::uint32_t>(bytes[i * 4 + 3]) << 24);
    }
    return out;
}

} // namespace

Result<void> save_project(const std::filesystem::path& path, const document::Project& project) {
    nlohmann::json root;
    root["schemaVersion"] = kSchemaVersion;
    root["document"] = detail::project_to_json(project);
    const std::string jsonText = root.dump(2);

    std::map<std::string, detail::Blob> entries;
    entries.emplace(kJsonEntry, detail::Blob(jsonText.begin(), jsonText.end()));

    if (project.hasImage()) {
        auto png = image::encode_png(project.original);
        if (!png) {
            return std::unexpected(png.error());
        }
        entries.emplace(kImageEntry, std::move(*png));
    }
    if (project.segmentation) {
        entries.emplace(kLabelsEntry, labels_to_bytes(project.segmentation->labels));
    }

    // Écriture atomique : fichier temporaire puis renommage.
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    if (auto written = detail::write_zip(tmp, entries); !written) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return std::unexpected(written.error());
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Sur certains systèmes, rename échoue si la cible existe : on remplace
        // via un fichier de repli, pour ne jamais perdre l'ancien projet.
        std::filesystem::path old = path;
        old += ".old";
        std::error_code ec2;
        std::filesystem::remove(old, ec2);
        std::filesystem::rename(path, old, ec2);
        if (ec2) {
            // Cible verrouillée : le .tmp (complet) est conservé et cité.
            return fail(ErrorCategory::Internal,
                        "Impossible de finaliser l'enregistrement : " + detail::path_utf8(path) +
                            " (fichier ouvert ailleurs ?). Vos données sont conservées dans " +
                            detail::path_utf8(tmp),
                        ec2.message());
        }
        std::error_code ec3;
        std::filesystem::rename(tmp, path, ec3);
        if (ec3) {
            // On restaure l'ancien projet ; le .tmp reste disponible.
            std::error_code ec4;
            std::filesystem::rename(old, path, ec4);
            return fail(ErrorCategory::Internal,
                        "Impossible de finaliser l'enregistrement : " + detail::path_utf8(path) +
                            ". Vos données sont conservées dans " + detail::path_utf8(tmp),
                        ec3.message());
        }
        std::filesystem::remove(old, ec2);
    }
    return {};
}

std::filesystem::path migration_backup_path(const std::filesystem::path& path, int fromVersion) {
    std::filesystem::path out = path;
    out.replace_extension();
    out += ".v" + std::to_string(fromVersion) + ".osp.bak";
    return out;
}

Result<void> backup_before_migrated_save(const std::filesystem::path& path, int fromVersion) {
    const auto bak = migration_backup_path(path, fromVersion);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || std::filesystem::exists(bak, ec)) {
        return {};
    }
    std::filesystem::copy_file(path, bak, std::filesystem::copy_options::skip_existing, ec);
    if (ec) {
        return fail(ErrorCategory::Internal,
                    "Impossible de créer la copie de sécurité : " + detail::path_utf8(bak),
                    ec.message());
    }
    return {};
}

namespace {

Result<document::Project> load_project_impl(const std::filesystem::path& path, LoadInfo* info) {
    auto entries = detail::read_zip(path);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const auto jsonIt = entries->find(kJsonEntry);
    if (jsonIt == entries->end()) {
        return fail(ErrorCategory::InvalidFile, "Archive projet invalide : project.json manquant");
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonIt->second);
    } catch (const nlohmann::json::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "project.json illisible", ex.what());
    }

    const int version = root.is_object() ? root.value("schemaVersion", 0) : 0;
    if (version <= 0 || version > kSchemaVersion) {
        if (version > kSchemaVersion) {
            return fail(ErrorCategory::UnsupportedFormat,
                        "Ce projet a été créé par une version plus récente d'OpenStitch "
                        "(format v" +
                            std::to_string(version) + ", cette version lit jusqu'à v" +
                            std::to_string(kSchemaVersion) +
                            "). Mettez à jour OpenStitch pour l'ouvrir.");
        }
        return fail(ErrorCategory::InvalidFile,
                    "Version de projet invalide (" + std::to_string(version) + ")");
    }
    if (info != nullptr) {
        info->fileVersion = version;
        info->migrated = version < kSchemaVersion;
    }

    if (!root.contains("document")) {
        return fail(ErrorCategory::InvalidFile,
                    "project.json incomplet : section document absente");
    }
    auto project = detail::project_from_json(root.at("document"));
    if (!project) {
        return std::unexpected(project.error());
    }

    if (const auto imgIt = entries->find(kImageEntry); imgIt != entries->end()) {
        auto img = image::decode_image(imgIt->second);
        if (!img) {
            return std::unexpected(img.error());
        }
        project->original = std::move(*img);
    }

    if (project->segmentation) {
        const auto labelsIt = entries->find(kLabelsEntry);
        if (labelsIt == entries->end()) {
            return fail(ErrorCategory::InvalidFile,
                        "Segmentation présente mais carte des labels manquante");
        }
        project->segmentation->labels = labels_from_bytes(labelsIt->second);
        const std::size_t expected = static_cast<std::size_t>(project->segmentation->width) *
                                     static_cast<std::size_t>(project->segmentation->height);
        if (project->segmentation->labels.size() != expected) {
            return fail(ErrorCategory::InvalidFile,
                        "Carte de segmentation incohérente avec ses dimensions");
        }
    }

    return project;
}

} // namespace

Result<document::Project> load_project(const std::filesystem::path& path, LoadInfo* info) {
    // Un JSON syntaxiquement valide mais de structure inattendue lève des
    // exceptions nlohmann (type_error, out_of_range...) : aucune ne doit fuir.
    try {
        return load_project_impl(path, info);
    } catch (const nlohmann::json::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Fichier projet invalide (structure inattendue)",
                    ex.what());
    } catch (const std::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Fichier projet illisible", ex.what());
    }
}

} // namespace openstitch::project_io
