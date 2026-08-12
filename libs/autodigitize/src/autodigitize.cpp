// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/autodigitize.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

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

    // Couleur de fond présumée : celle du plus gros morceau (§ audit projet
    // réel, logo circulaire sans canal alpha — cf. AutoOptions::
    // skip_largest_region). Un fond peut être fragmenté en PLUSIEURS régions
    // DISJOINTES de la même couleur (ex. les zones hors d'un motif rond
    // inscrit dans une image carrée, coupées par le motif lui-même) : sur ce
    // projet, trois régions blanches distinctes totalisaient 87,9 % des
    // pixels segmentés, la plus grosse seule n'en représentant que 40,9 %.
    // Exclure seulement le plus gros MORCEAU laissait les autres fragments du
    // même fond se faire numériser comme de vrais objets ; exclure toute
    // région de cette couleur EXACTE couvre le fond dans son ensemble.
    const std::optional<std::array<std::uint8_t, 3>> backgroundRgb =
        options.skip_largest_region && seg.region_slots[largestSlot]
            ? std::optional{seg.region_slots[largestSlot]->rgb}
            : std::nullopt;

    const vectorization::VectorizeOptions vecOpts{options.mm_per_px, options.simplify_tolerance};

    for (const RegionId id : regions) {
        const auto* region = seg.find(id);
        if (region == nullptr) {
            continue;
        }
        if (backgroundRgb && region->rgb == *backgroundRgb) {
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
            // Mode Parametric (rails Bézier épars) préféré : jonctions plus
            // propres, validé visuellement sur 6 formes (cf.
            // docs/source/satin.md, § Objets satin paramétriques). Anneaux et
            // cas refusés retombent automatiquement sur Legacy À L'INTÉRIEUR
            // de build_satin_columns (`columns` peuplé au lieu de
            // `parametric_columns`) — on lit simplement celui des deux qui a
            // été rempli, comme les créations satin manuelles côté
            // apps/desktop.
            auto_satin::SatinColumnsParameters satinOptions;
            satinOptions.analysis.thresholds.max_satin_width = options.satin_max_width;
            satinOptions.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
            const auto network = auto_satin::build_satin_columns(main, satinOptions);
            for (const auto& w : network.warnings) {
                result.warnings.push_back("Région " + std::to_string(id.value) + " : " + w);
            }
            const bool useParametric = !network.parametric_columns.empty();
            const std::size_t sectionCount =
                useParametric ? network.parametric_columns.size() : network.columns.size();
            if (sectionCount > 0) {
                const document::SatinParams defaults;
                const auto addSection = [&](const auto& column, std::size_t sectionIndex) {
                    document::EmbroideryObject section = emb;
                    section.id = ids.next();
                    section.name = "Satin region " + std::to_string(id.value) + " - section " +
                                   std::to_string(sectionIndex + 1) + "/" +
                                   std::to_string(sectionCount);
                    section.params = satin_params_from_column(column, defaults.density,
                                                              defaults.pull_compensation,
                                                              defaults.center_underlay,
                                                              options.satin_max_width);
                    result.embroideries.push_back(std::move(section));
                };
                if (useParametric) {
                    for (std::size_t i = 0; i < sectionCount; ++i) {
                        addSection(network.parametric_columns[i], i);
                    }
                } else {
                    for (std::size_t i = 0; i < sectionCount; ++i) {
                        addSection(network.columns[i], i);
                    }
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
