// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/thread_usage.hpp"

#include <algorithm>
#include <cstdio>

#include "openstitch/stitch_analysis/color_blocks.hpp"
#include "openstitch/thread_palette/chart_import.hpp"

namespace openstitch::stitch_analysis {

namespace {

using stitch::CommandType;

ThreadIdentity identity_of(const document::Project& project, ObjectId source) {
    if (const auto* emb = project.findEmbroidery(source)) {
        return ThreadIdentity{emb->thread, emb->rgb};
    }
    // Source inconnue (design importé) : couleur libre noire, comme color_blocks.
    return ThreadIdentity{std::nullopt, {0, 0, 0}};
}

template <typename Entry> Entry* find_entry(std::vector<Entry>& entries, const ThreadIdentity& id) {
    for (auto& e : entries) {
        if (e.identity == id) {
            return &e;
        }
    }
    return nullptr;
}

void add_unique(std::vector<ObjectId>& ids, ObjectId id) {
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
        ids.push_back(id);
    }
}

std::string fixed1(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

std::string csv_field(const std::string& s) {
    if (s.find_first_of(";\"\n\r") == std::string::npos) {
        return s;
    }
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// Ordre complet des objets après redistribution de `desired` (objets libres du
// film) dans les emplacements libres.
std::vector<ObjectId> redistribute(const document::Project& project,
                                   const std::vector<ColorFilmBlock>& film,
                                   const std::vector<ObjectId>& desired) {
    std::vector<ObjectId> inFilm;
    for (const auto& b : film) {
        inFilm.insert(inFilm.end(), b.objects.begin(), b.objects.end());
    }
    std::vector<ObjectId> order;
    std::vector<bool> fixed;
    for (const auto& obj : project.embroidery_objects) {
        order.push_back(obj.id);
        fixed.push_back(obj.locked ||
                        std::find(inFilm.begin(), inFilm.end(), obj.id) == inFilm.end());
    }
    std::size_t next = 0;
    std::vector<ObjectId> freeDesired;
    for (const ObjectId id : desired) {
        const auto* emb = project.findEmbroidery(id);
        if (emb != nullptr && !emb->locked) {
            freeDesired.push_back(id);
        }
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (!fixed[i] && next < freeDesired.size()) {
            order[i] = freeDesired[next++];
        }
    }
    return order;
}

} // namespace

std::vector<ThreadUsage> thread_usage(const document::Project& project,
                                      const stitch::StitchSequence& sequence,
                                      const thread_palette::ThreadLibrary* library,
                                      const ThreadUsageOptions& options) {
    std::vector<ThreadUsage> usage;
    const auto entry_for = [&](ObjectId source) -> ThreadUsage& {
        const ThreadIdentity id = identity_of(project, source);
        if (auto* e = find_entry(usage, id)) {
            return *e;
        }
        ThreadUsage u;
        u.identity = id;
        if (id.key) {
            u.code = id.key->code;
            if (library != nullptr) {
                if (const auto t = library->find(*id.key)) {
                    u.brand = t->brand;
                    u.code = t->key.code;
                    u.name = t->name;
                }
            }
        }
        usage.push_back(std::move(u));
        return usage.back();
    };

    const auto& cmds = sequence.commands;
    std::vector<std::size_t> trims; // coupes par fil, indexé comme `usage`
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const auto& c = cmds[i];
        if (c.type == CommandType::Stitch) {
            ThreadUsage& u = entry_for(c.source);
            ++u.stitch_count;
            add_unique(u.objects, c.source);
            if (i > 0 && cmds[i - 1].type == CommandType::Stitch &&
                cmds[i - 1].source == c.source) {
                u.length_mm += length_um(c.pos - cmds[i - 1].pos) / 1000.0;
            }
        } else if (c.type == CommandType::Trim) {
            ThreadUsage& u = entry_for(c.source);
            const auto idx = static_cast<std::size_t>(&u - usage.data());
            if (trims.size() < usage.size()) {
                trims.resize(usage.size(), 0);
            }
            ++trims[idx];
        }
    }
    trims.resize(usage.size(), 0);

    // Passages : un bloc de couleur compte pour le fil de sa première commande.
    const auto blocks = color_blocks(project, sequence);
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const ThreadIdentity id = identity_of(project, cmds[blocks[b].start].source);
        if (auto* e = find_entry(usage, id)) {
            ++e->color_blocks;
        }
    }

    for (std::size_t i = 0; i < usage.size(); ++i) {
        ThreadUsage& u = usage[i];
        const double sew =
            options.stitches_per_minute > 0.0
                ? static_cast<double>(u.stitch_count) / options.stitches_per_minute * 60.0
                : 0.0;
        // Un changement de fil par bloc, sauf le tout premier bloc du design (le
        // premier fil utilisé, `usage[0]`, est déjà enfilé au départ).
        const std::size_t changes =
            u.color_blocks > 0 && i == 0 ? u.color_blocks - 1 : u.color_blocks;
        u.estimated_seconds = sew + static_cast<double>(changes) * options.color_change_seconds +
                              static_cast<double>(trims[i]) * options.trim_seconds;
    }
    return usage;
}

