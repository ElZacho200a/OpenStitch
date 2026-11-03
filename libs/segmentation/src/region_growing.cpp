// SPDX-License-Identifier: Apache-2.0
#include "openstitch/segmentation/region_growing.hpp"

#include <algorithm>
#include <cstddef>

namespace openstitch::segmentation {

namespace {

constexpr std::uint8_t kOpaqueAlpha = 128; // alpha < 128 = fond (comme `segment`)

std::size_t index_of(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

// Voisins d'un pixel dans un ordre FIXE (haut, gauche, droite, bas, puis diagonales) : le
// parcours, donc le masque, ne dépend d'aucun détail d'implémentation.
struct Offset {
    int dx;
    int dy;
};
constexpr Offset kNeighbours[8] = {{0, -1},  {-1, 0}, {1, 0},  {0, 1},
                                   {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

// Comble les composantes de pixels hors masque de taille <= `max_px` qui ne touchent pas le bord
// de l'image (4-connexité). Une composante touchant le bord est le monde extérieur, jamais un
// trou.
void fill_small_holes(GrownRegion& g, std::size_t max_px) {
    if (max_px == 0) {
        return;
    }
    const std::size_t total = g.mask.size();
    std::vector<std::uint8_t> seen(total, 0);
    std::vector<std::size_t> comp;
    for (int y = g.y0; y <= g.y1; ++y) {
        for (int x = g.x0; x <= g.x1; ++x) {
            const std::size_t start = index_of(x, y, g.width);
            if (g.mask[start] != 0 || seen[start] != 0) {
                continue;
            }
            comp.clear();
            comp.push_back(start);
            seen[start] = 1;
            bool touches_border = false;
            for (std::size_t head = 0; head < comp.size(); ++head) {
                const std::size_t cur = comp[head];
                const int cx = static_cast<int>(cur % static_cast<std::size_t>(g.width));
                const int cy = static_cast<int>(cur / static_cast<std::size_t>(g.width));
                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + kNeighbours[k].dx;
                    const int ny = cy + kNeighbours[k].dy;
                    if (nx < 0 || ny < 0 || nx >= g.width || ny >= g.height) {
                        touches_border = true;
                        continue;
                    }
                    const std::size_t ni = index_of(nx, ny, g.width);
                    if (g.mask[ni] == 0 && seen[ni] == 0) {
                        seen[ni] = 1;
                        comp.push_back(ni);
                    }
                }
                if (comp.size() > max_px && touches_border) {
                    break; // inutile de continuer : ce n'est de toute façon pas un trou comblé
                }
            }
            if (!touches_border && comp.size() <= max_px) {
                for (const std::size_t i : comp) {
                    g.mask[i] = 1;
                }
            }
        }
    }
}

} // namespace

Result<GrownRegion> grow_region(const image::Image& img, int seed_x, int seed_y,
                                const GrowOptions& options) {
    if (img.width <= 0 || img.height <= 0 ||
        img.rgba.size() !=
            static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4U) {
        return fail(ErrorCategory::UserInput, "Image vide ou incohérente.");
    }
    if (seed_x < 0 || seed_y < 0 || seed_x >= img.width || seed_y >= img.height) {
        return fail(ErrorCategory::UserInput, "Le clic est en dehors de l'image.");
    }
    const auto pixel = [&img](std::size_t i) -> const std::uint8_t* { return &img.rgba[i * 4U]; };
    const std::size_t seed = index_of(seed_x, seed_y, img.width);
    if (pixel(seed)[3] < kOpaqueAlpha) {
        return fail(ErrorCategory::UserInput, "Le pixel cliqué est transparent (fond).");
    }

    GrownRegion out;
    out.width = img.width;
    out.height = img.height;
    out.mask.assign(img.rgba.size() / 4U, 0);
    out.seed_rgb = {pixel(seed)[0], pixel(seed)[1], pixel(seed)[2]};

    const double tolerance = std::max(0.0, options.tolerance);
    const int neighbour_count = options.connect_diagonals ? 8 : 4;

    // Mémo du dernier test : les aplats (cas courant) répètent la même couleur de pixel en pixel.
    std::array<std::uint8_t, 3> last_rgb = out.seed_rgb;
    bool last_ok = true;

    std::vector<std::size_t> queue;
    queue.push_back(seed);
    out.mask[seed] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::size_t cur = queue[head];
        const int cx = static_cast<int>(cur % static_cast<std::size_t>(img.width));
        const int cy = static_cast<int>(cur / static_cast<std::size_t>(img.width));
        for (int k = 0; k < neighbour_count; ++k) {
            const int nx = cx + kNeighbours[k].dx;
            const int ny = cy + kNeighbours[k].dy;
            if (nx < 0 || ny < 0 || nx >= img.width || ny >= img.height) {
                continue;
            }
            const std::size_t ni = index_of(nx, ny, img.width);
            if (out.mask[ni] != 0) {
                continue;
            }
            const std::uint8_t* p = pixel(ni);
            if (p[3] < kOpaqueAlpha) {
                continue;
            }
            const std::array<std::uint8_t, 3> rgb{p[0], p[1], p[2]};
            if (rgb != last_rgb) {
                last_rgb = rgb;
                last_ok = rgb == out.seed_rgb || cielab_delta_e(out.seed_rgb, rgb) <= tolerance;
            }
            if (last_ok) {
                out.mask[ni] = 1;
                queue.push_back(ni);
            }
        }
    }

    // Boîte englobante (balayage du masque : indépendante de l'ordre du parcours).
    out.x0 = img.width;
    out.y0 = img.height;
    out.x1 = -1;
    out.y1 = -1;
    for (const std::size_t i : queue) {
        const int x = static_cast<int>(i % static_cast<std::size_t>(img.width));
        const int y = static_cast<int>(i / static_cast<std::size_t>(img.width));
        out.x0 = std::min(out.x0, x);
        out.y0 = std::min(out.y0, y);
        out.x1 = std::max(out.x1, x);
        out.y1 = std::max(out.y1, y);
    }
    fill_small_holes(out, options.fill_holes_up_to_px);

    // Compte et couleur moyenne (somme entière : exacte, donc déterministe).
    std::uint64_t sum[3] = {0, 0, 0};
    for (int y = out.y0; y <= out.y1; ++y) {
        for (int x = out.x0; x <= out.x1; ++x) {
            const std::size_t i = index_of(x, y, img.width);
            if (out.mask[i] == 0) {
                continue;
            }
            ++out.pixel_count;
            const std::uint8_t* p = pixel(i);
            sum[0] += p[0];
            sum[1] += p[1];
            sum[2] += p[2];
        }
    }
    for (int c = 0; c < 3; ++c) {
        out.mean_rgb[static_cast<std::size_t>(c)] =
            static_cast<std::uint8_t>((sum[c] + out.pixel_count / 2) / out.pixel_count);
    }
    return out;
}

Segmentation segmentation_from_grown(const GrownRegion& grown) {
    Segmentation seg;
    seg.width = grown.width;
    seg.height = grown.height;
    if (grown.empty()) {
        return seg;
    }
    seg.labels.assign(grown.mask.size(), 0);
    for (std::size_t i = 0; i < grown.mask.size(); ++i) {
        seg.labels[i] = grown.mask[i] != 0 ? 1U : 0U;
    }
    Region region;
    region.id = RegionId{1};
    region.rgb = grown.mean_rgb;
    region.pixel_count = grown.pixel_count;
    seg.region_slots.push_back(region);
    return seg;
}

image::Image render_grown_preview(const GrownRegion& grown, std::array<std::uint8_t, 3> tint,
                                  std::uint8_t alpha) {
    image::Image img;
    if (grown.empty()) {
        return img;
    }
    img.width = grown.x1 - grown.x0 + 1;
    img.height = grown.y1 - grown.y0 + 1;
    img.source_had_alpha = true;
    img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4U,
                    0);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            if (!grown.contains(grown.x0 + x, grown.y0 + y)) {
                continue;
            }
            std::uint8_t* p = &img.rgba[index_of(x, y, img.width) * 4U];
            p[0] = tint[0];
            p[1] = tint[1];
            p[2] = tint[2];
            p[3] = alpha;
        }
    }
    return img;
}

} // namespace openstitch::segmentation
