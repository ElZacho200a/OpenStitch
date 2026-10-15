// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/project_file.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/project_ops/inspect.hpp"

namespace openstitch::project_ops {

Result<OpenedProject> open_project(const std::filesystem::path& path) {
    OpenedProject opened;
    auto loaded = project_io::load_project(path, &opened.info);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    opened.project = std::move(*loaded);
    return opened;
}

Result<void> write_project(const std::filesystem::path& target, const document::Project& project,
                           const project_io::LoadInfo* source,
                           const std::filesystem::path& sourcePath, bool no_clobber) {
    std::error_code ec;
    const auto parent = target.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) {
        return fail(ErrorCategory::UserInput,
                    "le dossier de sortie « " + parent.string() + " » n'existe pas",
                    "créez-le d'abord ou choisissez un autre chemin");
    }
    if (no_clobber && std::filesystem::exists(target, ec)) {
        return fail(ErrorCategory::UserInput,
                    "« " + target.string() + " » existe déjà (--no-clobber)",
                    "retirez --no-clobber pour l'écraser");
    }
    // Écraser un fichier migré : copie de sécurité d'abord (jamais de perte du format d'origine).
    if (source != nullptr && source->migrated && !sourcePath.empty() &&
        std::filesystem::equivalent(sourcePath, target, ec)) {
        if (auto backup = project_io::backup_before_migrated_save(target, source->fileVersion);
            !backup) {
            return std::unexpected(backup.error());
        }
    }
    return project_io::save_project(target, project);
}

std::vector<std::string> check_integrity(const document::Project& project) {
    std::vector<std::string> problems;
    std::vector<std::uint64_t> seen;
    const auto note = [&](const std::string& msg) { problems.push_back(msg); };
    for (const auto& v : project.vector_objects) {
        if (std::find(seen.begin(), seen.end(), v.id.value) != seen.end()) {
            note("identifiant d'objet vectoriel en double : " + std::to_string(v.id.value));
        }
        seen.push_back(v.id.value);
        if (v.source_region && project.segmentation &&
            project.segmentation->find(*v.source_region) == nullptr) {
            note("objet vectoriel " + std::to_string(v.id.value) + " : région source " +
                 std::to_string(v.source_region->value) + " absente de la segmentation");
        }
    }
    for (const auto& e : project.embroidery_objects) {
        if (std::find(seen.begin(), seen.end(), e.id.value) != seen.end()) {
            note("identifiant d'objet en double : " + std::to_string(e.id.value));
        }
        seen.push_back(e.id.value);
        const auto* v = project.findObject(e.source_vector);
        if (v == nullptr) {
            if (!e.is_satin()) {
                note("objet de broderie " + std::to_string(e.id.value) + " : source vectorielle " +
                     std::to_string(e.source_vector.value) + " introuvable");
            }
        } else if (e.visible && v->paths.empty()) {
            note("objet de broderie " + std::to_string(e.id.value) +
                 " visible mais sa source vectorielle est vide");
        }
        if (e.color_blend && project.findEmbroidery(e.color_blend->partner) == nullptr) {
            note("objet de broderie " + std::to_string(e.id.value) + " : partenaire de fondu " +
                 std::to_string(e.color_blend->partner.value) + " introuvable");
        }
    }
    const auto last = project.object_ids.last();
    for (const auto id : seen) {
        if (id > last) {
            note("identifiant " + std::to_string(id) +
                 " supérieur au compteur du projet (risque de réutilisation)");
            break;
        }
    }
    return problems;
}

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(sep, start);
        parts.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }
    return parts;
}

bool parse_double(const std::string& s, double& out) {
    const char* first = s.data();
    const char* last = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

Error bad_criterion(const std::string& c, const std::string& why) {
    return Error{ErrorCategory::UserInput, "critère de sélection invalide « " + c + " » : " + why,
                 "critères : id=1,2 color=#RRGGBB type=tatami visible=true locked=false "
                 "name=texte area_gt=mm2 area_lt=mm2 within=x0,y0,x1,y1"};
}

} // namespace

