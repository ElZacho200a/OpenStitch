// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/topology_cleanup.hpp"

#include <algorithm>
#include <map>

namespace openstitch::ai_segmentation {

namespace {

// Composante connexe (8-connexité) de même label. `border_edges` compte,
// pour chaque label voisin distinct (4-connexité), le nombre d'arêtes de
// grille partagées avec ce voisin -- une approximation de la longueur de
// frontière partagée, utilisée à la fois pour choisir la cible de fusion
// d'un îlot et pour estimer le périmètre (l'entrée `border_edges[label]`
// comptabilise aussi les arêtes qui sortent du canevas, jamais choisie
// comme cible de fusion puisqu'elle porte le label de la composante
// elle-même).
struct Component {
    std::uint32_t label{0};
    std::size_t pixel_count{0};
    bool touches_border{false};
    std::map<std::uint32_t, std::size_t> border_edges;
};

bool is_protected(RegionId id, const std::vector<RegionId>& protectedRegions) {
    return std::find(protectedRegions.begin(), protectedRegions.end(), id) !=
           protectedRegions.end();
}

}  // namespace

Result<SegmentationValidationReport> cleanup_topology(segmentation::Segmentation& seg,
                                                       const TopologyCleanupOptions& options) {
    if (seg.width <= 0 || seg.height <= 0 || seg.labels.empty()) {
        return fail(ErrorCategory::Internal, "Carte de labels vide : rien à nettoyer");
    }
    const int w = seg.width;
    const int h = seg.height;
    const auto pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const double pxAreaMm2 = options.mm_per_px * options.mm_per_px;
    const auto index = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    };

