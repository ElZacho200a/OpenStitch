// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/autodigitize.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
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

// Polygone approximatif de la bande couverte par une colonne satin (rail A
// aller, rail B retour), pour calculer la zone qu'une branche de squelette
// REJETÉE (§ audit lettres, docs/source/satin.md) laisse sans point. Les
// rails sont aplatis (`geometry::flatten`) : un rail paramétrique est une
// courbe de Bézier éparse, jamais une polyligne dense.
template <typename ColumnLike>
geometry::Path column_strip(const ColumnLike& column) {
    constexpr Micrometers kFlattenTolerance{30};  // 0,03 mm : sous la résolution DST
    const auto flatA = geometry::flatten(column.rail_a, kFlattenTolerance);
    const auto flatB = geometry::flatten(column.rail_b, kFlattenTolerance);
    geometry::Path strip;
    strip.closed = true;
    strip.nodes.reserve(flatA.points.size() + flatB.points.size());
    for (const auto& pt : flatA.points) {
        strip.nodes.push_back({pt, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    for (auto it = flatB.points.rbegin(); it != flatB.points.rend(); ++it) {
        strip.nodes.push_back({*it, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return strip;
}

// RÉTRÉCIT légèrement chaque bande avant de l'utiliser comme découpe pour le
// remplissage de repli : le territoire RESTANT (donc rempli en tatami)
// déborde ainsi délibérément de `kCoverageOverlap` À L'INTÉRIEUR de la bande
// satin, plutôt que de s'arrêter pile à son bord. Un recouvrement (léger
// double-point) est anodin ; un interstice ne l'est pas -- au moindre écart
// d'arrondi entre le contour vectorisé de la région et les rails aplatis
// d'une section satin, une découpe qui s'arrête PILE au bord de la bande (ou
// pire, l'élargit avant de la soustraire, ce qui recule le remplissage de
// repli et élargit l'interstice) laisse une multitude de fins interstices le
// long de chaque couture satin/tatami (défaut trouvé par revue : la première
// version de ce correctif élargissait la bande AVANT soustraction, donc
// RÉTRÉCISSAIT le territoire de repli -- l'inverse de l'effet recherché).
// Même principe que le recouvrement de jonction (`extend_into_confluence`,
// libs/auto_satin) et la pratique standard du métier (chevaucher plutôt que
// raccorder pile, cf. audit Wilcom Hatch — Column B/miter joints, docs/source/satin.md).
constexpr Micrometers kCoverageOverlap{400};  // 0,4 mm

std::vector<geometry::Path> shrink_strips_for_cutout(const std::vector<geometry::Path>& strips) {
    std::vector<geometry::Path> out;
    out.reserve(strips.size());
    for (const auto& strip : strips) {
        const auto shrunk =
            geometry::inset_path_set(geometry::PathSet{strip, {}}, kCoverageOverlap);
        if (shrunk && !shrunk->empty()) {
            for (const auto& piece : *shrunk) out.push_back(piece.outer);
        } else {
            out.push_back(strip);  // repli : bande non rétrécie plutôt qu'absente
        }
    }
    return out;
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
                std::vector<geometry::Path> strips;
                strips.reserve(sectionCount);
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
                    strips.push_back(column_strip(column));
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

                // Une branche de squelette rejetée (ex. trop large) ne doit
                // JAMAIS laisser une zone sans le moindre point : comble ce
                // qui n'est couvert par AUCUNE section satin réussie avec un
                // remplissage tatami, découpé sur la zone exacte (jamais
                // tout le contour source, jamais un débordement sur les
                // sections satin).
                //
                // Déclenché seulement si le squelette ÉLAGUÉ compte PLUS
                // d'arêtes que de sections produites -- signal STRUCTUREL
                // direct (une arête existait, aucune colonne n'en est
                // sortie), jamais une correspondance de texte sur
                // `network.warnings` (qui porte aussi des messages purement
                // informatifs sans rapport avec une couverture manquante,
                // ex. "couture fermée : routage cyclique de quatre
                // sections" sur un anneau déjà parfaitement couvert -- déjà
                // reproduit : déclencher sur `!warnings.empty()` cassait ce
                // cas et le réseau en T normal). Un rail satin (surtout en
                // mode Parametric, ajusté en Bézier épars) approxime
                // toujours la forme source avec une légère tolérance même
                // quand TOUT réussit -- calculer le reliquat géométrique
                // dans CE cas produirait de faux positifs (plusieurs mm² de
                // reliquat "naturel" aux pointes/jonctions), d'où l'intérêt
                // du signal structurel plutôt qu'un simple calcul d'aire.
                if (network.debug.graph.edges.size() > sectionCount) {
                    // Seuil délibérément bas et INDÉPENDANT de
                    // `min_fill_area_mm2` (ce dernier répond à "cette région
                    // entière vaut-elle un remplissage plutôt qu'un simple
                    // contour ?", pas à "ce reliquat de zone déjà largement
                    // couverte mérite-t-il d'être comblé ?" -- réutiliser le
                    // même seuil laissait passer des trous de plusieurs mm²
                    // sous couvert d'être "trop petits", alors que l'objectif
                    // explicite est de ne JAMAIS laisser de zone sans point).
                    constexpr double kMinFallbackAreaMm2 = 0.5;
                    const auto leftover = geometry::subtract_polygons(main, shrink_strips_for_cutout(strips));
                    if (leftover) {
                        for (const auto& piece : *leftover) {
                            if (net_area_um2(piece) / 1e6 < kMinFallbackAreaMm2) {
                                continue;  // reliquat négligeable (bruit d'arrondi géométrique)
                            }
                            document::VectorObject fallbackVec;
                            fallbackVec.id = ids.next();
                            fallbackVec.name = "Région " + std::to_string(id.value) +
                                               " (zone non couverte par le satin)";
                            fallbackVec.source_region = id;
                            fallbackVec.rgb = region->rgb;
                            fallbackVec.paths = {piece};
                            const ObjectId fallbackVecId = fallbackVec.id;
                            result.vectors.push_back(std::move(fallbackVec));

                            document::EmbroideryObject fallback;
                            fallback.source_vector = fallbackVecId;
                            fallback.rgb = region->rgb;
                            fallback.id = ids.next();
                            fallback.params = document::TatamiParams{};
                            fallback.name = "Remplissage repli région " + std::to_string(id.value);
                            result.embroideries.push_back(std::move(fallback));
                        }
                    }
                }
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