std::string thread_label(const ThreadUsage& usage) {
    if (!usage.identity.key) {
        return thread_palette::to_hex_color(usage.identity.rgb);
    }
    std::string label;
    if (!usage.brand.empty()) {
        label = usage.brand + " ";
    }
    label += usage.code;
    if (!usage.name.empty()) {
        label += " \xE2\x80\x94 " + usage.name; // tiret cadratin
    }
    return label;
}

std::string thread_usage_csv(const std::vector<ThreadUsage>& usage) {
    std::string out = "ordre;marque;nuancier;reference;nom;couleur;objets;points;longueur_mm;"
                      "duree_s\n";
    std::size_t rank = 0;
    for (const auto& u : usage) {
        out += std::to_string(++rank) + ';' + csv_field(u.brand) + ';' +
               csv_field(u.identity.key ? u.identity.key->chart_id : std::string()) + ';' +
               csv_field(u.code) + ';' + csv_field(u.name) + ';' +
               thread_palette::to_hex_color(u.identity.rgb) + ';' +
               std::to_string(u.objects.size()) + ';' + std::to_string(u.stitch_count) + ';' +
               fixed1(u.length_mm) + ';' + fixed1(u.estimated_seconds) + '\n';
    }
    return out;
}

std::vector<ObjectId> objects_using_thread(const document::Project& project,
                                           const ThreadIdentity& identity) {
    std::vector<ObjectId> ids;
    for (const auto& obj : project.embroidery_objects) {
        const bool match =
            identity.key ? obj.thread == identity.key : (!obj.thread && obj.rgb == identity.rgb);
        if (match) {
            ids.push_back(obj.id);
        }
    }
    return ids;
}

std::vector<ColorFilmBlock> color_film(const document::Project& project,
                                       const stitch::StitchSequence& sequence) {
    std::vector<ColorFilmBlock> film;
    const auto& cmds = sequence.commands;
    for (const auto& block : color_blocks(project, sequence)) {
        ColorFilmBlock f;
        f.identity = identity_of(project, cmds[block.start].source);
        for (std::size_t i = block.start; i < block.end && i < cmds.size(); ++i) {
            const auto& c = cmds[i];
            if (c.type != CommandType::Stitch) {
                continue;
            }
            ++f.stitch_count;
            add_unique(f.objects, c.source);
            if (i > block.start && cmds[i - 1].type == CommandType::Stitch &&
                cmds[i - 1].source == c.source) {
                f.length_mm += length_um(c.pos - cmds[i - 1].pos) / 1000.0;
            }
        }
        if (f.stitch_count == 0) {
            continue;
        }
        for (const ObjectId id : f.objects) {
            if (const auto* emb = project.findEmbroidery(id); emb != nullptr && emb->locked) {
                f.locked = true;
            }
        }
        film.push_back(std::move(f));
    }
    return film;
}

std::optional<std::vector<ObjectId>> reorder_film_blocks(const document::Project& project,
                                                         const std::vector<ColorFilmBlock>& film,
                                                         std::size_t from, std::size_t to) {
    if (from >= film.size() || to >= film.size()) {
        return std::nullopt;
    }
    std::vector<std::size_t> perm(film.size());
    for (std::size_t i = 0; i < perm.size(); ++i) {
        perm[i] = i;
    }
    perm.erase(perm.begin() + static_cast<std::ptrdiff_t>(from));
    perm.insert(perm.begin() + static_cast<std::ptrdiff_t>(to), from);
    std::vector<ObjectId> desired;
    for (const std::size_t b : perm) {
        desired.insert(desired.end(), film[b].objects.begin(), film[b].objects.end());
    }
    return redistribute(project, film, desired);
}

std::vector<ObjectId> merge_same_thread_blocks(const document::Project& project,
                                               const std::vector<ColorFilmBlock>& film) {
    std::vector<std::size_t> perm;
    std::vector<bool> used(film.size(), false);
    for (std::size_t i = 0; i < film.size(); ++i) {
        if (used[i]) {
            continue;
        }
        perm.push_back(i);
        used[i] = true;
        for (std::size_t j = i + 1; j < film.size(); ++j) {
            if (!used[j] && film[j].identity == film[i].identity) {
                perm.push_back(j);
                used[j] = true;
            }
        }
    }
    std::vector<ObjectId> desired;
    for (const std::size_t b : perm) {
        desired.insert(desired.end(), film[b].objects.begin(), film[b].objects.end());
    }
    return redistribute(project, film, desired);
}

std::size_t color_change_count(const std::vector<ColorFilmBlock>& film) {
    std::size_t changes = 0;
    for (std::size_t i = 1; i < film.size(); ++i) {
        // Deux blocs consécutifs de même fil ne demandent aucun changement réel.
        if (!(film[i].identity == film[i - 1].identity)) {
            ++changes;
        }
    }
    return changes;
}

} // namespace openstitch::stitch_analysis
