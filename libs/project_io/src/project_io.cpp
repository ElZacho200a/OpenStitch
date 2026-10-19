// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/core/parallel.hpp"

#include <algorithm>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>

#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "archive.hpp"
#include "json_serialize.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_io/migrations.hpp"

namespace openstitch::project_io {

namespace {

// Version écrite : v7 seulement si un auto-satin utilise une méthode autre que le squelette
// (un lecteur v6 la régénérerait en silence avec le squelette).
int schema_version_for(const std::vector<document::EmbroideryObject>& objects,
                       const std::vector<document::TextObject>& texts = {},
                       bool usesCalques = false) {
    // v9 : le document définit des calques (jamais pour les fichiers existants).
    if (usesCalques) {
        return kSchemaVersionCalques;
    }
    // v8 : un texte porte un crénage manuel ou des ligatures (jamais pour les fichiers existants).
    for (const auto& text : texts) {
        if (!text.kerning_pairs.empty() || text.ligatures) {
            return kSchemaVersionTextTypography;
        }
    }
    for (const auto& object : objects) {
        if (const auto* satin = std::get_if<document::AutoSatinParams>(&object.params)) {
            if (satin->strategy != document::AutoSatinStrategy::Skeleton) {
                return kSchemaVersionAutoSatinStrategy;
            }
        }
    }
    return kSchemaVersion;
}

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

namespace {

// PNG de la dernière image enregistrée. L'image d'origine ne change presque jamais entre deux
// enregistrements (sauvegarde automatique toutes les 2 min, Ctrl+S répétés), alors que son
// encodage domine le temps d'écriture du projet. Clé EXACTE : dimensions + octets comparés un à
// un (aucune empreinte, donc aucune collision possible). Une seule entrée, bornée à 64 Mio de
// pixels : au-delà, pas de cache.
Result<std::vector<std::uint8_t>> cached_png(const image::Image& image) {
    constexpr std::size_t kMaxCachedBytes = 64U * 1024U * 1024U;
    struct Cache {
        std::mutex mutex;
        int width{0};
        int height{0};
        std::vector<std::uint8_t> rgba;
        std::vector<std::uint8_t> png;
    };
    static Cache cache;
    if (image.rgba.size() > kMaxCachedBytes) {
        return image::encode_png(image);
    }
    {
        const std::lock_guard<std::mutex> lock(cache.mutex);
        if (!cache.png.empty() && cache.width == image.width && cache.height == image.height &&
            cache.rgba == image.rgba) {
            return cache.png;
        }
    }
    auto encoded = image::encode_png(image);
    if (encoded) {
        const std::lock_guard<std::mutex> lock(cache.mutex);
        cache.width = image.width;
        cache.height = image.height;
        cache.rgba = image.rgba;
        cache.png = *encoded;
    }
    return encoded;
}

} // namespace

Result<void> save_project(const std::filesystem::path& path, const document::Project& project) {
    // Les trois entrées (JSON, PNG de l'image, étiquettes) sont indépendantes et ne font que
    // lire le projet : préparées en parallèle. L'encodage PNG domine sur une grande image.
    std::string jsonText;
    std::optional<Result<std::vector<std::uint8_t>>> png;
    detail::Blob labels;
    parallel_for(3, [&](std::size_t task) {
        if (task == 0) {
            nlohmann::json root;
            root["schemaVersion"] = schema_version_for(
                project.embroidery_objects, project.text_objects, !project.calques.empty());
            root["document"] = detail::project_to_json(project);
            jsonText = root.dump(2);
        } else if (task == 1) {
            if (project.hasImage()) {
                png = cached_png(project.original);
            }
        } else if (project.segmentation) {
            labels = labels_to_bytes(project.segmentation->labels);
        }
    });

    std::map<std::string, detail::Blob> entries;
    entries.emplace(kJsonEntry, detail::Blob(jsonText.begin(), jsonText.end()));
    if (png) {
        if (!*png) {
            return std::unexpected(png->error());
        }
        entries.emplace(kImageEntry, std::move(**png));
    }
    if (project.segmentation) {
        entries.emplace(kLabelsEntry, std::move(labels));
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
    if (version <= 0 || version > kMaxReadableSchemaVersion) {
        if (version > kMaxReadableSchemaVersion) {
            return fail(ErrorCategory::UnsupportedFormat,
                        "Ce projet a été créé par une version plus récente d'OpenStitch "
                        "(format v" +
                            std::to_string(version) + ", cette version lit jusqu'à v" +
                            std::to_string(kMaxReadableSchemaVersion) +
                            "). Mettez à jour OpenStitch pour l'ouvrir.");
        }
        return fail(ErrorCategory::InvalidFile,
                    "Version de projet invalide (" + std::to_string(version) + ")");
    }
    if (info != nullptr) {
        info->fileVersion = version;
        info->migrated = version < kSchemaVersion;
    }

    // Migrations versionnées v(n) -> v(n+1) (HP-FMT-021) avant la lecture du document.
    {
        auto migrated = migrate_project_json(std::string_view(
            reinterpret_cast<const char*>(jsonIt->second.data()), jsonIt->second.size()));
        if (!migrated) {
            return std::unexpected(migrated.error());
        }
        root = nlohmann::json::parse(*migrated);
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

std::string fragment_to_json(const document::ObjectFragment& fragment) {
    // Un projet temporaire ne portant QUE le fragment : on réutilise tel quel le
    // sérialiseur du `.osp` (un seul schéma à maintenir).
    document::Project carrier;
    carrier.vector_objects = fragment.vectors;
    carrier.embroidery_objects = fragment.embroideries;
    nlohmann::json root;
    root["openstitchFragment"] = 1;
    root["schemaVersion"] = schema_version_for(carrier.embroidery_objects);
    root["document"] = detail::project_to_json(carrier);
    return root.dump();
}

Result<document::ObjectFragment> fragment_from_json(std::string_view text) {
    try {
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (!root.is_object() || !root.contains("openstitchFragment") ||
            !root.contains("document")) {
            return fail(ErrorCategory::InvalidFile,
                        "Le presse-papiers ne contient pas d'objets OpenStitch");
        }
        if (root.value("schemaVersion", 0) > kMaxReadableSchemaVersion) {
            return fail(ErrorCategory::InvalidFile,
                        "Objets copiés depuis une version plus récente d'OpenStitch");
        }
        auto carrier = detail::project_from_json(root.at("document"));
        if (!carrier) {
            return std::unexpected(carrier.error());
        }
        document::ObjectFragment fragment;
        fragment.vectors = std::move(carrier->vector_objects);
        fragment.embroideries = std::move(carrier->embroidery_objects);
        for (const auto& emb : fragment.embroideries) {
            const bool hasSource = std::any_of(
                fragment.vectors.begin(), fragment.vectors.end(),
                [&](const document::VectorObject& v) { return v.id == emb.source_vector; });
            if (!hasSource) {
                return fail(ErrorCategory::InvalidFile,
                            "Fragment incohérent : une broderie n'a pas sa forme source");
            }
        }
        return fragment;
    } catch (const std::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Presse-papiers illisible", ex.what());
    }
}

} // namespace openstitch::project_io
