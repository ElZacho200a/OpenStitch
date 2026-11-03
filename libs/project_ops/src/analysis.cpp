// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "thread_model.hpp"

namespace openstitch::project_ops {

namespace {

using detail::Component;
using detail::Lab;
using detail::ThreadModel;
using detail::ThreadRaster;
using nlohmann::json;

constexpr double kPi = std::numbers::pi;

Error make_error(ErrorCategory category, std::string message) {
    return Error{category, std::move(message), {}};
}

double round_to(double v, int digits) {
    const double k = std::pow(10.0, digits);
    return std::round(v * k) / k;
}

// Fond ignoré : la plus grande région de la segmentation dont la couleur n'est celle d'aucun fil.
std::optional<std::array<std::uint8_t, 3>> background_colour(const document::Project& project,
                                                             const ThreadModel& model) {
    if (!project.segmentation) {
        return std::nullopt;
    }
    std::size_t best = 0;
    std::optional<std::array<std::uint8_t, 3>> out;
    for (const auto& slot : project.segmentation->region_slots) {
        if (!slot || slot->pixel_count <= best) {
            continue;
        }
        if (std::find(model.palette.begin(), model.palette.end(), slot->rgb) !=
            model.palette.end()) {
            continue;
        }
        best = slot->pixel_count;
        out = slot->rgb;
    }
    return out;
}

// Distance de Chebyshev (en pixels) au plus proche pixel `true` de `seed` ; 32767 si aucun.
std::vector<std::int16_t> chebyshev_distance(const std::vector<char>& seed, int w, int h) {
    constexpr std::int16_t kFar = 32767;
    std::vector<std::int16_t> d(seed.size(), kFar);
    for (std::size_t i = 0; i < seed.size(); ++i) {
        if (seed[i] != 0) {
            d[i] = 0;
        }
    }
    const auto at = [&](int x, int y) -> std::int16_t {
        return x < 0 || y < 0 || x >= w || y >= h ? kFar : d[static_cast<std::size_t>(y) * w + x];
    };
    const auto relax = [&](int x, int y, int dx, int dy) {
        const std::int16_t v = at(x + dx, y + dy);
        return v == kFar ? kFar : static_cast<std::int16_t>(v + 1);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::int16_t& cur = d[static_cast<std::size_t>(y) * w + x];
            cur = std::min({cur, relax(x, y, -1, 0), relax(x, y, 0, -1), relax(x, y, -1, -1),
                            relax(x, y, 1, -1)});
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            std::int16_t& cur = d[static_cast<std::size_t>(y) * w + x];
            cur = std::min(
                {cur, relax(x, y, 1, 0), relax(x, y, 0, 1), relax(x, y, 1, 1), relax(x, y, -1, 1)});
        }
    }
    return d;
}

// Ouverture morphologique 3x3 (érosion puis dilatation) : retire les structures d'un ou deux pixels
// de large, ici le liseré anti-crénelé qui entoure chaque zone et que le moindre décalage de
// 1 pixel fait passer pour une « couleur perdue ».
std::vector<char> open3x3(const std::vector<char>& m, int w, int h) {
    const auto at = [&](const std::vector<char>& v, int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && v[static_cast<std::size_t>(y) * w + x] != 0;
    };
    std::vector<char> eroded(m.size(), 0), out(m.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool all = true;
            for (int dy = -1; dy <= 1 && all; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    all = all && at(m, x + dx, y + dy);
                }
            }
            eroded[static_cast<std::size_t>(y) * w + x] = all ? 1 : 0;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool any = false;
            for (int dy = -1; dy <= 1 && !any; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    any = any || at(eroded, x + dx, y + dy);
                }
            }
            out[static_cast<std::size_t>(y) * w + x] = any ? 1 : 0;
        }
    }
    return out;
}

struct FidelityData {
    FidelityReport report;
    json doc;
    std::vector<Component> holes;
    std::size_t hole_count{0};
};

