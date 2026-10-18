// SPDX-License-Identifier: Apache-2.0
#include "openstitch/segmentation/color_merge.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>

#include "openstitch/thread_palette/color_reduction.hpp"

namespace openstitch::segmentation {

namespace {

using Rgb = std::array<std::uint8_t, 3>;

// Couleur finale de chaque emplacement de région (nullopt : emplacement vide ou région exclue).
using FinalColors = std::vector<std::optional<Rgb>>;

std::size_t slot_index(RegionId id) {
    return static_cast<std::size_t>(id.value - 1);
}

// Une classe de teinte (régions vivantes de cette couleur, hors couleur exclue).
std::vector<ColorClass> classes_without(const Segmentation& seg,
                                        const std::optional<Rgb>& excluded) {
    std::vector<ColorClass> all = color_classes(seg);
    if (excluded) {
        std::erase_if(all, [&](const ColorClass& c) { return c.rgb == *excluded; });
    }
    return all;
}

// Plan à partir des couleurs finales : recolorations (celles qui changent) et, si demandé, les
// fusions de régions voisines devenues de même couleur.
ColorMergePlan make_plan(const Segmentation& seg, const FinalColors& finals, bool merge_adjacent) {
    ColorMergePlan plan;
    std::set<Rgb> before;
    std::set<Rgb> after;
    for (std::size_t slot = 0; slot < seg.region_slots.size(); ++slot) {
        const auto& region = seg.region_slots[slot];
        if (!region || !finals[slot]) {
            continue;
        }
        before.insert(region->rgb);
        after.insert(*finals[slot]);
        if (*finals[slot] != region->rgb) {
            plan.recolors.emplace_back(region->id, *finals[slot]);
        }
    }
    plan.colors_before = before.size();
    plan.colors_after = after.size();
    if (!merge_adjacent) {
        return plan;
    }

    // Composantes de régions voisines de même couleur finale (union-find sur les frontières).
    std::vector<std::size_t> parent(seg.region_slots.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&parent](std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for (const RegionBorder& border : region_adjacency(seg)) {
        const std::size_t a = slot_index(border.a);
        const std::size_t b = slot_index(border.b);
        if (a >= finals.size() || b >= finals.size() || !finals[a] || !finals[b] ||
            *finals[a] != *finals[b]) {
            continue;
        }
        const std::size_t ra = find(a);
        const std::size_t rb = find(b);
        if (ra != rb) {
            parent[std::max(ra, rb)] = std::min(ra, rb);
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> components; // racine -> emplacements croissants
    for (std::size_t slot = 0; slot < seg.region_slots.size(); ++slot) {
        if (seg.region_slots[slot] && finals[slot]) {
            components[find(slot)].push_back(slot);
        }
    }
    for (const auto& [root, members] : components) {
        (void)root;
        if (members.size() < 2) {
            continue;
        }
        // Garde la plus grande région (égalité -> plus petit identifiant).
        std::size_t keep = members.front();
        for (const std::size_t slot : members) {
            if (seg.region_slots[slot]->pixel_count > seg.region_slots[keep]->pixel_count) {
                keep = slot;
            }
        }
        for (const std::size_t slot : members) {
            if (slot != keep) {
                plan.merges.emplace_back(seg.region_slots[keep]->id, seg.region_slots[slot]->id);
            }
        }
    }
    return plan; // les régions exclues ont un `finals` vide : jamais recolorées ni fusionnées
}

// Couleurs finales = couleurs actuelles (hors couleur exclue, absentes de `finals`).
FinalColors current_colors(const Segmentation& seg, const std::optional<Rgb>& excluded) {
    FinalColors finals(seg.region_slots.size());
    for (std::size_t slot = 0; slot < seg.region_slots.size(); ++slot) {
        const auto& region = seg.region_slots[slot];
        if (region && !(excluded && region->rgb == *excluded)) {
            finals[slot] = region->rgb;
        }
    }
    return finals;
}

} // namespace

std::vector<ColorClass> color_classes(const Segmentation& seg) {
    std::map<Rgb, ColorClass> byColor;
    for (const auto& slot : seg.region_slots) {
        if (!slot) {
            continue;
        }
        ColorClass& cls = byColor[slot->rgb];
        cls.rgb = slot->rgb;
        cls.pixel_count += slot->pixel_count;
        cls.regions.push_back(slot->id); // emplacements parcourus par identifiant croissant
    }
    std::vector<ColorClass> out;
    out.reserve(byColor.size());
    for (auto& [rgb, cls] : byColor) {
        (void)rgb;
        out.push_back(std::move(cls));
    }
    std::stable_sort(out.begin(), out.end(), [](const ColorClass& a, const ColorClass& b) {
        return a.pixel_count > b.pixel_count; // égalité : l'ordre de la map (couleur croissante)
    });
    return out;
}

ColorMergePlan plan_reduce_colors(const Segmentation& seg, std::size_t max_colors,
                                  double max_distance, bool merge_adjacent,
                                  const std::optional<Rgb>& excluded) {
    const std::vector<ColorClass> classes = classes_without(seg, excluded);
    std::vector<thread_palette::WeightedColor> colors;
    colors.reserve(classes.size());
    for (const ColorClass& cls : classes) {
        colors.push_back({cls.rgb, static_cast<double>(cls.pixel_count)});
    }
    const auto reduction = thread_palette::reduce_colors(colors, max_colors, max_distance);

    FinalColors finals = current_colors(seg, excluded);
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const Rgb target = reduction.palette[reduction.mapping[i]];
        for (const RegionId id : classes[i].regions) {
            finals[slot_index(id)] = target;
        }
    }
    return make_plan(seg, finals, merge_adjacent);
}

ColorMergePlan plan_merge_colors(const Segmentation& seg, std::span<const Rgb> sources, Rgb target,
                                 bool merge_adjacent) {
    const std::set<Rgb> wanted(sources.begin(), sources.end());
    FinalColors finals = current_colors(seg, std::nullopt);
    for (std::size_t slot = 0; slot < seg.region_slots.size(); ++slot) {
        if (seg.region_slots[slot] && wanted.contains(seg.region_slots[slot]->rgb)) {
            finals[slot] = target;
        }
    }
    return make_plan(seg, finals, merge_adjacent);
}

Result<void> apply_color_merge(Segmentation& seg, const ColorMergePlan& plan) {
    for (const auto& [id, rgb] : plan.recolors) {
        if (auto r = recolor_region(seg, id, rgb); !r) {
            return std::unexpected(r.error());
        }
    }
    for (const auto& [keep, absorb] : plan.merges) {
        if (auto r = merge_regions(seg, keep, absorb); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

} // namespace openstitch::segmentation