    // 1. Composantes connexes en 8-connexité, indépendamment des slots de
    // région : deux morceaux disjoints d'une même région comptent comme deux
    // composantes -- c'est ce qui permet de détecter un îlot au sein d'une
    // région issue d'un masque non connexe.
    std::vector<int> componentOf(pixelCount, -1);
    std::vector<Component> components;
    std::vector<std::size_t> queue;
    for (std::size_t start = 0; start < pixelCount; ++start) {
        if (componentOf[start] != -1) {
            continue;
        }
        const std::uint32_t label = seg.labels[start];
        const int compId = static_cast<int>(components.size());
        Component comp;
        comp.label = label;
        queue.clear();
        queue.push_back(start);
        componentOf[start] = compId;
        std::size_t qi = 0;
        while (qi < queue.size()) {
            const std::size_t p = queue[qi++];
            const int x = static_cast<int>(p % static_cast<std::size_t>(w));
            const int y = static_cast<int>(p / static_cast<std::size_t>(w));
            ++comp.pixel_count;
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                comp.touches_border = true;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    const std::size_t np = index(nx, ny);
                    if (seg.labels[np] == label && componentOf[np] == -1) {
                        componentOf[np] = compId;
                        queue.push_back(np);
                    }
                }
            }
            const int dxs4[4] = {-1, 1, 0, 0};
            const int dys4[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dxs4[k];
                const int ny = y + dys4[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    ++comp.border_edges[label];  // sort du canevas : compte pour le périmètre
                    continue;
                }
                const std::uint32_t otherLabel = seg.labels[index(nx, ny)];
                if (otherLabel != label) {
                    ++comp.border_edges[otherLabel];
                }
            }
        }
        components.push_back(std::move(comp));
    }

    std::vector<std::uint32_t> newLabelFor(components.size());
    for (std::size_t c = 0; c < components.size(); ++c) {
        newLabelFor[c] = components[c].label;
    }

    // 2. Trous de fond : composante label==0, entièrement enclose (ne touche
    // pas le bord du canevas), sous le seuil -> comblée dans la région
    // voisine (majoritaire si plusieurs, comptée comme ambiguë).
    std::size_t tinyHoles = 0;
    std::size_t ambiguous = 0;
    for (std::size_t c = 0; c < components.size(); ++c) {
        const Component& comp = components[c];
        if (comp.label != 0 || comp.touches_border) {
            continue;
        }
        const double areaMm2 = static_cast<double>(comp.pixel_count) * pxAreaMm2;
        if (areaMm2 >= options.min_hole_area_mm2) {
            continue;
        }
        std::uint32_t best = 0;
        std::size_t bestCount = 0;
        std::size_t distinctNonBackground = 0;
        for (const auto& [neighborLabel, count] : comp.border_edges) {
            if (neighborLabel == 0) {
                continue;
            }
            ++distinctNonBackground;
            if (count > bestCount) {
                bestCount = count;
                best = neighborLabel;
            }
        }
        if (best == 0) {
            continue;  // trou sans voisin non-fond exploitable (ne devrait pas arriver)
        }
        if (distinctNonBackground > 1) {
            ++ambiguous;
        }
        newLabelFor[c] = best;
        ++tinyHoles;
    }

    // 3. Îlots de premier plan : composante label!=0 sous le seuil -> fusion
    // vers le voisin à la plus longue frontière partagée, ou retour au fond
    // si isolée. Les régions protégées sont dénombrées mais jamais touchées.
    std::size_t tinyIslands = 0;
    for (std::size_t c = 0; c < components.size(); ++c) {
        const Component& comp = components[c];
        if (comp.label == 0) {
            continue;
        }
        const double areaMm2 = static_cast<double>(comp.pixel_count) * pxAreaMm2;
        if (areaMm2 >= options.min_island_area_mm2) {
            continue;
        }
        ++tinyIslands;
        if (is_protected(RegionId{comp.label}, options.protected_regions)) {
            continue;
        }
        std::uint32_t best = 0;
        std::size_t bestCount = 0;
        for (const auto& [neighborLabel, count] : comp.border_edges) {
            if (neighborLabel == comp.label) {
                continue;
            }
            if (count > bestCount) {
                bestCount = count;
                best = neighborLabel;
            }
        }
        newLabelFor[c] = best;  // 0 si aucun voisin de premier plan : speck isolé, retour au fond
    }

    // 4. Application des relabels en une seule passe.
    for (std::size_t p = 0; p < pixelCount; ++p) {
        seg.labels[p] = newLabelFor[static_cast<std::size_t>(componentOf[p])];
    }

    // 5. Recalcul des compteurs de pixels et purge des slots vidés.
    for (auto& slot : seg.region_slots) {
        if (slot) {
            slot->pixel_count = 0;
        }
    }
    for (const std::uint32_t label : seg.labels) {
        if (label != 0 && static_cast<std::size_t>(label - 1) < seg.region_slots.size() &&
            seg.region_slots[label - 1]) {
            ++seg.region_slots[label - 1]->pixel_count;
        }
    }
    for (auto& slot : seg.region_slots) {
        if (slot && slot->pixel_count == 0) {
            slot.reset();
        }
    }

    // 6. Détection des bandes fines (informatif, non corrigé ici) : aire et
    // périmètre par région finale (post-fusions), largeur estimée par
    // 2×aire/périmètre (même formule que l'autonumérisation classique).
    std::vector<std::size_t> areaPx(seg.region_slots.size(), 0);
    std::vector<std::size_t> perimeterPx(seg.region_slots.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t label = seg.labels[index(x, y)];
            if (label == 0) {
                continue;
            }
            const std::size_t slot = label - 1;
            ++areaPx[slot];
            const int dxs4[4] = {-1, 1, 0, 0};
            const int dys4[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dxs4[k];
                const int ny = y + dys4[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h ||
                    seg.labels[index(nx, ny)] != label) {
                    ++perimeterPx[slot];
                }
            }
        }
    }
    std::size_t thinCount = 0;
    for (std::size_t slot = 0; slot < seg.region_slots.size(); ++slot) {
        if (!seg.region_slots[slot] || perimeterPx[slot] == 0) {
            continue;
        }
        const double areaMm2 = static_cast<double>(areaPx[slot]) * pxAreaMm2;
        const double perimMm = static_cast<double>(perimeterPx[slot]) * options.mm_per_px;
        const double widthMm = 2.0 * areaMm2 / perimMm;
        if (widthMm > 0.0 && widthMm < options.thin_band_max_width_mm) {
            ++thinCount;
        }
    }

    SegmentationValidationReport report;
    report.label_count = seg.region_count();
    report.component_count = components.size();
    report.tiny_island_count = tinyIslands;
    report.tiny_hole_count = tinyHoles;
    report.thin_component_count = thinCount;
    report.ambiguous_component_count = ambiguous;
    report.ready_for_vectorization = report.label_count > 0;
    return report;
}

}  // namespace openstitch::ai_segmentation