Result<FidelityData> compute_fidelity(const document::Project& project, const ThreadModel& model,
                                      const ThreadRaster& raster, const FidelityOptions& opt) {
    const image::Image src = detail::working_image(project);
    if (src.empty()) {
        return std::unexpected(make_error(ErrorCategory::OperationImpossible,
                                          "fidélité impossible : le projet n'a pas d'image source "
                                          "(utiliser l'opération set_source_image)"));
    }
    const int w = src.width;
    const int h = src.height;
    const double mmpp = project.mm_per_px.value;
    const double px_mm2 = mmpp * mmpp;
    const std::size_t n = static_cast<std::size_t>(w) * h;

    const auto bg = background_colour(project, model);
    const Lab bgLab = bg ? detail::to_lab((*bg)[0], (*bg)[1], (*bg)[2]) : Lab{};

    std::vector<Lab> palLab;
    for (const auto& c : model.palette) {
        palLab.push_back(detail::to_lab(c[0], c[1], c[2]));
    }

    std::vector<char> fg(n, 0);
    std::vector<Lab> labs(n);
    std::unordered_map<std::uint32_t, Lab> cache;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = src.rgba.data() + i * 4;
        if (p[3] < 128) {
            continue;
        }
        const std::uint32_t key = (std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) | p[2];
        auto it = cache.find(key);
        if (it == cache.end()) {
            it = cache.emplace(key, detail::to_lab(p[0], p[1], p[2])).first;
        }
        labs[i] = it->second;
        fg[i] = (bg && detail::delta_e(it->second, bgLab) <= opt.background_tolerance) ? 0 : 1;
    }

    std::vector<char> nonfg(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        nonfg[i] = fg[i] == 0 ? 1 : 0;
    }
    const auto dist = chebyshev_distance(nonfg, w, h);
    const int zone = std::max(1, static_cast<int>(std::ceil(opt.hole_zone_mm / mmpp)));

    std::vector<char> missing(n, 0), hole(n, 0), lost(n, 0);
    std::size_t motif = 0, nMissing = 0, nHole = 0, nLost = 0, nWrong = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (fg[i] == 0) {
            continue;
        }
        ++motif;
        if (raster.covered[i] == 0) {
            missing[i] = 1;
            ++nMissing;
            if (dist[i] >= zone) {
                hole[i] = 1;
                ++nHole;
            }
            continue;
        }
        const std::size_t cover = static_cast<std::size_t>(raster.colour[i]) - 1;
        const double dCover = detail::delta_e(labs[i], palLab[cover]);
        if (dCover > opt.wrong_colour) {
            ++nWrong;
            std::size_t nearest = 0;
            double best = 1e9;
            for (std::size_t k = 0; k < palLab.size(); ++k) {
                const double d = detail::delta_e(labs[i], palLab[k]);
                if (d < best) {
                    best = d;
                    nearest = k;
                }
            }
            if (nearest != cover && best < opt.near_palette) {
                lost[i] = 1;
                ++nLost;
            }
        }
    }

    lost = open3x3(lost, w, h);
    nLost = static_cast<std::size_t>(std::count(lost.begin(), lost.end(), char{1}));

    FidelityData out;
    std::size_t holeTotal = 0;
    out.holes = detail::components(hole, w, h, opt.largest, &holeTotal);
    out.hole_count = holeTotal;
    std::size_t lostTotal = 0;
    const auto lostTop = detail::components(lost, w, h, 5, &lostTotal);

    const double motifMm2 = static_cast<double>(motif) * px_mm2;
    FidelityReport& r = out.report;
    r.motif_mm2 = motifMm2;
    r.missing_mm2 = static_cast<double>(nMissing) * px_mm2;
    r.interior_hole_mm2 = static_cast<double>(nHole) * px_mm2;
    r.lost_detail_mm2 = static_cast<double>(nLost) * px_mm2;
    r.interior_hole_ratio = motif ? static_cast<double>(nHole) / static_cast<double>(motif) : 0.0;
    r.lost_detail_ratio = motif ? static_cast<double>(nLost) / static_cast<double>(motif) : 0.0;

    const auto where = [&](const Component& c) {
        return json{{"area_mm2", round_to(static_cast<double>(c.area) * px_mm2, 2)},
                    {"x_px", static_cast<int>(c.cx)},
                    {"y_px", static_cast<int>(c.cy)},
                    {"x_mm", round_to(detail::px_to_mm_x(c.cx, w, mmpp), 2)},
                    {"y_mm", round_to(detail::px_to_mm_y(c.cy, h, mmpp), 2)}};
    };
    json holes = json::array();
    for (const auto& c : out.holes) {
        holes.push_back(where(c));
    }
    json lostArr = json::array();
    for (const auto& c : lostTop) {
        lostArr.push_back(where(c));
    }
    const auto ratio = [&](std::size_t count) {
        return motif ? round_to(100.0 * static_cast<double>(count) / static_cast<double>(motif), 3)
                     : 0.0;
    };
    out.doc = json{{"schema", kFidelitySchema},
                   {"image", {{"width_px", w}, {"height_px", h}, {"mm_per_px", mmpp}}},
                   {"background", bg ? json(rgb_to_hex(*bg)) : json(nullptr)},
                   {"motif_mm2", round_to(motifMm2, 1)},
                   {"missing", {{"mm2", round_to(r.missing_mm2, 2)}, {"percent", ratio(nMissing)}}},
                   {"interior_holes",
                    {{"mm2", round_to(r.interior_hole_mm2, 2)},
                     {"percent", ratio(nHole)},
                     {"count", holeTotal},
                     {"largest", holes}}},
                   {"lost_detail",
                    {{"mm2", round_to(r.lost_detail_mm2, 2)},
                     {"percent", ratio(nLost)},
                     {"count", lostTotal},
                     {"largest", lostArr}}},
                   {"wrong_colour", {{"percent", ratio(nWrong)}}},
                   {"settings",
                    {{"thread_mm", opt.thread_mm},
                     {"hole_zone_mm", opt.hole_zone_mm},
                     {"background_tolerance", opt.background_tolerance},
                     {"near_palette", opt.near_palette},
                     {"wrong_colour", opt.wrong_colour}}}};
    r.json = out.doc.dump(2);
    {
        std::string t = "Fidélité à l'image source\n";
        t += "  motif à broder       : " + std::to_string(static_cast<int>(motifMm2)) + " mm2\n";
        t += "  sans fil (bords)     : " + std::to_string(static_cast<int>(r.missing_mm2)) +
             " mm2 (" + std::to_string(ratio(nMissing)).substr(0, 5) + " %)\n";
        t += "  TISSU VISIBLE dedans : " + std::to_string(static_cast<int>(r.interior_hole_mm2)) +
             " mm2 (" + std::to_string(ratio(nHole)).substr(0, 5) + " %), " +
             std::to_string(holeTotal) + " trous\n";
        for (const auto& c : out.holes) {
            t += "      " + std::to_string(c.area * px_mm2).substr(0, 5) + " mm2 en (x " +
                 std::to_string(static_cast<int>(detail::px_to_mm_x(c.cx, w, mmpp))) + ", y " +
                 std::to_string(static_cast<int>(detail::px_to_mm_y(c.cy, h, mmpp))) +
                 " mm) = pixel " + std::to_string(static_cast<int>(c.cx)) + "," +
                 std::to_string(static_cast<int>(c.cy)) + "\n";
        }
        t += "  détails perdus       : " + std::to_string(static_cast<int>(r.lost_detail_mm2)) +
             " mm2 (" + std::to_string(ratio(nLost)).substr(0, 5) + " %)\n";
        r.text = std::move(t);
    }

    if (opt.error_map) {
        image::Image map;
        map.width = w;
        map.height = h;
        map.rgba.assign(n * 4, 255);
        for (std::size_t i = 0; i < n; ++i) {
            std::uint8_t c[3] = {235, 235, 235};
            if (fg[i] != 0) {
                c[0] = c[1] = c[2] = 200;
                if (hole[i] != 0) {
                    c[0] = 255, c[1] = 0, c[2] = 0;
                } else if (missing[i] != 0) {
                    c[0] = 255, c[1] = 170, c[2] = 170;
                } else if (lost[i] != 0) {
                    c[0] = 255, c[1] = 200, c[2] = 0;
                }
            }
            map.rgba[i * 4] = c[0];
            map.rgba[i * 4 + 1] = c[1];
            map.rgba[i * 4 + 2] = c[2];
        }
        auto png = image::encode_png(map);
        if (!png) {
            return std::unexpected(png.error());
        }
        r.error_map_png = std::move(*png);
    }
    return out;
}

