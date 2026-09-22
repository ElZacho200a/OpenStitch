// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/autodigitize.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/satin_planning/satin_sections.hpp"
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
constexpr Micrometers kCoverageOverlap{400}; // 0,4 mm

std::vector<geometry::Path> shrink_strips_for_cutout(const std::vector<geometry::Path>& strips) {
    std::vector<geometry::Path> out;
    out.reserve(strips.size());
    for (const auto& strip : strips) {
        const auto shrunk =
            geometry::inset_path_set(geometry::PathSet{strip, {}}, kCoverageOverlap);
        if (shrunk && !shrunk->empty()) {
            for (const auto& piece : *shrunk)
                out.push_back(piece.outer);
        } else {
            out.push_back(strip); // repli : bande non rétrécie plutôt qu'absente
        }
    }
    return out;
}

// Source d'un morceau à classifier : soit une région de segmentation
// (`auto_digitize`), soit un objet vectoriel déjà existant, ex. import SVG
// direct (`auto_digitize_vectors`, § "skip segmentation"). `label` sert de
// base à tous les noms/avertissements produits ci-dessous ("Région 3" ou
// "Import SVG 3" selon la source) -- seule différence de comportement entre
// les deux chemins, tout le reste (classification satin/tatami/contour,
// repli sur reliquat) est PARTAGÉ, jamais dupliqué.
struct RegionSource {
    ObjectId vec_id;
    std::array<std::uint8_t, 3> rgb;
    std::string label;
    // Présent seulement pour une région de segmentation -- reporté sur tout
    // vecteur de repli créé ci-dessous, pour que les appelants puissent
    // regrouper repli et couverture satin par région d'origine (ex.
    // tests/integration/test_pipeline.cpp). Absent pour un objet vectoriel
    // déjà existant (import SVG direct) : pas de région à relier.
    std::optional<RegionId> region_id;
};

// Classification AutoChoice (§24) + construction des objets de broderie pour
// UN morceau déjà vectorisé (`main`, le plus grand sous-chemin d'une région
// ou d'un objet vectoriel importé). Ajoute à `result.embroideries` (et, pour
// un reliquat satin non couvert, à `result.vectors`) -- ne touche jamais à
// l'objet vectoriel source lui-même (déjà ajouté par l'appelant le cas
// échéant).
void classify_and_build_embroidery(const geometry::PathSet& main, const RegionSource& source,
                                   IdGenerator<ObjectId>& ids, const AutoOptions& options,
                                   AutoResult& result) {
    const double areaMm2 = net_area_um2(main) / 1e6;
    double perim = perimeter_um(main.outer);
    for (const auto& hole : main.holes) perim += perimeter_um(hole);
    const double meanWidthUm = perim > 0.0 ? 2.0 * net_area_um2(main) / perim : 0.0;

    document::EmbroideryObject emb;
    emb.source_vector = source.vec_id;
    emb.rgb = source.rgb;
    // §21/§24 : classification AUTOMATIQUE, sans utilisateur interactif
    // à qui proposer un choix -- déjà la valeur par défaut, fixée ici
    // explicitement plutôt que silencieusement héritée.
    emb.intent = document::EmbroideryIntent::AutoChoice;

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
        satin_planning::SatinBuildReport built = satin_planning::build_satin_sections(
            main, satinOptions, defaults.density, defaults.pull_compensation, defaults.center_underlay,
            options.satin_max_width, source.label);
        for (auto& w : built.warnings) {
            result.warnings.push_back(std::move(w));
        }
        std::vector<satin_planning::BuiltSatinSection>& sections = built.sections;
        const bool structuralGap = built.structural_gap;

        const std::size_t sectionCount = sections.size();
        if (sectionCount > 0) {
            std::vector<geometry::Path> strips;
            strips.reserve(sectionCount);
            for (std::size_t i = 0; i < sectionCount; ++i) {
                document::EmbroideryObject section = emb;
                section.id = ids.next();
                section.name = "Satin " + source.label + " - section " + std::to_string(i + 1) +
                              "/" + std::to_string(sectionCount);
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
                    diag << source.label << " : satin incomplet : "
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
                        fallbackVec.name = source.label + " (zone non couverte par le satin)";
                        fallbackVec.source_region = source.region_id;
                        fallbackVec.rgb = source.rgb;
                        fallbackVec.paths = {piece};
                        const ObjectId fallbackVecId = fallbackVec.id;
                        result.vectors.push_back(std::move(fallbackVec));

                        document::EmbroideryObject fallback;
                        fallback.source_vector = fallbackVecId;
                        fallback.rgb = source.rgb;
                        fallback.id = ids.next();
                        fallback.params = document::TatamiParams{};
                        fallback.intent = document::EmbroideryIntent::AutoChoice;
                        fallback.name = "Remplissage repli " + source.label;
                        result.embroideries.push_back(std::move(fallback));
                    }
                }
            }
        }
    }
    if (!madeSatin && options.use_naive_satin && bigEnoughToFill && isThin && main.holes.empty()) {
        if (auto rails = stitch_generation::rails_from_contour(main.outer)) {
            document::SatinParams sp;
            sp.rail_a = rails->first;
            sp.rail_b = rails->second;
            sp.max_width = options.satin_max_width;
            emb.id = ids.next();
            emb.params = sp;
            emb.name = "Satin " + source.label;
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
            emb.name = "Remplissage " + source.label;
        } else {
            // Trop petite pour un bloc : simple contour cousu.
            document::RunningStitchParams rp;
            rp.repeats = 3;
            emb.params = rp;
            emb.name = "Contour " + source.label;
        }
    }
    if (!emittedSatinNetwork) {
        result.embroideries.push_back(std::move(emb));
    }
}

