// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/autodigitize.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <sstream>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/satin_planning/satin_plan.hpp"
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

template <typename ColumnLike>
BuiltSatinSection make_section(const ColumnLike& col, Micrometers density, Micrometers pullCompensation,
                               bool centerUnderlay, Micrometers maxWidth) {
    BuiltSatinSection out;
    out.params = satin_params_from_column(col, density, pullCompensation, centerUnderlay, maxWidth);
    out.strip = column_strip(col);
    return out;
}

std::vector<BuiltSatinSection> sections_from_result(const auto_satin::SatinColumnsResult& built, Micrometers density,
                                                    Micrometers pullCompensation, bool centerUnderlay,
                                                    Micrometers maxWidth) {
    std::vector<BuiltSatinSection> out;
    if (!built.parametric_columns.empty()) {
        out.reserve(built.parametric_columns.size());
        for (const auto& c : built.parametric_columns) {
            out.push_back(make_section(c, density, pullCompensation, centerUnderlay, maxWidth));
        }
    } else {
        out.reserve(built.columns.size());
        for (const auto& c : built.columns) {
            out.push_back(make_section(c, density, pullCompensation, centerUnderlay, maxWidth));
        }
    }
    return out;
}

}  // namespace

SatinBuildReport build_satin_sections(const geometry::PathSet& region,
                                      const auto_satin::SatinColumnsParameters& genParams, Micrometers density,
                                      Micrometers pullCompensation, bool centerUnderlay, Micrometers maxWidth,
                                      const std::string& warningLabel) {
    SatinBuildReport report;
    const std::string prefix = warningLabel.empty() ? std::string() : (warningLabel + " : ");

    const auto analysis = auto_satin::analyze_region(region, genParams.analysis);
    if (analysis) {
        report.whole_region_report = analysis->report;
    }

    // Planner récursif unifié (§32 du plan de refonte satin, 2026-08-14) :
    // seul point d'appel restant vers `libs/satin_planning` -- plus de
    // décomposition à une seule passe ici, `create_satin_plan` gère
    // lui-même la récursion, la mesure de couverture et la réparation de
    // résidu.
    satin_planning::SatinPlanConfig planConfig;
    planConfig.genParams = genParams;
    planConfig.density = density;
    const satin_planning::SatinPlan plan = satin_planning::create_satin_plan(region, planConfig);

    for (const auto& w : plan.warnings) {
        report.warnings.push_back(prefix + w);
    }

    report.sections.reserve(plan.regions.size());
    for (const auto& r : plan.regions) {
        if (r.depth > 0 || r.from_residual_repair) {
            report.used_sgsd = true;
        }
        auto secs = sections_from_result(r.columns, density, pullCompensation, centerUnderlay, maxWidth);
        for (auto& s : secs) report.sections.push_back(std::move(s));
    }

    // Le résidu reste une géométrie BRUTE, complète et JAMAIS filtrée --
    // c'est à l'appelant de décider quoi en faire (§12 du plan de refonte :
    // « aucun fallback silencieux vers tatami »). `create_satin_plan` ne
    // filtre déjà plus les composantes individuellement négligeables hors de
    // ce résidu (défaut réel trouvé et corrigé le 2026-08-14 : de nombreux
    // petits reliquats "négligeables" un par un peuvent s'additionner en un
    // vrai trou de plusieurs centaines de mm² sur une forme complexe, § docs/
    // source/satin.md) -- il ne fait plus que décider quelles composantes
    // méritent une TENTATIVE de réparation individuelle, jamais ce qui est
    // rapporté.
    report.unresolved_residual = plan.unresolved_residual;
    if (plan.aggregate_coverage) {
        report.aggregate_coverage = *plan.aggregate_coverage;
    }

    // `structural_gap` est un raccourci booléen pour les appelants qui ne
    // veulent pas inspecter `unresolved_residual` en détail : significatif
    // au sens AGRÉGÉ (somme de toutes les composantes manquantes, jamais une
    // composante isolée), avec le même seuil mixte fixe+proportionnel que le
    // reste du pipeline (tolère le reliquat naturel d'une pointe/jonction,
    // même minuscule, sans le confondre avec un vrai trou -- mais une SOMME
    // de nombreux petits reliquats reste, elle, correctement signalée).
    double residualAreaMm2 = 0.0;
    for (const auto& piece : report.unresolved_residual) residualAreaMm2 += net_area_um2(piece) / 1e6;
    const double totalAreaMm2 = net_area_um2(region) / 1e6;
    constexpr double kGapThresholdFloorMm2 = 1.0;
    constexpr double kGapThresholdRatio = 0.03;
    report.structural_gap = residualAreaMm2 > std::max(kGapThresholdFloorMm2, kGapThresholdRatio * totalAreaMm2);

    if (report.sections.empty()) {
        report.refusal = "aucune colonne satin n'a pu être construite";
    }

    return report;
}

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
            const document::SatinParams defaults;

            // Point d'entrée UNIQUE partagé avec les créations satin
            // manuelles (apps/desktop/main_window.cpp) : SGSD sur une région
            // branchée, repli interne sur l'appel direct sinon -- mêmes
            // garanties de couverture partout (§ build_satin_sections).
            SatinBuildReport built = build_satin_sections(main, satinOptions, defaults.density,
                                                          defaults.pull_compensation, defaults.center_underlay,
                                                          options.satin_max_width, "Région " + std::to_string(id.value));
            for (auto& w : built.warnings) {
                result.warnings.push_back(std::move(w));
            }
            std::vector<BuiltSatinSection>& sections = built.sections;
            const bool structuralGap = built.structural_gap;

            const std::size_t sectionCount = sections.size();
            if (sectionCount > 0) {
                std::vector<geometry::Path> strips;
                strips.reserve(sectionCount);
                for (std::size_t i = 0; i < sectionCount; ++i) {
                    document::EmbroideryObject section = emb;
                    section.id = ids.next();
                    section.name = "Satin region " + std::to_string(id.value) + " - section " +
                                   std::to_string(i + 1) + "/" + std::to_string(sectionCount);
                    section.params = std::move(sections[i].params);
                    result.embroideries.push_back(std::move(section));
                    strips.push_back(std::move(sections[i].strip));
                }
                madeSatin = true;
                emittedSatinNetwork = true;

                // Une branche de squelette rejetée (ex. trop large), ou une
                // sous-région ACCEPTÉE mais dont la colonne ne couvre qu'une
                // fraction de sa propre surface (ex. une boucle/contre-poinçon
                // de lettre trop ronde pour un unique ruban satin), ne doit
                // JAMAIS laisser une zone sans le moindre point. Ici,
                // l'auto-numérisation reste la voie « AutoChoice » (§24 du
                // plan de refonte satin, 2026-08-14) : classification
                // automatique, sans utilisateur interactif à qui proposer un
                // choix (§12/§23) -- le repli tatami automatique reste donc
                // justifié dans CE contexte précis, mais ne doit JAMAIS être
                // silencieux : un avertissement explicite (aire, pourcentage)
                // est toujours poussé avant de créer le repli.
                //
                // `structuralGap`/`built.unresolved_residual` (§ build_satin_
                // sections, qui délègue désormais à `satin_planning::
                // create_satin_plan`) mesurent le reliquat géométrique RÉEL
                // après une décomposition récursive et une réparation de
                // résidu déjà tentées -- jamais un simple signal structurel.
                if (structuralGap) {
                    if (built.aggregate_coverage) {
                        std::ostringstream diag;
                        diag.setf(std::ios::fixed);
                        diag.precision(1);
                        diag << "Région " << id.value << " : satin incomplet : "
                             << (built.aggregate_coverage->raw_coverage_ratio * 100.0)
                             << "% de la région couverte par le satin, "
                             << built.aggregate_coverage->missing_area_mm2
                             << " mm² comblés par un remplissage tatami de repli (classification automatique)";
                        result.warnings.push_back(diag.str());
                    }
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