Result<std::vector<ObjectId>> select_objects(const document::Project& project,
                                             const std::vector<std::string>& criteria) {
    struct Rect {
        double x0, y0, x1, y1;
    };
    std::vector<std::uint64_t> ids;
    std::optional<std::array<std::uint8_t, 3>> color;
    std::optional<std::string> type;
    std::optional<bool> visible;
    std::optional<bool> locked;
    std::optional<std::string> name;
    std::optional<double> areaGt;
    std::optional<double> areaLt;
    std::optional<Rect> within;
    for (const auto& c : criteria) {
        const auto eq = c.find('=');
        if (eq == std::string::npos || eq == 0) {
            return std::unexpected(bad_criterion(c, "forme attendue clé=valeur"));
        }
        const std::string key = c.substr(0, eq);
        const std::string val = c.substr(eq + 1);
        if (key == "id") {
            for (const auto& p : split(val, ',')) {
                std::uint64_t v = 0;
                const auto [ptr, ec] = std::from_chars(p.data(), p.data() + p.size(), v);
                if (ec != std::errc{} || ptr != p.data() + p.size()) {
                    return std::unexpected(bad_criterion(c, "identifiants entiers séparés par des virgules"));
                }
                ids.push_back(v);
            }
        } else if (key == "color") {
            color = rgb_from_hex(val);
            if (!color) {
                return std::unexpected(bad_criterion(c, "couleur #RRGGBB attendue"));
            }
        } else if (key == "type") {
            if (val != "running" && val != "tatami" && val != "satin" && val != "directional" &&
                val != "auto_satin") {
                return std::unexpected(
                    bad_criterion(c, "type parmi running, tatami, satin, directional, auto_satin"));
            }
            type = val;
        } else if (key == "visible" || key == "locked") {
            if (val != "true" && val != "false") {
                return std::unexpected(bad_criterion(c, "true ou false"));
            }
            (key == "visible" ? visible : locked) = (val == "true");
        } else if (key == "name") {
            name = val;
        } else if (key == "area_gt" || key == "area_lt") {
            double v = 0.0;
            if (!parse_double(val, v)) {
                return std::unexpected(bad_criterion(c, "nombre (mm²) attendu"));
            }
            (key == "area_gt" ? areaGt : areaLt) = v;
        } else if (key == "within") {
            const auto p = split(val, ',');
            Rect r{};
            if (p.size() != 4 || !parse_double(p[0], r.x0) || !parse_double(p[1], r.y0) ||
                !parse_double(p[2], r.x1) || !parse_double(p[3], r.y1)) {
                return std::unexpected(bad_criterion(c, "quatre nombres x0,y0,x1,y1 (mm)"));
            }
            within = r;
        } else {
            return std::unexpected(bad_criterion(c, "clé inconnue « " + key + " »"));
        }
    }

    std::vector<ObjectId> out;
    for (const auto& e : project.embroidery_objects) {
        if (!ids.empty() && std::find(ids.begin(), ids.end(), e.id.value) == ids.end()) {
            continue;
        }
        if (color && e.rgb != *color) {
            continue;
        }
        if (type && *type != stitch_type_name(e)) {
            continue;
        }
        if (visible && e.visible != *visible) {
            continue;
        }
        if (locked && e.locked != *locked) {
            continue;
        }
        if (name && e.name.find(*name) == std::string::npos) {
            continue;
        }
        const auto* v = project.findObject(e.source_vector);
        if (areaGt || areaLt) {
            if (v == nullptr) {
                continue;
            }
            double um2 = 0.0;
            for (const auto& set : v->paths) {
                um2 += geometry::path_set_area_um2(set);
            }
            const double mm2 = um2 / 1.0e6;
            if ((areaGt && !(mm2 > *areaGt)) || (areaLt && !(mm2 < *areaLt))) {
                continue;
            }
        }
        if (within) {
            if (v == nullptr || v->paths.empty()) {
                continue;
            }
            bool inside = true;
            const auto check = [&](const geometry::Path& path) {
                for (const auto& n : path.nodes) {
                    const double x = n.pos.x.value / 1000.0;
                    const double y = n.pos.y.value / 1000.0;
                    inside = inside && x >= std::min(within->x0, within->x1) &&
                             x <= std::max(within->x0, within->x1) &&
                             y >= std::min(within->y0, within->y1) &&
                             y <= std::max(within->y0, within->y1);
                }
            };
            for (const auto& set : v->paths) {
                check(set.outer);
                for (const auto& h : set.holes) {
                    check(h);
                }
            }
            if (!inside) {
                continue;
            }
        }
        out.push_back(e.id);
    }
    return out;
}

} // namespace openstitch::project_ops