// Le plus grand sous-chemin (par aire nette) d'un ensemble de PathSet --
// même critère que `auto_digitize` utilisait déjà pour choisir la forme
// dominante d'une région fragmentée (ex. rétrécissement sous 1 px de
// rasterisation) : ce choix se généralise tel quel à un objet vectoriel
// importé qui contiendrait plusieurs sous-chemins disjoints.
const geometry::PathSet& largest_piece(const std::vector<geometry::PathSet>& sets) {
    return *std::max_element(sets.begin(), sets.end(), [](const auto& a, const auto& b) {
        return net_area_um2(a) < net_area_um2(b);
    });
}

// Paires de régions voisines (identifiants triés), cf. `region_adjacency`.
using RegionPairs = std::set<std::pair<std::uint64_t, std::uint64_t>>;

RegionPairs adjacent_pairs(const segmentation::Segmentation& seg) {
    RegionPairs out;
    for (const auto& b : segmentation::region_adjacency(seg)) {
        out.insert({b.a.value, b.b.value});
    }
    return out;
}

// Orientation de rangées ramenée dans [0, pi).
double normalize_row_angle(double a) {
    a = std::fmod(a, std::numbers::pi);
    return a < 0.0 ? a + std::numbers::pi : a;
}

// Écart entre deux orientations de rangées, modulo pi (dans [0, pi/2]).
double row_angle_gap(double a, double b) {
    const double d = std::fmod(std::abs(a - b), std::numbers::pi);
    return std::min(d, std::numbers::pi - d);
}

