// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/factory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <utility>

#include "debug_dump.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/image_blend.hpp"
#include "openstitch/autodigitize/two_color_blend.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/core/pipeline_profile.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
#include "openstitch/segmentation/blend_zones.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::project_ops {

namespace {

Micrometers mm_to_um(double mm) {
    return Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
}

void say(const StatusLog& log, const std::string& line) {
    if (log) {
        log(line);
    }
}

} // namespace

namespace {

std::vector<std::string> split_on(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::string cur;
    for (const char c : text) {
        if (c == sep) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

// Entier strict (« 3 », pas « 3x »).
std::optional<int> parse_int_strict(const std::string& t) {
    if (t.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const int v = std::stoi(t, &used);
        return used == t.size() ? std::optional<int>{v} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parse_double_strict(const std::string& t) {
    if (t.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const double v = std::stod(t, &used);
        return used == t.size() ? std::optional<double>{v} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

Result<std::vector<image::ImageOp>> parse_pre_spec(const std::string& spec) {
    std::vector<image::ImageOp> ops;
    if (spec.empty()) {
        return ops;
    }
    const auto bad = [&](const std::string& item, const std::string& why) {
        return fail(
            ErrorCategory::UserInput, "traitement d'image invalide « " + item + " » : " + why,
            "grammaire : median[:1|2] bilateral[:1..3] flatten[:1..3] clahe[:C] "
            "sharpen[:A] quantize[:N] contrast:B:C "
            "gray crop:x:y:w:h flip:h|v rotate:1..3 transparent:RRGGBB[:T], séparés par « + »");
    };
    for (const auto& item : split_on(spec, '+')) {
        const auto f = split_on(item, ':');
        const std::string& name = f[0];
        const auto argc = f.size() - 1;
        const auto intArg = [&](std::size_t i, int def) -> std::optional<int> {
            return i < f.size() ? parse_int_strict(f[i]) : std::optional<int>{def};
        };
        if (name == "median" && argc <= 1) {
            const auto v = intArg(1, 1);
            if (!v || *v < 1 || *v > 2) {
                return bad(item, "force 1 ou 2");
            }
            ops.emplace_back(image::MedianDenoiseOp{*v});
        } else if (name == "bilateral" && argc <= 1) {
            const auto v = intArg(1, 2);
            if (!v || *v < 1 || *v > 3) {
                return bad(item, "force 1 à 3");
            }
            ops.emplace_back(image::BilateralDenoiseOp{*v});
        } else if (name == "quantize" && argc <= 1) {
            const auto v = intArg(1, 8);
            if (!v || *v < 2 || *v > 64) {
                return bad(item, "nombre de couleurs 2 à 64");
            }
            ops.emplace_back(image::QuantizeOp{*v});
        } else if (name == "contrast" && argc == 2) {
            const auto b = parse_double_strict(f[1]);
            const auto c = parse_double_strict(f[2]);
            if (!b || !c || *b < -100 || *b > 100 || *c < -100 || *c > 100) {
                return bad(item, "luminosité et contraste entre -100 et 100");
            }
            ops.emplace_back(image::BrightnessContrastOp{*b, *c});
        } else if (name == "flatten" && argc <= 1) {
            const auto v = intArg(1, 2);
            if (!v || *v < 1 || *v > 3) {
                return bad(item, "force 1 à 3");
            }
            ops.emplace_back(image::FlattenOp{*v});
        } else if (name == "clahe" && argc <= 1) {
            const auto v = argc == 1 ? parse_double_strict(f[1]) : std::optional<double>{2.0};
            if (!v || *v < 1.0 || *v > 8.0) {
                return bad(item, "limite de contraste 1 à 8");
            }
            ops.emplace_back(image::ClaheOp{*v});
        } else if (name == "sharpen" && argc <= 1) {
            const auto v = argc == 1 ? parse_double_strict(f[1]) : std::optional<double>{1.0};
            if (!v || *v < 0.0 || *v > 3.0) {
                return bad(item, "netteté 0 à 3");
            }
            ops.emplace_back(image::SharpenOp{*v});
        } else if (name == "transparent" && (argc == 1 || argc == 2)) {
            // Couleur hexadécimale rendue transparente dans toute l'image, tolérance 0..255.
            const auto tol = intArg(2, 24);
            const std::string& hex = f[1];
            const bool hexOk = hex.size() == 6 &&
                               hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
            if (!hexOk || !tol || *tol < 0 || *tol > 255) {
                return bad(item, "couleur RRGGBB et tolérance 0 à 255");
            }
            const auto rgb = std::stoul(hex, nullptr, 16);
            ops.emplace_back(image::ColorToAlphaOp{static_cast<std::uint8_t>((rgb >> 16) & 0xFF),
                                                   static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                                                   static_cast<std::uint8_t>(rgb & 0xFF), *tol,
                                                   false, 0, 0});
        } else if (name == "gray" && argc == 0) {
            ops.emplace_back(image::GrayscaleOp{});
        } else if (name == "crop" && argc == 4) {
            const auto x = parse_int_strict(f[1]);
            const auto y = parse_int_strict(f[2]);
            const auto w = parse_int_strict(f[3]);
            const auto h = parse_int_strict(f[4]);
            if (!x || !y || !w || !h || *x < 0 || *y < 0 || *w <= 0 || *h <= 0) {
                return bad(item, "x:y:largeur:hauteur en pixels, positifs");
            }
            ops.emplace_back(image::CropOp{*x, *y, *w, *h});
        } else if (name == "flip" && argc == 1 && (f[1] == "h" || f[1] == "v")) {
            ops.emplace_back(image::FlipOp{f[1] == "h"});
        } else if (name == "rotate" && argc == 1) {
            const auto v = parse_int_strict(f[1]);
            if (!v || *v < 1 || *v > 3) {
                return bad(item, "quarts de tour 1 à 3");
            }
            ops.emplace_back(image::Rotate90Op{*v});
        } else {
            return bad(item, "opération inconnue ou arguments incorrects");
        }
    }
    return ops;
}

} // namespace

Result<void> validate_pre_spec(const std::string& spec) {
    auto ops = parse_pre_spec(spec);
    if (!ops) {
        return std::unexpected(ops.error());
    }
    return {};
}

namespace {

// Aire (mm²) d'un jeu de chemins : contour extérieur moins les trous (nœuds, sans courbure).
double path_set_area_mm2(const geometry::PathSet& set) {
    const auto shoelace = [](const geometry::Path& p) {
        double a = 0.0;
        const auto& n = p.nodes;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const auto& u = n[i].pos;
            const auto& v = n[(i + 1) % n.size()].pos;
            a += static_cast<double>(u.x.value) * v.y.value -
                 static_cast<double>(v.x.value) * u.y.value;
        }
        return std::abs(a) / 2.0 / 1e6; // µm² -> mm²
    };
    double area = shoelace(set.outer);
    for (const auto& h : set.holes) {
        area -= shoelace(h);
    }
    return area;
}

double guide_length_mm(const geometry::Path& guide) {
    std::vector<Vec2um> pts;
    pts.reserve(guide.nodes.size());
    for (const auto& n : guide.nodes) {
        pts.push_back(n.pos);
    }
    return geometry::polyline_length(pts) / 1000.0;
}

std::size_t apply_flow_fill(const std::vector<document::VectorObject>& vectors,
                            std::vector<document::EmbroideryObject>& embroideries,
                            const DigitizeSettings& s) {
    // Couleur la plus sombre = traits et contours : ils gardent leur tatami.
    const auto lum = [](const std::array<std::uint8_t, 3>& c) { return c[0] + c[1] + c[2]; };
    const auto findVector = [&](ObjectId id) -> const document::VectorObject* {
        for (const auto& v : vectors) {
            if (v.id == id) {
                return &v;
            }
        }
        return nullptr;
    };
    int darkest = 3 * 255 + 1;
    for (const auto& e : embroideries) {
        if (e.is_tatami()) {
            darkest = std::min(darkest, lum(e.rgb));
        }
    }
    std::size_t converted = 0;
    for (auto& e : embroideries) {
        const auto* tatami = std::get_if<document::TatamiParams>(&e.params);
        const auto* source = findVector(e.source_vector);
        if (tatami == nullptr || source == nullptr || lum(e.rgb) <= darkest) {
            continue;
        }
        double area = 0.0;
        for (const auto& set : source->paths) {
            area += path_set_area_mm2(set);
        }
        if (area < s.flow_min_area_mm2) {
            continue;
        }
        std::vector<geometry::Path> guides;
        for (const auto& set : source->paths) {
            auto g = stitch_generation::directional_guides_from_region(set);
            guides.insert(guides.end(), std::make_move_iterator(g.begin()),
                          std::make_move_iterator(g.end()));
        }
        double longest = 0.0;
        for (const auto& g : guides) {
            longest = std::max(longest, guide_length_mm(g));
        }
        if (guides.empty() || longest * longest / area < s.flow_min_elongation) {
            continue;
        }
        auto params = stitch_generation::directional_from_tatami(
            *tatami, source->paths, static_cast<std::uint32_t>(e.id.value));
        params.guides = std::move(guides);
        e.params = std::move(params);
        ++converted;
    }
    return converted;
}

} // namespace

Result<void> apply_basic_finishing(document::Project& project, double trim_threshold_mm,
                                   std::string_view lock) {
    document::LockStitch type{};
    if (lock == "none") {
        type = document::LockStitch::None;
    } else if (lock == "backforth") {
        type = document::LockStitch::BackAndForth;
    } else if (lock == "triangle") {
        type = document::LockStitch::Triangle;
    } else if (lock == "zigzag") {
        type = document::LockStitch::MicroZigzag;
    } else {
        return fail(ErrorCategory::UserInput,
                    "point d'arrêt inconnu : « " + std::string(lock) + " »",
                    "valeurs : none, backforth, triangle, zigzag");
    }
    if (!(trim_threshold_mm > 0.0)) {
        return fail(ErrorCategory::UserInput, "le seuil de coupe doit être strictement positif");
    }
    project.finishing.trim_threshold = mm_to_um(trim_threshold_mm);
    project.finishing.lock_type = type;
    return {};
}

Result<void> parse_route_levels(std::string_view list, document::HiddenTravel& h) {
    h.between_objects = h.within_object = h.reorder_underlay = h.reorder_regions = false;
    if (list.empty() || list == "all") {
        h.between_objects = h.within_object = h.reorder_underlay = true; // sans le mode avancé
        return {};
    }
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto comma = list.find(',', start);
        const std::string name(list.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (name == "between") {
            h.between_objects = true;
        } else if (name == "within") {
            h.within_object = true;
        } else if (name == "underlay") {
            h.reorder_underlay = true;
        } else if (name == "all") {
            h.between_objects = h.within_object = h.reorder_underlay = true;
        } else if (name == "regions") {
            h.reorder_regions = true;
        } else {
            return fail(ErrorCategory::UserInput,
                        "niveau inconnu : « " + name +
                            " » (between, within, underlay, regions ou all)");
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return {};
}

Result<document::Project> create_empty_project(const NewProjectOptions& options) {
    if (!(options.width_mm > 0.0) || !(options.height_mm > 0.0)) {
        return fail(ErrorCategory::UserInput,
                    "le cadre doit avoir une largeur et une hauteur > 0 mm");
    }
    if (options.width_mm > 2000.0 || options.height_mm > 2000.0) {
        return fail(ErrorCategory::UserInput, "cadre au-delà de 2 m : valeur incohérente",
                    "les dimensions s'expriment en millimètres");
    }
    if (options.margin_mm < 0.0 ||
        options.margin_mm * 2.0 >= std::min(options.width_mm, options.height_mm)) {
        return fail(ErrorCategory::UserInput,
                    "la marge doit être >= 0 et laisser une zone brodable non vide",
                    "marge < moitié du plus petit côté du cadre");
    }
    document::Project project;
    project.canvas.width = mm_to_um(options.width_mm);
    project.canvas.height = mm_to_um(options.height_mm);
    project.canvas.margin = mm_to_um(options.margin_mm);
    if (auto finishing = apply_basic_finishing(project, options.trim_threshold_mm, options.lock);
        !finishing) {
        return std::unexpected(finishing.error());
    }
    return project;
}

autodigitize::AutoOptions make_auto_options(const DigitizeSettings& s, Millimeters mm_per_px,
                                            bool skipLargest) {
    autodigitize::AutoOptions opts;
    opts.mm_per_px = mm_per_px;
    opts.skip_largest_region = skipLargest;
    opts.skip_background_outer_only = s.skip_background == 2;
    opts.satin_bands = s.satin_bands;
    opts.satin_compare = s.satin_compare;
    opts.curve_tolerance = mm_to_um(s.curve_tolerance_mm);
    if (s.simplify_tolerance_um) {
        opts.simplify_tolerance = Micrometers{*s.simplify_tolerance_um};
    }
    opts.max_threads = s.max_threads;
    if (s.min_feature_mm) {
        opts.min_feature_width_mm = *s.min_feature_mm;
    }
    if (s.min_region_mm2) {
        opts.min_region_area_mm2 = *s.min_region_mm2;
    }
    opts.island_area_mm2 = s.island_cleanup_mm2;
    opts.fill_overlap = mm_to_um(s.fill_overlap_mm);
    opts.fill_hidden_underpath = s.fill_hidden_underpath;
    if (s.fill_inset_mm) {
        opts.fill_inset = mm_to_um(*s.fill_inset_mm);
    }
    opts.island_max_delta_e = s.island_max_delta_e;
    if (s.solid_block) {
        autodigitize::apply_solid_block(opts, s.solid_block_overlap_mm);
    }
    return opts;
}

Result<DigitizedObjects> digitize_objects(const DigitizeSource& source, IdGenerator<ObjectId>& ids,
                                          const DigitizeSettings& s, bool skipLargest,
                                          const StatusLog& log,
                                          autodigitize::LineLayerMasks* debugMasks) {
    if (s.mode == "blend") {
        // Zones de fondu : chaîne des formes en bloc uni (chaque zone déborde sous ses voisines,
        // pas de tissu entre deux zones) ; `apply_blend_zones` change ensuite les tatami en
        // fondus.
        DigitizeSettings shapes = s;
        shapes.mode = "shapes";
        shapes.solid_block = true;
        return digitize_objects(source, ids, shapes, skipLargest, log, debugMasks);
    }
    const bool stippling = s.mode == "stipple";
    const bool contours = s.mode == "contours";
    if (!stippling && !contours && s.mode != "shapes") {
        return fail(ErrorCategory::UserInput, "mode inconnu : « " + s.mode + " »",
                    "valeurs : shapes, contours, stipple");
    }
    if ((stippling ? source.image == nullptr : source.segmentation == nullptr) ||
        (s.dark_lines && !contours && source.image == nullptr)) {
        return fail(ErrorCategory::OperationImpossible,
                    "numérisation impossible : segmentation ou image de travail manquante");
    }
    autodigitize::AutoOptions opts = make_auto_options(s, source.mm_per_px, skipLargest);
    opts.detail_mask = source.detail_mask;

    // Stratégie « contours / dessin au trait » : lignes médianes cousues en point droit
    // (satin legacy dégradé en point droit).
    autodigitize::ContourOptions co;
    co.mm_per_px = source.mm_per_px;
    co.skip_largest_region = skipLargest;
    co.detail = s.detail;
    co.routing = s.single_path ? autodigitize::ContourRouting::SinglePath
                               : autodigitize::ContourRouting::Chains;
    co.technique = s.technique == "running" ? autodigitize::ContourTechnique::Running
                   : (s.technique == "legacy-satin" || s.technique == "satin")
                       ? autodigitize::ContourTechnique::Satin
                       : autodigitize::ContourTechnique::Automatic;
    if (contours) {
        char buf[120];
        std::snprintf(buf, sizeof buf, "Mode : contours (detail %.2f, technique %s)", s.detail,
                      s.technique.c_str());
        say(log, buf);
    }
    DigitizedObjects out;
    autodigitize::ContourMetrics cm;
    autodigitize::StippleMetrics sm;
    autodigitize::LineLayerMetrics lm;
    bool usedDarkLines = false;
    auto result = [&]() -> Result<autodigitize::AutoResult> {
        if (stippling) {
            autodigitize::StippleObjectOptions so;
            so.mm_per_px = source.mm_per_px;
            so.density.invert = s.stipple_invert;
            so.stipple.point_count = s.stipple_points;
            so.stipple.seed = static_cast<std::uint32_t>(s.stipple_seed);
            say(log, "Mode : stippling (" + std::to_string(s.stipple_points) + " points, graine " +
                         std::to_string(s.stipple_seed) + ")");
            return autodigitize::auto_digitize_stipple(*source.image, ids, so, &sm);
        }
        if (!contours && s.dark_lines) {
            autodigitize::LineLayerOptions lo;
            lo.enabled = true;
            lo.max_line_width_mm = s.dark_lines_width_mm;
            lo.dark_max_lightness = s.dark_lines_lightness;
            lo.local_contrast = s.dark_lines_contrast;
            lo.objects_only = s.dark_lines_objects_only;
            lo.bars_max_width_mm = s.dark_lines_bars_mm;
            lo.min_line_length_mm = s.dark_lines_min_length_mm;
            lo.rings_satin = s.rings_satin;
            lo.masks = debugMasks;
            // Classification automatique (simple/triple selon la largeur) : `Running` (point droit
            // simple partout, HP-AUTO-021) changerait la sortie historique des traits sombres.
            co.technique = autodigitize::ContourTechnique::Automatic;
            // Un réseau de traits se coud en parcours eulérien : moins de polylignes, donc moins
            // de coupes, au prix d'un court repassage.
            co.routing = autodigitize::ContourRouting::SinglePath;
            usedDarkLines = true;
            return autodigitize::auto_digitize_with_lines(*source.segmentation, *source.image, ids,
                                                          opts, lo, co, &lm);
        }
        return contours ? autodigitize::auto_digitize_contours(*source.segmentation, ids, co, &cm)
                        : autodigitize::auto_digitize(*source.segmentation, ids, opts);
    }();
    if (!result) {
        return std::unexpected(result.error());
    }
    out.objects = std::move(*result);
    if (stippling) {
        out.stipple = sm;
    }
    if (contours) {
        out.contour = cm;
    }
    if (usedDarkLines) {
        out.dark_lines = lm;
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "Traits sombres : %zu px séparés des remplissages, %zu objet(s) de ligne "
                      "(%zu segment(s), %.0f mm de point droit)",
                      lm.line_pixels, lm.line_objects, lm.contours.segments,
                      lm.contours.running_length_mm);
        say(log, buf);
    }
    if (s.flow_fill && !stippling) {
        out.flow_fill = apply_flow_fill(out.objects.vectors, out.objects.embroideries, s);
        say(log, "Remplissage directionnel (mouvement) : " + std::to_string(out.flow_fill) +
                     " forme(s) allongée(s)");
    }
    return out;
}

Result<void> apply_blend_zones(document::Project& project, const image::Image& working,
                               const DigitizeSettings& s, DigitizeReport& report,
                               const StatusLog& log) {
    const Millimeters mm = project.imageSource().mm_per_px;
    const auto halfW = static_cast<std::int32_t>(std::lround(working.width * mm.value * 500.0));
    const auto halfH = static_cast<std::int32_t>(std::lround(working.height * mm.value * 500.0));
    const std::vector<geometry::PathSet> whole{
        geometry::PathSet{geometry::rectangle_path(Vec2um{Micrometers{-halfW}, Micrometers{-halfH}},
                                                   Vec2um{Micrometers{halfW}, Micrometers{halfH}}),
                          {}}};
    autodigitize::ImageBlendOptions options;
    options.max_threads_per_point = static_cast<std::size_t>(s.blend_max_per_zone);
    auto palette = autodigitize::propose_blend_palette(
        working, mm, whole, static_cast<std::size_t>(s.blend_threads), options);
    if (!palette) {
        return fail(ErrorCategory::OperationImpossible,
                    "palette de mélange impossible : " + palette.error().message,
                    "image trop uniforme pour le mode blend : essayez --mode shapes");
    }
    // Ordre de couture de la palette : du fil le plus RARE au plus fréquent (pixels dont il est
    // le plus proche). Le fil dominant d'une zone y est ainsi cousu en dernier : il couvre le
    // plus et cache les trajets des fils rares cousus avant lui ; la dernière couche, que rien
    // ne recouvre, est la plus compacte.
    {
        const auto samples = autodigitize::sample_region_colors(working, mm, whole, 20'000);
        std::vector<std::size_t> nearest(palette->size(), 0);
        for (const auto& sample : samples) {
            std::size_t best = 0;
            long bestD = std::numeric_limits<long>::max();
            for (std::size_t i = 0; i < palette->size(); ++i) {
                long d = 0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const long delta = long{sample.rgb[c]} - long{(*palette)[i][c]};
                    d += delta * delta;
                }
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            ++nearest[best];
        }
        std::vector<std::size_t> order(palette->size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return nearest[a] < nearest[b]; });
        std::vector<std::array<std::uint8_t, 3>> sorted;
        for (const auto i : order) {
            sorted.push_back((*palette)[i]);
        }
        *palette = std::move(sorted);
    }
    report.blend_palette = *palette;
    {
        std::string line = "Palette de mélange : " + std::to_string(palette->size()) + " fil(s)";
        for (const auto& c : *palette) {
            char hex[12];
            std::snprintf(hex, sizeof hex, " #%02X%02X%02X", c[0], c[1], c[2]);
            line += hex;
        }
        say(log, line);
    }
    std::vector<ObjectId> fills;
    for (const auto& e : project.embroidery_objects) {
        if (e.is_tatami()) {
            fills.push_back(e.id);
        }
    }
    for (const ObjectId id : fills) {
        const auto* object = project.findEmbroidery(id);
        const auto* vec = object ? project.findObject(object->source_vector) : nullptr;
        if (!vec) {
            continue;
        }
        const auto picks = autodigitize::choose_zone_threads(
            working, mm, vec->paths, *palette, static_cast<std::size_t>(s.blend_max_per_zone), 0.2,
            options);
        if (picks.empty()) {
            continue;
        }
        if (picks.size() == 1) {
            if (auto* e = project.findEmbroidery(id)) {
                e->rgb = (*palette)[picks.front()];
                e->thread.reset();
            }
            ++report.blend_flats;
            continue;
        }
        ImageBlendRequest request;
        for (const auto i : picks) {
            request.colors.push_back((*palette)[i]);
        }
        request.cell = mm_to_um(s.blend_cell_mm);
        request.continuous_rows = s.blend_continuous;
        request.min_share = 0.15;
        request.max_threads_per_point = picks.size();
        // Rangées horizontales dans toutes les zones : l'angle propre à chaque tatami ferait
        // apparaître chaque frontière de zone comme un changement de direction.
        request.stitch_angle = Angle{0.0};
        auto command = make_image_blend_command(project, id, request);
        if (!command) {
            report.warnings.push_back("zone " + std::to_string(id.value) +
                                      " laissée en aplat : " + command.error().message);
            ++report.blend_flats;
            continue;
        }
        commands::UndoStack stack;
        stack.execute(std::move(*command), project);
        ++report.blend_fondus;
    }
    // Ordre de couture : fil par fil sur TOUTES les zones (un changement de fil par couleur de
    // la palette au lieu d'un par couche). Dans une zone, les couches suivent déjà l'ordre de
    // la palette (de l'arrière vers l'avant) : le tri stable le conserve. Les ancres ne cousent
    // rien et passent en tête.
    std::map<std::array<std::uint8_t, 3>, int> indexOf;
    for (std::size_t i = 0; i < palette->size(); ++i) {
        indexOf.emplace((*palette)[i], static_cast<int>(i));
    }
    const auto key = [&](const document::EmbroideryObject& e) {
        if (e.is_multicolor_gradient()) {
            return -1;
        }
        const auto it = indexOf.find(e.rgb);
        return it == indexOf.end() ? static_cast<int>(palette->size()) : it->second;
    };
    std::stable_sort(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                     [&](const auto& a, const auto& b) { return key(a) < key(b); });
    say(log, "Zones de fondu : " + std::to_string(report.blend_fondus) + " fondu(s), " +
                 std::to_string(report.blend_flats) + " aplat(s)");
    return {};
}

Result<DigitizeReport> digitize_image_into(document::Project& project,
                                           const std::filesystem::path& imagePath,
                                           const DigitizeSettings& s, const StatusLog& log) {
    if (!project.embroidery_objects.empty() || !project.vector_objects.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "le projet contient déjà des objets : la numérisation automatique ne "
                    "s'applique qu'à un projet sans objet",
                    "créez un projet neuf (project digitize) ou supprimez les objets d'abord");
    }
    if (!(s.dpi > 0.0)) {
        return fail(ErrorCategory::UserInput, "la résolution (dpi) doit être strictement positive");
    }
    const bool stippling = s.mode == "stipple";
    const bool contours = s.mode == "contours";
    const bool blending = s.mode == "blend";
    if (!stippling && !contours && !blending && s.mode != "shapes") {
        return fail(ErrorCategory::UserInput, "mode inconnu : « " + s.mode + " »",
                    "valeurs : shapes, contours, stipple, blend");
    }
    if (blending && (s.blend_threads < 2 || s.blend_threads > 16 || s.blend_max_per_zone < 1 ||
                     s.blend_max_per_zone > 4 || !(s.blend_cell_mm >= 0.5) ||
                     !(s.blend_cell_mm <= 5.0) || !(s.blend_edge > 0.0))) {
        return fail(ErrorCategory::UserInput, "réglages du mode blend hors bornes",
                    "fils 2 à 16, fils par zone 1 à 4, case 0,5 à 5 mm, seuil de bord > 0");
    }

    const auto loaded = [&] {
        const pipeline_profile::Scope profile(pipeline_profile::Stage::Decode);
        return image::load_image(imagePath);
    }();
    if (!loaded) {
        return fail(ErrorCategory::InvalidFile,
                    "chargement de l'image impossible : " + loaded.error().message,
                    "formats acceptés : PNG, JPEG, BMP, TIFF ; vérifiez le chemin et l'extension "
                    "du fichier");
    }
    DigitizeReport report;
    report.image_width_px = static_cast<std::size_t>(loaded->width);
    report.image_height_px = static_cast<std::size_t>(loaded->height);
    report.image_had_alpha = loaded->source_had_alpha;
    say(log, "Image : " + std::to_string(loaded->width) + " x " + std::to_string(loaded->height) +
                 " px (alpha source : " + (loaded->source_had_alpha ? "oui" : "non") + ")");

    project.imageSource().mm_per_px = Millimeters{25.4 / s.dpi};
    project.imageSource().original = *loaded;
    // Lignes cohérentes (FDoG) : la photo devient des traits noirs sur fond blanc ; la
    // segmentation et le mode contours travaillent ensuite dessus.
    if (s.coherent_lines) {
        const auto lines =
            image::apply_op(project.imageSource().original, image::CoherentLinesOp{});
        if (!lines) {
            return fail(ErrorCategory::OperationImpossible,
                        "extraction des lignes impossible : " + lines.error().message,
                        "retirez --coherent-lines ou essayez une image plus grande");
        }
        project.imageSource().original = *lines;
        say(log, "Lignes cohérentes (FDoG) appliquées avant la segmentation");
    }

    // Traitements d'image avant segmentation : non destructifs (`project.imageSource().ops`), la
    // segmentation et les analyses travaillent sur l'image de travail.
    auto preOps = parse_pre_spec(s.pre);
    if (!preOps) {
        return std::unexpected(preOps.error());
    }
    image::Image working = project.imageSource().original;
    if (!preOps->empty()) {
        auto processed = [&] {
            const auto bytes = project.imageSource().original.rgba.size();
            const pipeline_profile::Scope profile(pipeline_profile::Stage::Preprocess, bytes,
                                                  bytes);
            return image::apply_pipeline(project.imageSource().original, *preOps);
        }();
        if (!processed) {
            return fail(ErrorCategory::OperationImpossible,
                        "traitement d'image impossible : " + processed.error().message,
                        "vérifier --pre (recadrage dans l'image, forces valides)");
        }
        working = std::move(*processed);
        project.imageSource().ops = *preOps;
        say(log, "Traitement d'image avant segmentation : " + s.pre + " (" +
                     std::to_string(working.width) + " x " + std::to_string(working.height) +
                     " px)");
    }

    // Finitions (Lots E/F) : nouveau projet -> activées ; réglables.
    if (auto finishing = apply_basic_finishing(project, s.trim_threshold_mm, s.lock); !finishing) {
        return std::unexpected(finishing.error());
    }
    project.finishing.hidden_travel.enabled = s.hidden_travel;
    project.finishing.hidden_travel.allow_same_color_surface = s.hidden_same_color;
    if (auto levels = parse_route_levels(s.hidden_levels, project.finishing.hidden_travel);
        !levels) {
        return std::unexpected(levels.error());
    }
    project.finishing.hidden_travel.max_bridge_length = mm_to_um(s.hidden_max_mm);

    // Stippling : pas de segmentation, l'image est lue comme une carte de tons.
    std::optional<segmentation::BackgroundCandidate> candidate;
    if (!stippling) {
        auto seg = [&]() -> Result<segmentation::Segmentation> {
            const pipeline_profile::Scope profile(pipeline_profile::Stage::Segment);
            if (blending) {
                return segmentation::segment_blend_zones(
                    working, project.imageSource().mm_per_px,
                    {.edge_threshold = s.blend_edge, .min_zone_mm2 = s.blend_min_zone_mm2});
            }
            segmentation::SegmentationOptions options{.max_colors = s.max_colors,
                                                      .min_region_px = s.min_region_px,
                                                      .smoothing_radius_px = s.smoothing_px};
            if (s.fine_structures) {
                options.fine_structures = segmentation::FineStructureOptions{};
            }
            return segmentation::segment(working, options);
        }();
        if (!seg) {
            return fail(ErrorCategory::OperationImpossible,
                        "segmentation impossible : " + seg.error().message,
                        "essayez une image plus grande ou --max-colors différent");
        }
        report.regions = seg->region_count();
        say(log, "Régions segmentées : " + std::to_string(report.regions));
        project.imageSource().segmentation = std::move(*seg);
        candidate = segmentation::background_candidate(*project.imageSource().segmentation);
    }
    report.background_candidate = candidate;

    // Même règle que le dialogue desktop (Lot A) : jamais sur le seul critère « pas d'alpha ».
    if (candidate) {
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "Région candidate au fond : #%02X%02X%02X, %.1f %% de l'image, L* %.0f, %d "
                      "bord(s) touché(s)",
                      candidate->rgb[0], candidate->rgb[1], candidate->rgb[2],
                      candidate->area_ratio * 100.0, candidate->lightness,
                      candidate->sides_touched);
        say(log, buf);
    }
    report.background_auto = s.skip_background < 0;
    const bool skipLargest = stippling
                                 ? false
                                 : (s.skip_background < 0 ? (candidate && candidate->recommended)
                                                          : (s.skip_background != 0));
    report.background_skipped = skipLargest;

    say(log, std::string("Ignorer la plus grande région (fond) : ") +
                 (skipLargest ? "oui" : "non") +
                 (report.background_auto ? " (automatique)" : " (option explicite)"));

    autodigitize::LineLayerMasks debugMasks;
    const DigitizeSource src{
        project.imageSource().segmentation ? &*project.imageSource().segmentation : nullptr,
        &working, project.imageSource().mm_per_px};
    // Mode blend : les zones passent d'abord par la chaîne des formes (`digitize_objects`), puis
    // chaque tatami devient un fondu (`apply_blend_zones`).
    auto outcome = [&] {
        const pipeline_profile::Scope profile(pipeline_profile::Stage::Digitize);
        return digitize_objects(src, project.object_ids, s, skipLargest, log,
                                s.debug_dir.empty() ? nullptr : &debugMasks);
    }();
    if (!outcome) {
        return fail(ErrorCategory::OperationImpossible,
                    "numérisation impossible : " + outcome.error().message,
                    "image trop uniforme, ou seul le fond a été détecté : essayez "
                    "--skip-background no, --max-colors plus grand ou --min-region-px plus petit");
    }
    report.stipple = outcome->stipple;
    report.contour = outcome->contour;
    report.dark_lines = outcome->dark_lines;
    if (outcome->dark_lines) {
        report.rings_satin = outcome->dark_lines->ring_objects;
    }
    report.flow_fill = outcome->flow_fill;
    report.warnings = std::move(outcome->objects.warnings);
    for (auto& v : outcome->objects.vectors) {
        project.vector_objects.push_back(std::move(v));
    }
    for (auto& e : outcome->objects.embroideries) {
        project.embroidery_objects.push_back(std::move(e));
    }
    if (blending) {
        if (auto blended = apply_blend_zones(project, working, s, report, log); !blended) {
            return std::unexpected(blended.error());
        }
    }
    report.vector_objects = project.vector_objects.size();
    report.embroidery_objects = project.embroidery_objects.size();
    for (const auto& e : project.embroidery_objects) {
        if (e.is_satin() || e.is_auto_satin()) {
            ++report.satin;
        } else if (e.is_tatami()) {
            ++report.tatami;
        } else if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            ++report.running;
        }
    }
    if (!s.debug_dir.empty()) {
        if (auto dumped = detail::write_debug_dir(std::filesystem::path(s.debug_dir), project,
                                                  report, s, debugMasks);
            !dumped) {
            report.warnings.push_back("diagnostic non écrit : " + dumped.error().message);
        } else {
            say(log, "Diagnostic écrit dans " + s.debug_dir);
        }
    }
    return report;
}

} // namespace openstitch::project_ops
