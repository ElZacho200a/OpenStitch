// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/project_io.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
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

}  // namespace

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
        // Sur certains systèmes, rename échoue si la cible existe : on remplace.
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return fail(ErrorCategory::Internal,
                        "Impossible de finaliser l'enregistrement : " + path.string(),
                        ec.message());
        }
    }
    return {};
}

Result<document::Project> load_project(const std::filesystem::path& path) {
    auto entries = detail::read_zip(path);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const auto jsonIt = entries->find(kJsonEntry);
    if (jsonIt == entries->end()) {
        return fail(ErrorCategory::InvalidFile,
                    "Archive projet invalide : project.json manquant");
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonIt->second);
    } catch (const nlohmann::json::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "project.json illisible", ex.what());
    }

    const int version = root.value("schemaVersion", 0);
    if (version <= 0 || version > kSchemaVersion) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Version de projet non prise en charge (" + std::to_string(version) + ")");
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

}  // namespace openstitch::project_io