// Lot B (audit marine plein cadre, 2026-09-22) : angle et sous-couche de
// chaque tatami créé par l'auto-numérisation, plutôt que `TatamiParams{}`
// (tout à 0°, sans sous-couche). Post-passe sur le résultat complet :
// l'angle d'un tatami dépend de ses VOISINS, qui ne sont connus qu'une fois
// toutes les régions classées (satin/tatami/contour, replis compris).
// `adjacency` absent (import SVG direct, aucune segmentation) : orientation
// naturelle seule, sans règle de voisinage.
void configure_tatami_fills(AutoResult& result, const std::vector<document::VectorObject>& inputs,
                            const RegionPairs* adjacency, const AutoOptions& options) {
    const auto findVector = [&](ObjectId id) -> const document::VectorObject* {
        const std::array<const std::vector<document::VectorObject>*, 2> lists{&result.vectors,
                                                                              &inputs};
        for (const auto* list : lists) {
            for (const auto& v : *list) {
                if (v.id == id) {
                    return &v;
                }
            }
        }
        return nullptr;
    };

    struct Fill {
        std::size_t index; // dans result.embroideries
        double areaMm2;
        double natural; // angle naturel (axe principal ou isotrope)
        std::optional<RegionId> region;
        double angle{0.0};
        bool assigned{false};
    };
    std::vector<Fill> fills;
    for (std::size_t i = 0; i < result.embroideries.size(); ++i) {
        const auto& e = result.embroideries[i];
        if (!e.is_tatami()) {
            continue;
        }
        const document::VectorObject* vec = findVector(e.source_vector);
        if (vec == nullptr) {
            continue;
        }
        double area = 0.0;
        for (const auto& set : vec->paths) {
            area += geometry::path_set_area_um2(set) / 1e6;
        }
        const auto axis = geometry::principal_axis(vec->paths);
        const double natural = axis && axis->anisotropy >= options.fill_isotropy_ratio
                                   ? axis->angle.radians
                                   : options.isotropic_fill_angle.radians;
        fills.push_back({i, area, normalize_row_angle(natural), vec->source_region});
    }

    if (options.auto_fill_underlay) {
        for (const Fill& f : fills) {
            auto& p = std::get<document::TatamiParams>(result.embroideries[f.index].params);
            p.underlay_edge = f.areaMm2 >= options.underlay_edge_min_area_mm2;
            p.underlay_parallel = f.areaMm2 >= options.underlay_parallel_min_area_mm2;
        }
    }
    if (!options.auto_fill_angle) {
        return;
    }

    // Les plus grandes régions fixent leur angle d'abord : lors d'un conflit
    // avec un voisin déjà placé, c'est toujours la plus petite qui cède.
    // Ordre total déterministe (aire, puis identifiant d'objet).
    std::vector<std::size_t> order(fills.size());
    for (std::size_t k = 0; k < order.size(); ++k) {
        order[k] = k;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (fills[a].areaMm2 != fills[b].areaMm2) {
            return fills[a].areaMm2 > fills[b].areaMm2;
        }
        return result.embroideries[fills[a].index].id.value <
               result.embroideries[fills[b].index].id.value;
    });
    const auto neighbours = [&](const Fill& a, const Fill& b) {
        if (adjacency == nullptr || !a.region || !b.region) {
            return false;
        }
        const std::uint64_t x = a.region->value;
        const std::uint64_t y = b.region->value;
        return x == y || adjacency->contains({std::min(x, y), std::max(x, y)});
    };
    const double gap = options.min_neighbor_fill_angle_gap.radians;
    const double step = std::max(1e-3, options.fill_angle_search_step.radians);
    const int maxK = static_cast<int>(std::ceil(std::numbers::pi / 2.0 / step));
    for (const std::size_t k : order) {
        Fill& f = fills[k];
        std::vector<double> placed;
        for (const Fill& g : fills) {
            if (&g != &f && g.assigned && neighbours(f, g)) {
                placed.push_back(g.angle);
            }
        }
        const auto minGap = [&](double a) {
            double m = std::numbers::pi;
            for (const double p : placed) {
                m = std::min(m, row_angle_gap(a, p));
            }
            return m;
        };
        // Candidats du plus proche au plus éloigné de l'angle naturel
        // (+pas avant -pas) ; le premier qui respecte l'écart gagne, sinon
        // celui qui maximise l'écart minimal.
        double best = f.natural;
        double bestGap = -1.0;
        for (int i = 0; i <= 2 * maxK; ++i) {
            const int m = (i + 1) / 2 * (i % 2 == 1 ? 1 : -1);
            const double cand = normalize_row_angle(f.natural + m * step);
            const double g = minGap(cand);
            if (g >= gap - 1e-9) {
                best = cand;
                break;
            }
            if (g > bestGap + 1e-12) {
                bestGap = g;
                best = cand;
            }
        }
        f.angle = best;
        f.assigned = true;
        std::get<document::TatamiParams>(result.embroideries[f.index].params).angle = Angle{best};
    }
}

}  // namespace

Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg, IdGenerator<ObjectId>& ids,
                                 const AutoOptions& options) {
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
            continue; // région non vectorisable : ignorée sans erreur
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

        // Choix du type de point selon la forme du plus grand morceau --
        // logique PARTAGÉE avec `auto_digitize_vectors` (§ classify_and_
        // build_embroidery ci-dessus), une région de segmentation n'étant
        // qu'une des deux sources possibles d'un morceau déjà vectorisé.
        const RegionSource source{vecId, region->rgb, "Région " + std::to_string(id.value), id};
        classify_and_build_embroidery(largest_piece(*sets), source, ids, options, result);
    }

    if (result.vectors.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucune région exploitable pour la numérisation automatique");
    }
    const RegionPairs adjacency = adjacent_pairs(seg);
    configure_tatami_fills(result, {}, &adjacency, options);
    return result;
}

Result<AutoResult> auto_digitize_vectors(const std::vector<document::VectorObject>& vectors,
                                         IdGenerator<ObjectId>& ids, const AutoOptions& options) {
    if (vectors.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun objet vectoriel à numériser");
    }

    AutoResult result;
    for (const auto& vec : vectors) {
        if (vec.paths.empty()) {
            continue;  // objet vectoriel vide : ignoré sans erreur, comme une région non vectorisable
        }
        // Aucun nouvel objet vectoriel créé pour la géométrie d'entrée : elle
        // existe déjà (import SVG direct, § "skip segmentation") -- seul un
        // éventuel reliquat satin non couvert en ajoute un (repli tatami, cf.
        // classify_and_build_embroidery).
        const RegionSource source{vec.id, vec.rgb, vec.name, std::nullopt};
        classify_and_build_embroidery(largest_piece(vec.paths), source, ids, options, result);
    }

    if (result.embroideries.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucun objet vectoriel exploitable pour la numérisation automatique");
    }
    configure_tatami_fills(result, vectors, nullptr, options);
    return result;
}

}  // namespace openstitch::autodigitize