struct DensityData {
    DensityReport report;
    json doc;
    std::vector<json> hotspots;
};

Result<DensityData> compute_density(const document::Project& project, const ThreadModel& model,
                                    const DensityOptions& opt) {
    const image::Image work = detail::working_image(project);
    if (!(opt.cell_mm > 0.05) || !(opt.reference_spacing_mm > 0.0)) {
        return std::unexpected(
            make_error(ErrorCategory::UserInput,
                       "densité : cellule > 0,05 mm et écart de référence > 0 requis"));
    }
    const double cell = opt.cell_mm;
    const int cols = static_cast<int>(std::ceil((model.max_x - model.min_x) / cell)) + 1;
    const int rows = static_cast<int>(std::ceil((model.max_y - model.min_y) / cell)) + 1;
    if (cols <= 0 || rows <= 0 || static_cast<double>(cols) * rows > 50e6) {
        return std::unexpected(make_error(ErrorCategory::OperationImpossible,
                                          "densité : grille trop grande (motif hors normes ?)"));
    }
    std::vector<double> length(static_cast<std::size_t>(cols) * rows, 0.0);
    const auto cell_of = [&](double x, double y) {
        const int cx = std::clamp(static_cast<int>((x - model.min_x) / cell), 0, cols - 1);
        const int cy = std::clamp(static_cast<int>((model.max_y - y) / cell), 0, rows - 1);
        return static_cast<std::size_t>(cy) * cols + cx;
    };
    constexpr double kStep = 0.1;
    const auto walk = [&](const detail::ThreadSeg& s, auto&& visit) {
        const double len = std::hypot(s.bx - s.ax, s.by - s.ay);
        const int k = std::max(1, static_cast<int>(std::ceil(len / kStep)));
        for (int i = 0; i < k; ++i) {
            const double t = (i + 0.5) / k;
            visit(cell_of(s.ax + t * (s.bx - s.ax), s.ay + t * (s.by - s.ay)), len / k);
        }
    };
    for (const auto& s : model.segs) {
        walk(s, [&](std::size_t c, double dl) { length[c] += dl; });
    }
    const double perLayer = cell * cell / opt.reference_spacing_mm; // mm de fil pour 1 épaisseur
    std::vector<double> layers(length.size());
    std::vector<double> covered;
    for (std::size_t i = 0; i < length.size(); ++i) {
        layers[i] = length[i] / perLayer;
        if (length[i] > 0.0) {
            covered.push_back(layers[i]);
        }
    }
    std::sort(covered.begin(), covered.end());
    DensityData out;
    DensityReport& r = out.report;
    r.layers_max = covered.empty() ? 0.0 : covered.back();
    r.layers_p99 =
        covered.empty() ? 0.0 : covered[static_cast<std::size_t>(0.99 * (covered.size() - 1))];
    double mean = 0.0;
    for (const double v : covered) {
        mean += v;
    }
    mean = covered.empty() ? 0.0 : mean / static_cast<double>(covered.size());
    const double cellMm2 = cell * cell;
    const auto area_over = [&](double thr) {
        return static_cast<double>(std::count_if(covered.begin(), covered.end(),
                                                 [&](double v) { return v >= thr; })) *
               cellMm2;
    };
    r.hotspot_area_mm2 = area_over(opt.hotspot_layers);

    std::vector<char> hot(layers.size(), 0);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        hot[i] = layers[i] >= opt.hotspot_layers ? 1 : 0;
    }
    std::size_t hotTotal = 0;
    const auto comps = detail::components(hot, cols, rows, 8, &hotTotal);
    // Objets qui contribuent à chaque zone chaude : second passage sur les brins.
    std::vector<std::int8_t> owner(layers.size(), -1);
    for (std::size_t k = 0; k < comps.size(); ++k) {
        for (int y = comps[k].min_y; y <= comps[k].max_y; ++y) {
            for (int x = comps[k].min_x; x <= comps[k].max_x; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * cols + x;
                if (hot[i] != 0 && owner[i] < 0) {
                    owner[i] = static_cast<std::int8_t>(k);
                }
            }
        }
    }
    std::vector<std::map<std::uint32_t, double>> contrib(comps.size());
    for (const auto& s : model.segs) {
        walk(s, [&](std::size_t c, double dl) {
            if (owner[c] >= 0) {
                contrib[static_cast<std::size_t>(owner[c])][s.object] += dl;
            }
        });
    }
    const double mmpp = project.mm_per_px.value;
    json hotspots = json::array();
    for (std::size_t k = 0; k < comps.size(); ++k) {
        const Component& c = comps[k];
        const double x_mm = model.min_x + c.cx * cell;
        const double y_mm = model.max_y - c.cy * cell;
        double peak = 0.0;
        for (int y = c.min_y; y <= c.max_y; ++y) {
            for (int x = c.min_x; x <= c.max_x; ++x) {
                peak = std::max(peak, layers[static_cast<std::size_t>(y) * cols + x]);
            }
        }
        std::vector<std::pair<double, std::uint32_t>> byLen;
        for (const auto& [id, l] : contrib[k]) {
            byLen.emplace_back(l, id);
        }
        std::sort(byLen.begin(), byLen.end(), std::greater<>());
        json ids = json::array();
        for (std::size_t i = 0; i < byLen.size() && i < 6; ++i) {
            ids.push_back(byLen[i].second);
        }
        json h = {{"area_mm2", round_to(static_cast<double>(c.area) * cellMm2, 1)},
                  {"max_layers", round_to(peak, 2)},
                  {"x_mm", round_to(x_mm, 2)},
                  {"y_mm", round_to(y_mm, 2)},
                  {"objects", ids}};
        if (!work.empty()) {
            h["x_px"] = static_cast<int>(detail::mm_to_px_x(x_mm, work.width, mmpp));
            h["y_px"] = static_cast<int>(detail::mm_to_px_y(y_mm, work.height, mmpp));
        }
        out.hotspots.push_back(h);
        hotspots.push_back(h);
    }
    out.doc = json{{"schema", kDensitySchema},
                   {"cell_mm", cell},
                   {"reference_spacing_mm", opt.reference_spacing_mm},
                   {"layers_mean", round_to(mean, 2)},
                   {"layers_p99", round_to(r.layers_p99, 2)},
                   {"layers_max", round_to(r.layers_max, 2)},
                   {"area_over_mm2",
                    {{"2_layers", round_to(area_over(2.0), 1)},
                     {"3_layers", round_to(area_over(3.0), 1)},
                     {"4_layers", round_to(area_over(4.0), 1)}}},
                   {"hotspot_layers", opt.hotspot_layers},
                   {"hotspot_area_mm2", round_to(r.hotspot_area_mm2, 1)},
                   {"hotspot_count", hotTotal},
                   {"hotspots", hotspots}};
    r.json = out.doc.dump(2);
    {
        std::string t = "Épaisseurs de fil (1 = un remplissage à " +
                        std::to_string(opt.reference_spacing_mm).substr(0, 4) + " mm)\n";
        t += "  moyenne " + std::to_string(mean).substr(0, 4) + " ; p99 " +
             std::to_string(r.layers_p99).substr(0, 4) + " ; max " +
             std::to_string(r.layers_max).substr(0, 4) + "\n";
        t += "  surface >= 2 : " + std::to_string(static_cast<int>(area_over(2.0))) +
             " mm2 ; >= 3 : " + std::to_string(static_cast<int>(area_over(3.0))) +
             " mm2 ; >= 4 : " + std::to_string(static_cast<int>(area_over(4.0))) + " mm2\n";
        for (const auto& h : out.hotspots) {
            t += "  zone chaude " + std::to_string(h["area_mm2"].get<double>()).substr(0, 5) +
                 " mm2, max " + std::to_string(h["max_layers"].get<double>()).substr(0, 4) +
                 " en (x " + std::to_string(static_cast<int>(h["x_mm"].get<double>())) + ", y " +
                 std::to_string(static_cast<int>(h["y_mm"].get<double>())) + " mm), objets";
            for (const auto& id : h["objects"]) {
                t += " " + std::to_string(id.get<std::uint64_t>());
            }
            t += "\n";
        }
        r.text = std::move(t);
    }

    if (opt.heatmap) {
        const int scale = std::max(1, static_cast<int>(std::lround(opt.heatmap_px_per_cell)));
        image::Image img;
        img.width = cols * scale;
        img.height = rows * scale;
        img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 255);
        struct Stop {
            double v;
            std::array<double, 3> c;
        };
        static const Stop ramp[] = {{0.0, {235, 235, 235}}, {0.5, {70, 130, 255}},
                                    {1.0, {70, 200, 100}},  {2.0, {250, 220, 60}},
                                    {3.0, {250, 140, 40}},  {4.0, {230, 30, 30}}};
        const auto colour = [&](double v) {
            if (v <= 0.0) {
                return ramp[0].c;
            }
            for (std::size_t i = 1; i < std::size(ramp); ++i) {
                if (v <= ramp[i].v) {
                    const double t = (v - ramp[i - 1].v) / (ramp[i].v - ramp[i - 1].v);
                    std::array<double, 3> c{};
                    for (int k = 0; k < 3; ++k) {
                        c[static_cast<std::size_t>(k)] =
                            ramp[i - 1].c[static_cast<std::size_t>(k)] * (1 - t) +
                            ramp[i].c[static_cast<std::size_t>(k)] * t;
                    }
                    return c;
                }
            }
            return ramp[std::size(ramp) - 1].c;
        };
        for (int y = 0; y < img.height; ++y) {
            for (int x = 0; x < img.width; ++x) {
                const auto c =
                    colour(layers[static_cast<std::size_t>(y / scale) * cols + x / scale]);
                std::uint8_t* p =
                    img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
                p[0] = static_cast<std::uint8_t>(c[0]);
                p[1] = static_cast<std::uint8_t>(c[1]);
                p[2] = static_cast<std::uint8_t>(c[2]);
            }
        }
        auto png = image::encode_png(img);
        if (!png) {
            return std::unexpected(png.error());
        }
        r.heatmap_png = std::move(*png);
    }
    return out;
}

