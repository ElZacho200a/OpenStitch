// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/autodigitize.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::autodigitize {

namespace {

double perimeter_um(const geometry::Path& path) {
    double p = 0.0;
    const auto& n = path.nodes;
    for (std::size_t i = 0; i < n.size(); ++i) {
        p += length_um(n[(i + 1) % n.size()].pos - n[i].pos);
    }
    return p;
}

// Aire nette d'un PathSet (extérieur moins trous), en µm².
double net_area_um2(const geometry::PathSet& set) {
    double area = std::abs(geometry::signed_area_um2(set.outer));
    for (const auto& hole : set.holes) {
        area -= std::abs(geometry::signed_area_um2(hole));
    }
    return std::max(0.0, area);
}

}  // namespace

Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg,
                                 IdGenerator<ObjectId>& ids, const AutoOptions& options) {
    AutoResult result;

    // Régions vivantes, triées par identifiant pour un résultat déterministe.
    std::vector<RegionId> regions;
    std::size_t largestSlot = 0;
    std::size_t largestCount = 0;
    for (std::size_t s = 0; s < seg.region_slots.size(); ++s) {
        if (seg.region_slots[s]) {
            regions.push_back(seg.region_slots[s]->id);
            if (seg.region_slots[s]->pixel_count > largestCount) {
                largestCount = seg.region_slots[s]->pixel_count;
                largestSlot = s;
            }
        }
    }
    if (regions.empty()) {
        return fail(ErrorCategory::UserInput, "Aucune région à numériser");
    }

    const vectorization::VectorizeOptions vecOpts{options.mm_per_px, options.simplify_tolerance};

    for (const RegionId id : regions) {
        if (options.skip_largest_region && seg.region_slots[id.value - 1] &&
            (id.value - 1) == largestSlot) {
            continue;
        }
        const auto* region = seg.find(id);
        if (region == nullptr) {
            continue;
        }
        auto sets = vectorization::vectorize_region(seg, id, vecOpts);
        if (!sets || sets->empty()) {
            continue;  // région non vectorisable : ignorée sans erreur
        }

        // Objet vectoriel (toujours créé : c'est la géométrie éditable).
        document::VectorObject vec;
        vec.id = ids.next();
        vec.name = "Région " + std::to_string(id.value);
        vec.source_region = id;
        vec.rgb = region->rgb;
        vec.paths = *sets;
        const ObjectId vecId = vec.id;
        result.vectors.push_back(std::move(vec));

        // Choix du type de point selon la forme du plus grand morceau.
        const geometry::PathSet& main = *std::max_element(
            sets->begin(), sets->end(), [](const auto& a, const auto& b) {
                return net_area_um2(a) < net_area_um2(b);
            });
        const double areaMm2 = net_area_um2(main) / 1e6;
        double perim = perimeter_um(main.outer);
        for (const auto& hole : main.holes) perim += perimeter_um(hole);
        const double meanWidthUm = perim > 0.0 ? 2.0 * net_area_um2(main) / perim : 0.0;

        document::EmbroideryObject emb;
        emb.source_vector = vecId;
        emb.rgb = region->rgb;

        const bool bigEnoughToFill = areaMm2 >= options.min_fill_area_mm2;
        // Bande fine : largeur moyenne sous la limite satin. Le moteur
        // topologique peut produire plusieurs sections ouvertes partageant la
        // meme source ; cela represente le reseau multi-rail sans casser le
        // format SatinParams historique a deux rails.
        const bool isThin =
            meanWidthUm > 0.0 && meanWidthUm <= static_cast<double>(options.satin_max_width.value);
        bool madeSatin = false;
        bool emittedSatinNetwork = false;
        if (options.use_auto_satin && bigEnoughToFill && isThin) {
            auto_satin::SatinColumnsParameters satinOptions;
            satinOptions.analysis.thresholds.max_satin_width = options.satin_max_width;
            const auto network = auto_satin::build_satin_columns(main, satinOptions);
            if (!network.columns.empty()) {
                const std::size_t sectionCount = network.columns.size();
                for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
                    const auto& column = network.columns[sectionIndex];
                    document::EmbroideryObject section = emb;
                    section.id = ids.next();
                    section.name = "Satin region " + std::to_string(id.value) + " - section " +
                                   std::to_string(sectionIndex + 1) + "/" +
                                   std::to_string(sectionCount);
                    document::SatinParams sp;
                    sp.rail_a = column.rail_a;
                    sp.rail_b = column.rail_b;
                    sp.max_width = options.satin_max_width;
                    sp.rungs.reserve(column.rungs.size());
                    for (const auto& rung : column.rungs) {
                        sp.rungs.push_back({rung.a, rung.b});
                    }
                    if (column.section_count > 1 || column.start_junction ||
                        column.end_junction) {
                        sp.topology = document::SatinSectionTopology{
                            column.section_index, column.section_count,
                            column.start_junction, column.end_junction};
                    }
                    section.params = std::move(sp);
                    result.embroideries.push_back(std::move(section));
                }
                madeSatin = true;
                emittedSatinNetwork = true;
            }
        }
        if (!madeSatin && options.use_naive_satin && bigEnoughToFill && isThin &&
            main.holes.empty()) {
            if (auto rails = stitch_generation::rails_from_contour(main.outer)) {
                document::SatinParams sp;
                sp.rail_a = rails->first;
                sp.rail_b = rails->second;
                sp.max_width = options.satin_max_width;
                emb.id = ids.next();
                emb.params = sp;
                emb.name = "Satin région " + std::to_string(id.value);
                madeSatin = true;
            }
        }
        if (!madeSatin) {
            emb.id = ids.next();
            if (bigEnoughToFill) {
                // Toute zone remplissable -> tatami (découpé sur la région, sans
                // débordement). L'orientation des fils reste éditable ensuite.
                document::TatamiParams tp;
                emb.params = tp;
                emb.name = "Remplissage région " + std::to_string(id.value);
            } else {
                // Trop petite pour un bloc : simple contour cousu.
                document::RunningStitchParams rp;
                rp.repeats = 3;
                emb.params = rp;
                emb.name = "Contour région " + std::to_string(id.value);
            }
        }
        if (!emittedSatinNetwork) {
            result.embroideries.push_back(std::move(emb));
        }
    }

    if (result.vectors.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucune région exploitable pour la numérisation automatique");
    }
    return result;
}

}  // namespace openstitch::autodigitize
