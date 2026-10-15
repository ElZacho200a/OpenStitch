// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/core/parallel.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <utility>

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/thread_palette/color_reduction.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::autodigitize {

namespace {

// « Limiter à N fils » (HP-THR-011) : fusionne les couleurs de régions les plus
// proches (CIEDE2000) jusqu'à n'en garder que `max_threads`, la teinte de la
// plus grosse région l'emportant. Les régions du fond ignoré ne comptent pas et
// gardent leur couleur. Recolore seulement : la géométrie des régions est
// intacte (les objets de même couleur restent contigus au routage).
void limit_thread_count(segmentation::Segmentation& seg, std::size_t maxThreads,
                        const std::optional<std::array<std::uint8_t, 3>>& background) {
    if (maxThreads == 0) {
        return;
    }
    std::vector<segmentation::Region*> regions;
    std::vector<thread_palette::WeightedColor> colors;
    for (auto& slot : seg.region_slots) {
        if (!slot || (background && slot->rgb == *background)) {
            continue;
        }
        regions.push_back(&*slot);
        colors.push_back({slot->rgb, static_cast<double>(slot->pixel_count)});
    }
    const auto reduction = thread_palette::reduce_colors(colors, maxThreads);
    for (std::size_t i = 0; i < regions.size(); ++i) {
        regions[i]->rgb = reduction.palette[reduction.mapping[i]];
    }
}

// Aire nette d'un PathSet (extérieur moins trous), en µm².
double net_area_um2(const geometry::PathSet& set) {
    double area = std::abs(geometry::signed_area_um2(set.outer));
    for (const auto& hole : set.holes) {
        area -= std::abs(geometry::signed_area_um2(hole));
    }
    return std::max(0.0, area);
}

// La vectorisation trace les contours par les centres des pixels de bord : le
// polygone perd un demi-pixel par arête de frontière. Le seuil de fragment
// (Lot D) porte sur cette aire vectorisée, celle qui décide du type de point
// (audit marine : des régions de 3 mm² en pixels donnaient des contours de
// 2,5 mm²). Propriété de la vectorisation, pas un réglage.
constexpr double kVectorizedBoundaryLoss = 0.5;

// Source d'un morceau à classifier : soit une région de segmentation
// (`auto_digitize`), soit un objet vectoriel déjà existant, ex. import SVG
// direct (`auto_digitize_vectors`, § "skip segmentation"). `label` sert de
// base à tous les noms produits ci-dessous ("Région 3" ou "Import SVG 3"
// selon la source) -- seule différence de comportement entre les deux chemins,
// tout le reste (classification tatami/contour) est PARTAGÉ, jamais dupliqué.
struct RegionSource {
    ObjectId vec_id;
    std::array<std::uint8_t, 3> rgb;
    std::string label;
};

// Résultat de l'essai de satin sur une région.
struct SatinChoice {
    bool satin{false};
    bool compact_axis{false}; // forme compacte : axe synthétique (cordes parallèles)
};

// Une région est satinable si le satin par squelette la couvre réellement : on
// ESSAIE la génération (déterministe) et on mesure la couverture plutôt que de
// deviner depuis la seule forme. Deux voies :
//  1. bande étroite et allongée (anneau, ruban, arc) : squelette normal ;
//  2. forme compacte sans trou (disque, pétale) : axe synthétique, si sa largeur
//     reste sous `satin_compact_max_width`.
// Tout échec = pas de satin (repli tatami).
SatinChoice choose_satin(const geometry::PathSet& region, const AutoOptions& options) {
    if (region.holes.size() > 1) {
        return {}; // plusieurs trous : cas non couvert par le satin squelette
    }
    auto_satin::SkeletonSatinParameters params;
    const auto analysis = auto_satin::analyze_region(region, params.analysis);
    if (!analysis) {
        return {};
    }
    const auto& r = analysis->report;
    // Le rapport de satinabilité ignore les trous dans le périmètre et n'a pas
    // d'axe pour un anneau (squelette sans arête) : on mesure ici la bande
    // elle-même. Une bande a deux bords de longueur ~L, d'où L = périmètre
    // total / 2 ; W = diamètre inscrit maximal ; largeur moyenne = aire / L.
    double perimeterUm = 0.0;
    const auto addPerimeter = [&perimeterUm](const geometry::Path& path) {
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            perimeterUm +=
                length_um(path.nodes[(i + 1) % path.nodes.size()].pos - path.nodes[i].pos);
        }
    };
    addPerimeter(region.outer);
    for (const auto& hole : region.holes) {
        addPerimeter(hole);
    }
    const double lengthUm = perimeterUm / 2.0;
    const double widthUm = r.maximum_width_mm * 1000.0;
    const double meanWidthUm = lengthUm > 0.0 ? r.area_mm2 * 1e6 / lengthUm : 0.0;
    const double minWidth = static_cast<double>(params.analysis.thresholds.min_satin_width.value);
    const double maxWidth = static_cast<double>(options.satin_max_width.value);
    const double minElongation = params.analysis.thresholds.min_elongation;

    const auto covered = [&](bool compact) {
        auto_satin::SkeletonSatinParameters p;
        p.measure_coverage = true;
        p.compact_axis = compact;
        const auto generated = auto_satin::generate_skeleton_satin(region, p);
        return generated && !generated->columns.empty() &&
               generated->diagnostics.coverage_measured &&
               generated->diagnostics.coverage_ratio >= options.satin_min_coverage;
    };

    const bool bandLike =
        meanWidthUm >= minWidth && widthUm <= maxWidth && lengthUm >= minElongation * widthUm;
    if (bandLike && covered(false)) {
        return {true, false};
    }
    const double compactMax = static_cast<double>(options.satin_compact_max_width.value);
    if (!bandLike && compactMax > 0.0 && region.holes.empty() && widthUm >= minWidth &&
        widthUm <= compactMax && covered(true)) {
        return {true, true};
    }
    return {};
}

// Classification AutoChoice (§24) + construction des objets de broderie pour
// UN morceau déjà vectorisé (`main`, le plus grand sous-chemin d'une région
// ou d'un objet vectoriel importé). Ajoute à `result.embroideries` -- ne touche
// jamais à l'objet vectoriel source lui-même (déjà ajouté par l'appelant le cas
// échéant). Règle métier : l'auto-broderie ne produit que tatami ou contour ;
// le satin n'est choisi que pour les bandes étroites (`satin_bands`).
// Essai de satin d'un morceau : la SEULE partie coûteuse de la classification (génération
// d'essai + mesure de couverture). Fonction pure de (morceau, options) : calculée en parallèle
// par les appelants, puis passée à `classify_and_build_embroidery`.
SatinChoice satin_choice_for(const geometry::PathSet& main, const AutoOptions& options) {
    const double areaMm2 = net_area_um2(main) / 1e6;
    return areaMm2 >= options.min_fill_area_mm2 && options.satin_bands ? choose_satin(main, options)
                                                                       : SatinChoice{};
}

void classify_and_build_embroidery(const geometry::PathSet& main, const RegionSource& source,
                                   IdGenerator<ObjectId>& ids, const AutoOptions& options,
                                   const SatinChoice& satin, AutoResult& result) {
    const double areaMm2 = net_area_um2(main) / 1e6;

    document::EmbroideryObject emb;
    emb.source_vector = source.vec_id;
    emb.rgb = source.rgb;
    // §21/§24 : classification AUTOMATIQUE, sans utilisateur interactif
    // à qui proposer un choix -- déjà la valeur par défaut, fixée ici
    // explicitement plutôt que silencieusement héritée.
    emb.intent = document::EmbroideryIntent::AutoChoice;

    emb.id = ids.next();
    if (satin.satin) {
        // Bande étroite (anneau, ruban, arc) ou petit disque -> auto-satin.
        document::AutoSatinParams sp;
        sp.compact_axis = satin.compact_axis;
        emb.params = sp;
        emb.name = "Satin " + source.label;
    } else if (areaMm2 >= options.min_fill_area_mm2) {
        // Toute zone remplissable -> tatami. L'orientation des fils reste
        // éditable ensuite.
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
    result.embroideries.push_back(std::move(emb));
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
// toutes les régions classées (tatami/contour).
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

    for (const Fill& f : fills) {
        std::get<document::TatamiParams>(result.embroideries[f.index].params).hidden_underpath =
            options.fill_hidden_underpath;
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

// Élargit (delta > 0) chaque PathSet de `delta` ; un élargissement qui
// échouerait garde la forme brute (jamais de perte de surface).
std::vector<geometry::PathSet> dilate(const std::vector<geometry::PathSet>& sets,
                                      Micrometers delta) {
    std::vector<geometry::PathSet> out;
    for (const auto& set : sets) {
        if (auto grown = geometry::inset_path_set(set, Micrometers{-delta.value});
            grown && !grown->empty()) {
            out.insert(out.end(), grown->begin(), grown->end());
        } else {
            out.push_back(set);
        }
    }
    return out;
}

// Lot C (audit marine plein cadre) : chaque tatami rentré de `inset` sur
// TOUS ses bords laissait, avec la simplification indépendante de chaque
// contour, un interstice visible le long de chaque frontière partagée. Ici,
// la surface remplie devient
//     (région rentrée de inset) ∪ (région élargie de fill_overlap ∩ voisins élargis)
// soit un débord de `fill_overlap` UNIQUEMENT du côté des régions voisines
// brodées : les bords extérieurs du motif et le contact avec le fond ignoré
// (région sans objet vectoriel, donc jamais voisine brodée) gardent le
// retrait. Les voisins sont élargis eux aussi : la vectorisation trace les
// contours par les CENTRES des pixels de bord (vectorize.cpp), donc deux
// régions voisines sont séparées d'un pixel entier. Élargir les deux côtés
// de `fill_overlap + ½ pixel` place la bande à ±fill_overlap autour de la
// VRAIE frontière (le bord commun des pixels). La surface est portée par l'objet
// vectoriel (éditable) et `inset` passe à 0 pour ne pas rentrer deux fois.
// Toutes les surfaces sont calculées sur la géométrie d'ORIGINE avant d'être
// appliquées (le résultat ne dépend pas de l'ordre de traitement).
void overlap_neighbor_fills(AutoResult& result,
                            const std::map<std::uint64_t, ObjectId>& regionVector,
                            const std::vector<segmentation::RegionBorder>& borders,
                            const AutoOptions& options) {
    if (options.fill_overlap.value <= 0) {
        return;
    }
    std::map<std::uint64_t, std::vector<std::uint64_t>> neighbours;
    for (const auto& b : borders) {
        if (regionVector.contains(b.a.value) && regionVector.contains(b.b.value)) {
            neighbours[b.a.value].push_back(b.b.value);
            neighbours[b.b.value].push_back(b.a.value);
        }
    }
    const Micrometers grow{
        options.fill_overlap.value +
        static_cast<std::int32_t>(std::lround(to_micrometers(options.mm_per_px).value / 2.0))};
    const auto vectorIndex = [&](ObjectId id) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < result.vectors.size(); ++i) {
            if (result.vectors[i].id == id) {
                return i;
            }
        }
        return std::nullopt;
    };

    struct Change {
        std::size_t vec;
        std::size_t emb;
        std::vector<geometry::PathSet> paths;
    };
    std::vector<Change> changes;
    for (std::size_t e = 0; e < result.embroideries.size(); ++e) {
        const auto& emb = result.embroideries[e];
        if (!emb.is_tatami()) {
            continue;
        }
        const auto vi = vectorIndex(emb.source_vector);
        if (!vi) {
            continue;
        }
        const auto& vec = result.vectors[*vi];
        // Seul l'objet vectoriel PRINCIPAL d'une région : les vecteurs importés
        // ou générés ailleurs n'ont pas de voisinage de segmentation fiable.
        if (!vec.source_region || !regionVector.contains(vec.source_region->value) ||
            regionVector.at(vec.source_region->value) != vec.id) {
            continue;
        }
        const auto it = neighbours.find(vec.source_region->value);
        if (it == neighbours.end()) {
            continue;
        }
        std::vector<geometry::PathSet> around;
        for (const std::uint64_t n : it->second) {
            if (const auto ni = vectorIndex(regionVector.at(n))) {
                const auto& np = result.vectors[*ni].paths;
                around.insert(around.end(), np.begin(), np.end());
            }
        }
        const auto band =
            geometry::intersect_polygons(dilate(vec.paths, grow), dilate(around, grow));
        if (!band || band->empty()) {
            continue;
        }
        const auto& params = std::get<document::TatamiParams>(emb.params);
        std::vector<geometry::PathSet> parts;
        for (const auto& set : vec.paths) {
            if (auto in = geometry::inset_path_set(set, params.inset); in && !in->empty()) {
                parts.insert(parts.end(), in->begin(), in->end());
            } else {
                parts.push_back(set); // même repli que generate_tatami
            }
        }
        parts.insert(parts.end(), band->begin(), band->end());
        auto merged = geometry::union_polygons(parts);
        if (!merged || merged->empty()) {
            continue;
        }
        changes.push_back({*vi, e, std::move(*merged)});
    }
    for (auto& c : changes) {
        result.vectors[c.vec].paths = std::move(c.paths);
        std::get<document::TatamiParams>(result.embroideries[c.emb].params).inset = Micrometers{0};
    }
}

// Lot C : ordre de couture en couches (optimization::LayeredColorThenProximity).
// Unité d'ordre = suite CONTIGUË d'objets de même `source_vector`.
void order_in_layers(AutoResult& result, const std::vector<document::VectorObject>& inputs,
                     const AutoOptions& options) {
    if (!options.order_by_layers || result.embroideries.size() < 2) {
        return;
    }
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
    std::vector<std::pair<std::size_t, std::size_t>> units; // [début, fin)
    for (std::size_t i = 0; i < result.embroideries.size(); ++i) {
        if (!units.empty() && result.embroideries[units.back().first].source_vector ==
                                  result.embroideries[i].source_vector) {
            units.back().second = i + 1;
        } else {
            units.emplace_back(i, i + 1);
        }
    }
    std::vector<optimization::OrderItem> items;
    items.reserve(units.size());
    for (std::size_t u = 0; u < units.size(); ++u) {
        const auto& first = result.embroideries[units[u].first];
        optimization::OrderItem item;
        item.id = ObjectId{u + 1}; // identifiant d'unité (local à cette fonction)
        item.rgb = first.rgb;
        if (const auto* vec = findVector(first.source_vector)) {
            std::int64_t x0 = std::numeric_limits<std::int64_t>::max(), y0 = x0;
            std::int64_t x1 = std::numeric_limits<std::int64_t>::min(), y1 = x1;
            for (const auto& set : vec->paths) {
                item.area_mm2 += geometry::path_set_area_um2(set) / 1e6;
                for (const auto& n : set.outer.nodes) {
                    x0 = std::min<std::int64_t>(x0, n.pos.x.value);
                    y0 = std::min<std::int64_t>(y0, n.pos.y.value);
                    x1 = std::max<std::int64_t>(x1, n.pos.x.value);
                    y1 = std::max<std::int64_t>(y1, n.pos.y.value);
                }
            }
            if (x0 <= x1) {
                item.centroid = Vec2um{Micrometers{static_cast<std::int32_t>((x0 + x1) / 2)},
                                       Micrometers{static_cast<std::int32_t>((y0 + y1) / 2)}};
            }
        }
        items.push_back(item);
    }
    const auto order =
        optimization::optimize_order(items, optimization::OrderStrategy::LayeredColorThenProximity,
                                     {.layer_large_area_ratio = options.layer_large_area_ratio});
    std::vector<document::EmbroideryObject> reordered;
    reordered.reserve(result.embroideries.size());
    for (const ObjectId unit : order) {
        const auto [b, e] = units[unit.value - 1];
        for (std::size_t i = b; i < e; ++i) {
            reordered.push_back(std::move(result.embroideries[i]));
        }
    }
    result.embroideries = std::move(reordered);
}

} // namespace

Result<AutoResult> auto_digitize(const segmentation::Segmentation& input,
                                 IdGenerator<ObjectId>& ids, const AutoOptions& options) {
    AutoResult result;

    // Régions vivantes, triées par identifiant pour un résultat déterministe.
    std::vector<RegionId> regions;
    std::size_t largestSlot = 0;
    std::size_t largestCount = 0;
    for (std::size_t s = 0; s < input.region_slots.size(); ++s) {
        if (input.region_slots[s]) {
            regions.push_back(input.region_slots[s]->id);
            if (input.region_slots[s]->pixel_count > largestCount) {
                largestCount = input.region_slots[s]->pixel_count;
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
        options.skip_largest_region && input.region_slots[largestSlot]
            ? std::optional{input.region_slots[largestSlot]->rgb}
            : std::nullopt;

    // Fragments (Lot D, audit marine plein cadre) : sur une COPIE de travail
    // (la segmentation de l'utilisateur reste intacte), retire d'abord les
    // isthmes et lamelles plus étroits que `min_feature_width_mm`, puis
    // fusionne chaque région sous `min_region_area_mm2` avec la voisine de
    // plus longue frontière -- au lieu d'en faire un contour point triple.
    // Le fond ignoré n'absorbe jamais : un fragment qui n'a que lui pour
    // voisin reste une région isolée (cousue en contour plus bas). Seuils en
    // mm/mm², convertis avec mm_per_px : indépendants de la résolution.
    segmentation::Segmentation seg = input;
    const double mmPerPx = options.mm_per_px.value;
    if (options.min_feature_width_mm > 0.0 && mmPerPx > 0.0) {
        segmentation::remove_thin_parts(
            seg, static_cast<int>(std::lround(options.min_feature_width_mm / mmPerPx)));
    }
    if (options.island_area_mm2 > 0.0 && mmPerPx > 0.0) {
        segmentation::merge_small_regions(
            seg, static_cast<std::size_t>(std::ceil(options.island_area_mm2 / (mmPerPx * mmPerPx))),
            backgroundRgb, kVectorizedBoundaryLoss, segmentation::MergeTarget::ClosestColour,
            options.island_max_delta_e);
    }
    if (options.min_region_area_mm2 > 0.0 && mmPerPx > 0.0) {
        segmentation::merge_small_regions(
            seg,
            static_cast<std::size_t>(std::ceil(options.min_region_area_mm2 / (mmPerPx * mmPerPx))),
            backgroundRgb, kVectorizedBoundaryLoss);
    }

    limit_thread_count(seg, options.max_threads, backgroundRgb);

    const vectorization::VectorizeOptions vecOpts{options.mm_per_px, options.simplify_tolerance,
                                                  options.curve_tolerance};
    // Objet vectoriel principal de chaque région numérisée (Lot C) : une
    // région absente (fond ignoré, non vectorisable) n'est jamais une voisine
    // brodée.
    std::map<std::uint64_t, ObjectId> regionVector;

    // Phase parallèle : vectorisation et essai de satin de chaque région, les deux étapes
    // coûteuses. Chaque tâche lit la segmentation (constante ici) et n'écrit que dans sa case ;
    // les identifiants sont attribués ensuite, en séquence et dans l'ordre des régions : le
    // résultat ne dépend ni du nombre de fils ni de leur ordonnancement.
    struct Vectorized {
        std::vector<geometry::PathSet> sets; // vide : région ignorée
        SatinChoice satin;
    };
    const segmentation::Segmentation& constSeg = seg;
    std::vector<Vectorized> vectorized(regions.size());
    parallel_for(regions.size(), [&](std::size_t i) {
        const auto* region = constSeg.find(regions[i]);
        if (region == nullptr || (backgroundRgb && region->rgb == *backgroundRgb)) {
            return;
        }
        auto sets = vectorization::vectorize_region(constSeg, regions[i], vecOpts);
        if (!sets || sets->empty()) {
            return; // région non vectorisable : ignorée sans erreur
        }
        vectorized[i].sets = std::move(*sets);
        vectorized[i].satin = satin_choice_for(largest_piece(vectorized[i].sets), options);
    });

    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (vectorized[i].sets.empty()) {
            continue;
        }
        const RegionId id = regions[i];
        const auto* region = constSeg.find(id);

        // Objet vectoriel (toujours créé : c'est la géométrie éditable).
        document::VectorObject vec;
        vec.id = ids.next();
        vec.name = "Région " + std::to_string(id.value);
        vec.source_region = id;
        vec.rgb = region->rgb;
        vec.paths = std::move(vectorized[i].sets);
        const ObjectId vecId = vec.id;
        result.vectors.push_back(std::move(vec));
        regionVector[id.value] = vecId;

        // Choix du type de point selon la forme du plus grand morceau --
        // logique PARTAGÉE avec `auto_digitize_vectors` (§ classify_and_
        // build_embroidery ci-dessus), une région de segmentation n'étant
        // qu'une des deux sources possibles d'un morceau déjà vectorisé.
        const RegionSource source{vecId, region->rgb, "Région " + std::to_string(id.value)};
        classify_and_build_embroidery(largest_piece(result.vectors.back().paths), source, ids,
                                      options, vectorized[i].satin, result);
    }

    if (result.vectors.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucune région exploitable pour la numérisation automatique");
    }
    const auto borders = segmentation::region_adjacency(seg);
    RegionPairs adjacency;
    for (const auto& b : borders) {
        adjacency.insert({b.a.value, b.b.value});
    }
    configure_tatami_fills(result, {}, &adjacency, options);
    overlap_neighbor_fills(result, regionVector, borders, options);
    order_in_layers(result, {}, options);
    return result;
}

Result<AutoResult> auto_digitize_vectors(const std::vector<document::VectorObject>& vectors,
                                         IdGenerator<ObjectId>& ids, const AutoOptions& options) {
    if (vectors.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun objet vectoriel à numériser");
    }

    AutoResult result;
    // Essais de satin en parallèle (cf. `auto_digitize`), assemblage en séquence.
    std::vector<SatinChoice> choices(vectors.size());
    parallel_for(vectors.size(), [&](std::size_t i) {
        if (!vectors[i].paths.empty()) {
            choices[i] = satin_choice_for(largest_piece(vectors[i].paths), options);
        }
    });
    for (std::size_t i = 0; i < vectors.size(); ++i) {
        const auto& vec = vectors[i];
        if (vec.paths.empty()) {
            continue; // objet vectoriel vide : ignoré sans erreur, comme une région non
                      // vectorisable
        }
        // Aucun nouvel objet vectoriel créé pour la géométrie d'entrée : elle
        // existe déjà (import SVG direct, § "skip segmentation").
        const RegionSource source{vec.id, vec.rgb, vec.name};
        classify_and_build_embroidery(largest_piece(vec.paths), source, ids, options, choices[i],
                                      result);
    }

    if (result.embroideries.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucun objet vectoriel exploitable pour la numérisation automatique");
    }
    configure_tatami_fills(result, vectors, nullptr, options);
    order_in_layers(result, vectors, options);
    return result;
}

} // namespace openstitch::autodigitize