double angle_deviation(double a, double b) {
    double d = std::fmod(std::fabs(a - b), kPi);
    return std::min(d, kPi - d);
}

} // namespace

Result<FidelityReport> measure_fidelity(const document::Project& project,
                                        const FidelityOptions& options) {
    const image::Image work = detail::working_image(project);
    auto model = detail::build_thread_model(project);
    if (!model) {
        return std::unexpected(model.error());
    }
    if (work.empty()) {
        return std::unexpected(make_error(ErrorCategory::OperationImpossible,
                                          "fidélité impossible : le projet n'a pas d'image source "
                                          "(utiliser l'opération set_source_image)"));
    }
    const auto raster = detail::rasterize_threads(*model, work.width, work.height,
                                                  project.mm_per_px.value, options.thread_mm);
    auto data = compute_fidelity(project, *model, raster, options);
    if (!data) {
        return std::unexpected(data.error());
    }
    return std::move(data->report);
}

Result<DensityReport> measure_density(const document::Project& project,
                                      const DensityOptions& options) {
    const image::Image work = detail::working_image(project);
    auto model = detail::build_thread_model(project);
    if (!model) {
        return std::unexpected(model.error());
    }
    auto data = compute_density(project, *model, options);
    if (!data) {
        return std::unexpected(data.error());
    }
    return std::move(data->report);
}

std::pair<int, int> working_size(const document::Project& project) {
    const image::Image work = detail::working_image(project);
    return {work.width, work.height};
}

Result<LocateReport> locate_objects(const document::Project& project, const LocateQuery& query) {
    const image::Image work = detail::working_image(project);
    auto model = detail::build_thread_model(project);
    if (!model) {
        return std::unexpected(model.error());
    }
    const double reach = query.radius_mm + 0.2; // rayon de la demande + demi-fil
    struct Hit {
        std::size_t segments{0};
        std::uint32_t last_order{0};
        std::map<std::string, std::size_t> passes;
    };
    std::map<std::uint32_t, Hit> hits;
    static const char* const kPass[] = {"underlay", "top", "travel", "lock", "manual"};
    for (const auto& s : model->segs) {
        const double dx = s.bx - s.ax;
        const double dy = s.by - s.ay;
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0.0 ? ((query.x_mm - s.ax) * dx + (query.y_mm - s.ay) * dy) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double qx = s.ax + t * dx - query.x_mm;
        const double qy = s.ay + t * dy - query.y_mm;
        if (qx * qx + qy * qy <= reach * reach) {
            Hit& h = hits[s.object];
            ++h.segments;
            h.last_order = std::max(h.last_order, s.order);
            ++h.passes[kPass[static_cast<std::size_t>(s.pass)]];
        }
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> byOrder;
    for (const auto& [id, h] : hits) {
        byOrder.emplace_back(h.last_order, id);
    }
    std::sort(byOrder.begin(), byOrder.end(), std::greater<>());
    json objs = json::array();
    for (const auto& [order, id] : byOrder) {
        const Hit& h = hits[id];
        const auto* obj = model->object(project, id);
        json o = {{"id", id},
                  {"name", obj ? obj->name : ""},
                  {"type", obj ? stitch_type_name(*obj) : "?"},
                  {"color", obj ? rgb_to_hex(obj->rgb) : "?"},
                  {"document_rank", model->rank_of(id)},
                  {"segments_near", h.segments},
                  {"passes", h.passes},
                  {"sewn_last_order", order}};
        if (obj != nullptr) {
            if (const auto* tp = std::get_if<document::TatamiParams>(&obj->params)) {
                o["angle_deg"] = round_to(tp->angle.radians * 180.0 / kPi, 1);
                o["row_spacing_mm"] = static_cast<double>(tp->row_spacing.value) / 1000.0;
                o["hidden_underpath"] = tp->hidden_underpath;
            }
        }
        objs.push_back(std::move(o));
    }
    const double mmpp = project.mm_per_px.value;
    json q = {{"x_mm", query.x_mm}, {"y_mm", query.y_mm}, {"radius_mm", query.radius_mm}};
    if (!work.empty()) {
        q["x_px"] = round_to(detail::mm_to_px_x(query.x_mm, work.width, mmpp), 1);
        q["y_px"] = round_to(detail::mm_to_px_y(query.y_mm, work.height, mmpp), 1);
    }
    json doc = {{"schema", kLocateSchema},
                {"query", q},
                {"top_object", byOrder.empty() ? json(nullptr) : json(byOrder.front().second)},
                {"objects", objs}};
    LocateReport rep;
    rep.json = doc.dump(2);
    std::string t = "Brins à moins de " + std::to_string(query.radius_mm).substr(0, 4) +
                    " mm de (x " + std::to_string(query.x_mm).substr(0, 6) + ", y " +
                    std::to_string(query.y_mm).substr(0, 6) + " mm), du dernier cousu (visible " +
                    "dessus) au premier\n";
    if (byOrder.empty()) {
        t += "  aucun : le tissu est visible ici\n";
    }
    for (const auto& [order, id] : byOrder) {
        const auto* obj = model->object(project, id);
        t += "  objet " + std::to_string(id) + "  " + (obj ? stitch_type_name(*obj) : "?") + "  " +
             (obj ? rgb_to_hex(obj->rgb) : "?") + "  « " + (obj ? obj->name : "") + " »  " +
             std::to_string(hits[id].segments) + " brins\n";
        rep.objects.push_back(id);
    }
    rep.text = std::move(t);
    return rep;
}

Result<DiagnoseReport> diagnose_project(const document::Project& project) {
    const image::Image work = detail::working_image(project);
    auto model = detail::build_thread_model(project);
    if (!model) {
        return std::unexpected(model.error());
    }
    DiagnoseReport report;
    const auto add = [&](Finding f) {
        (f.severity == "error"     ? report.errors
         : f.severity == "warning" ? report.warnings
                                   : report.infos)++;
        report.findings.push_back(std::move(f));
    };
    const double mmpp = project.mm_per_px.value;
    const bool hasImage = !work.empty();
    const double motifW = model->max_x - model->min_x;
    const double motifH = model->max_y - model->min_y;
    const double motifDiag = std::hypot(motifW, motifH);
    report.motif_width_mm = motifW;
    report.motif_height_mm = motifH;
    report.stitches = model->stats.stitches;
    report.jumps = model->stats.jumps;
    report.trims = model->stats.trims;
    report.color_changes = model->stats.color_changes;
    report.thread_m = model->stats.thread_length_um / 1e6;

    // 1. Motif plus grand que l'image source : points aberrants (défaut des poignées de Bézier,
    //    trouvé à 2 m du motif).
    if (hasImage) {
        const double halfW = work.width * mmpp / 2.0 + 3.0;
        const double halfH = work.height * mmpp / 2.0 + 3.0;
        std::set<std::uint64_t> out;
        for (const auto& s : model->segs) {
            if (std::fabs(s.ax) > halfW || std::fabs(s.bx) > halfW || std::fabs(s.ay) > halfH ||
                std::fabs(s.by) > halfH) {
                out.insert(s.object);
            }
        }
        if (!out.empty()) {
            Finding f;
            f.severity = "error";
            f.code = "motif-hors-image";
            f.message = "Des points sortent de l'image source (motif de " +
                        std::to_string(static_cast<int>(motifW)) + " x " +
                        std::to_string(static_cast<int>(motifH)) + " mm pour une image de " +
                        std::to_string(static_cast<int>(work.width * mmpp)) + " x " +
                        std::to_string(static_cast<int>(work.height * mmpp)) +
                        " mm) : géométrie aberrante.";
            f.suggestion = "Inspecter ces objets (`project inspect --object ID --geometry`) ; "
                           "essayer sans --curve-tolerance.";
            f.objects.assign(out.begin(), out.end());
            if (f.objects.size() > 10) {
                f.objects.resize(10);
            }
            add(std::move(f));
        }
    }

    // 2. Objet étalé : grande surface très creuse sur une grande part du motif (réseau de barres).
    for (const auto& e : project.embroidery_objects) {
        if (!e.visible) {
            continue;
        }
        const document::VectorObject* vec = nullptr;
        for (const auto& v : project.vector_objects) {
            if (v.id == e.source_vector) {
                vec = &v;
                break;
            }
        }
        if (vec == nullptr) {
            continue;
        }
        double area = 0.0;
        double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
        for (const auto& set : vec->paths) {
            area += geometry::path_set_area_um2(set) / 1e6;
            for (const auto& nd : set.outer.nodes) {
                x0 = std::min(x0, nd.pos.x.value / 1000.0);
                x1 = std::max(x1, nd.pos.x.value / 1000.0);
                y0 = std::min(y0, nd.pos.y.value / 1000.0);
                y1 = std::max(y1, nd.pos.y.value / 1000.0);
            }
        }
        const double bw = x1 - x0;
        const double bh = y1 - y0;
        if (bw <= 0.0 || bh <= 0.0) {
            continue;
        }
        const double fill = area / (bw * bh);
        if (area >= 300.0 && fill < 0.08 && std::hypot(bw, bh) >= 0.45 * motifDiag) {
            Finding f;
            f.severity = "warning";
            f.code = "objet-etale";
            f.message = "L'objet « " + e.name + " » (" + std::to_string(static_cast<int>(area)) +
                        " mm2) s'étale sur " + std::to_string(static_cast<int>(bw)) + " x " +
                        std::to_string(static_cast<int>(bh)) +
                        " mm avec un taux de remplissage de " +
                        std::to_string(static_cast<int>(fill * 100.0)) +
                        " % : réseau de formes fines relié en un seul objet.";
            f.suggestion = "Séparer l'objet (`break_apart`) ou revoir --dark-lines-bars / "
                           "--dark-lines-lightness.";
            f.objects.push_back(e.id.value);
            f.at_mm = std::array<double, 2>{(x0 + x1) / 2.0, (y0 + y1) / 2.0};
            add(std::move(f));
        }
    }

    // 3. Quasi-doublons de fil : un changement de fil pour rien.
    {
        std::vector<Lab> labs;
        for (const auto& c : model->palette) {
            labs.push_back(detail::to_lab(c[0], c[1], c[2]));
        }
        for (std::size_t a = 0; a < labs.size(); ++a) {
            for (std::size_t b = a + 1; b < labs.size(); ++b) {
                const double d = detail::delta_e(labs[a], labs[b]);
                if (d < 12.0) {
                    Finding f;
                    f.severity = "info";
                    f.code = "fils-quasi-identiques";
                    f.message = "Les fils " + rgb_to_hex(model->palette[a]) + " et " +
                                rgb_to_hex(model->palette[b]) + " sont presque identiques (ΔE " +
                                std::to_string(static_cast<int>(d)) +
                                ") : un changement de fil de plus pour rien.";
                    f.suggestion = "Fusionner ces deux couleurs (`set_color`) ou baisser "
                                   "--max-colors.";
                    add(std::move(f));
                }
            }
        }
    }

    // 4. Tissu visible à l'intérieur du motif.
    if (hasImage) {
        const auto raster = detail::rasterize_threads(*model, work.width, work.height, mmpp, 0.55);
        FidelityOptions fo;
        auto fid = compute_fidelity(project, *model, raster, fo);
        if (fid) {
            report.interior_hole_ratio = fid->report.interior_hole_ratio;
            report.lost_detail_ratio = fid->report.lost_detail_ratio;
            const double pct = 100.0 * fid->report.interior_hole_ratio;
            if (pct > 0.15) {
                Finding f;
                f.severity = pct > 0.8 ? "warning" : "info";
                f.code = "tissu-visible";
                f.message = "Du tissu est visible à l'intérieur du motif : " +
                            std::to_string(static_cast<int>(fid->report.interior_hole_mm2)) +
                            " mm2 (" + std::to_string(pct).substr(0, 4) + " % du motif), " +
                            std::to_string(fid->hole_count) + " trous.";
                f.suggestion = "Plus gros trous : `project fidelity` ; pistes : --fill-inset 0, "
                               "--dark-lines-lightness plus bas (le marine est pris pour du noir), "
                               "--min-region-mm2 plus bas.";
                if (!fid->holes.empty()) {
                    f.at_mm = std::array<double, 2>{
                        detail::px_to_mm_x(fid->holes[0].cx, work.width, mmpp),
                        detail::px_to_mm_y(fid->holes[0].cy, work.height, mmpp)};
                }
                add(std::move(f));
            }
            const double lostPct = 100.0 * fid->report.lost_detail_ratio;
            if (lostPct > 1.0) {
                Finding f;
                f.severity = "warning";
                f.code = "details-perdus";
                f.message = std::to_string(lostPct).substr(0, 4) +
                            " % du motif a perdu sa couleur au profit d'une voisine.";
                f.suggestion = "Baisser --island-cleanup (0 garde les petits îlots) et "
                               "--min-region-mm2.";
                add(std::move(f));
            }
        }
    }

    // 5. Surépaisseurs de fil.
    {
        DensityOptions dopt;
        auto dens = compute_density(project, *model, dopt);
        if (dens) {
            report.hotspot_area_mm2 = dens->report.hotspot_area_mm2;
            report.layers_max = dens->report.layers_max;
        }
        if (dens && dens->report.hotspot_area_mm2 >= 20.0) {
            Finding f;
            f.severity = "warning";
            f.code = "surepaisseur";
            f.message =
                std::to_string(static_cast<int>(dens->report.hotspot_area_mm2)) +
                " mm2 portent au moins " + std::to_string(static_cast<int>(dopt.hotspot_layers)) +
                " épaisseurs de fil (max " + std::to_string(dens->report.layers_max).substr(0, 4) +
                ") : plissement et "
                "casse d'aiguille probables.";
            f.suggestion = "Voir la carte (`project density --heatmap`) ; réduire les passes "
                           "superposées (traits triples, barres sur remplissage).";
            if (!dens->hotspots.empty()) {
                const auto& h = dens->hotspots.front();
                f.at_mm = std::array<double, 2>{h["x_mm"].get<double>(), h["y_mm"].get<double>()};
                for (const auto& id : h["objects"]) {
                    f.objects.push_back(id.get<std::uint64_t>());
                }
            }
            add(std::move(f));
        }
    }

    // 6. Liaisons visibles par-dessus un remplissage (brins qui s'écartent de l'angle des rangées).
    {
        double total = 0.0;
        double off = 0.0;
        std::map<std::uint32_t, double> byObject;
        for (const auto& s : model->segs) {
            if (s.pass != stitch::StitchPass::TopStitch) {
                continue;
            }
            const auto* obj = model->object(project, s.object);
            const auto* tp = obj ? std::get_if<document::TatamiParams>(&obj->params) : nullptr;
            if (tp == nullptr) {
                continue;
            }
            const double len = std::hypot(s.bx - s.ax, s.by - s.ay);
            total += len;
            if (len >= 1.0 && angle_deviation(std::atan2(s.by - s.ay, s.bx - s.ax),
                                              tp->angle.radians) > 25.0 * kPi / 180.0) {
                off += len;
                byObject[s.object] += len;
            }
        }
        report.connector_ratio = total > 0.0 ? off / total : 0.0;
        if (total > 0.0 && off / total > 0.005) {
            std::vector<std::pair<double, std::uint32_t>> v;
            for (const auto& [id, l] : byObject) {
                v.emplace_back(l, id);
            }
            std::sort(v.begin(), v.end(), std::greater<>());
            Finding f;
            f.severity = "warning";
            f.code = "liaisons-visibles";
            f.message = std::to_string(100.0 * off / total).substr(0, 4) +
                        " % du fil des remplissages (" + std::to_string(static_cast<int>(off)) +
                        " mm) est posé en brins obliques par-dessus les rangées : liaisons entre "
                        "morceaux visibles.";
            f.suggestion = "--no-underpath --hidden-travel (remplissages propres, plus de coupes).";
            for (std::size_t i = 0; i < v.size() && i < 8; ++i) {
                f.objects.push_back(v[i].second);
            }
            add(std::move(f));
        }
    }

    json arr = json::array();
    for (const auto& f : report.findings) {
        json j = {{"severity", f.severity},
                  {"code", f.code},
                  {"message", f.message},
                  {"suggestion", f.suggestion},
                  {"objects", f.objects}};
        if (f.at_mm) {
            j["at_mm"] = {round_to((*f.at_mm)[0], 2), round_to((*f.at_mm)[1], 2)};
            if (hasImage) {
                j["at_px"] = {
                    static_cast<int>(detail::mm_to_px_x((*f.at_mm)[0], work.width, mmpp)),
                    static_cast<int>(detail::mm_to_px_y((*f.at_mm)[1], work.height, mmpp))};
            }
        }
        arr.push_back(std::move(j));
    }
    json doc = {
        {"schema", kDiagnoseSchema},
        {"summary",
         {{"errors", report.errors}, {"warnings", report.warnings}, {"infos", report.infos}}},
        {"metrics",
         {{"interior_hole_percent", round_to(100.0 * report.interior_hole_ratio, 3)},
          {"lost_detail_percent", round_to(100.0 * report.lost_detail_ratio, 3)},
          {"hotspot_area_mm2", round_to(report.hotspot_area_mm2, 1)},
          {"layers_max", round_to(report.layers_max, 2)},
          {"connector_percent", round_to(100.0 * report.connector_ratio, 3)},
          {"motif_width_mm", round_to(report.motif_width_mm, 1)},
          {"motif_height_mm", round_to(report.motif_height_mm, 1)},
          {"stitches", report.stitches},
          {"jumps", report.jumps},
          {"trims", report.trims},
          {"color_changes", report.color_changes},
          {"thread_m", round_to(report.thread_m, 1)}}},
        {"findings", arr}};
    report.json = doc.dump(2);
    std::string t = "Diagnostic : " + std::to_string(report.errors) + " erreur(s), " +
                    std::to_string(report.warnings) + " avertissement(s), " +
                    std::to_string(report.infos) + " info(s)\n";
    for (const auto& f : report.findings) {
        t += "  [" + f.severity + "] " + f.code + " : " + f.message + "\n";
        if (!f.suggestion.empty()) {
            t += "      -> " + f.suggestion + "\n";
        }
        if (!f.objects.empty()) {
            t += "      objets :";
            for (const auto id : f.objects) {
                t += " " + std::to_string(id);
            }
            t += "\n";
        }
        if (f.at_mm) {
            t += "      en (x " + std::to_string(static_cast<int>((*f.at_mm)[0])) + ", y " +
                 std::to_string(static_cast<int>((*f.at_mm)[1])) + " mm)\n";
        }
    }
    if (report.findings.empty()) {
        t += "  rien à signaler\n";
    }
    report.text = std::move(t);
    return report;
}

} // namespace openstitch::project_ops
