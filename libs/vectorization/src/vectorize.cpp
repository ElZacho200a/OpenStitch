// SPDX-License-Identifier: Apache-2.0
#include "openstitch/vectorization/vectorize.hpp"
#include "openstitch/core/parallel.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/clean.hpp"
#include "openstitch/geometry/curve_fit.hpp"
#include "openstitch/geometry/measure.hpp"
#include "openstitch/geometry/simplify.hpp"

namespace openstitch::vectorization {

namespace {

// Centre de pixel -> coordonnées physiques (µm), origine au centre de
// l'image, Y vers le haut (le repère du modèle, ADR-003).
geometry::PathNode to_model(const cv::Point& px, int width, int height, double mmPerPx) {
    const double xMm = (static_cast<double>(px.x) + 0.5 - width / 2.0) * mmPerPx;
    const double yMm = (height / 2.0 - (static_cast<double>(px.y) + 0.5)) * mmPerPx;
    return geometry::PathNode{
        Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
               Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}},
        geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

double total_area_um2(const std::vector<geometry::PathSet>& sets) {
    double area = 0.0;
    for (const auto& set : sets) {
        area += geometry::path_set_area_um2(set);
    }
    return area;
}

} // namespace

bool is_overlap_grown_contour(const std::vector<geometry::PathSet>& current,
                              const std::vector<geometry::PathSet>& raw, Micrometers max_band) {
    const double rawArea = total_area_um2(raw);
    if (rawArea <= 0.0 || total_area_um2(current) <= rawArea * 1.03) {
        return false;
    }
    double perimeter = 0.0;
    for (const auto& set : raw) {
        perimeter += geometry::path_length_um(set.outer, Micrometers{100});
        for (const auto& hole : set.holes) {
            perimeter += geometry::path_length_um(hole, Micrometers{100});
        }
    }
    const double budget = perimeter * static_cast<double>(max_band.value);
    const auto grown = geometry::difference_polygons(current, raw);
    const auto missing = geometry::difference_polygons(raw, current);
    if (!grown || !missing) {
        return false;
    }
    return total_area_um2(*grown) <= budget && total_area_um2(*missing) <= budget;
}

Result<std::vector<geometry::PathSet>> vectorize_region(const segmentation::Segmentation& seg,
                                                        RegionId id,
                                                        const VectorizeOptions& options) {
    if (seg.find(id) == nullptr) {
        return fail(ErrorCategory::Internal, "Région introuvable",
                    "vectorize id=" + std::to_string(id.value));
    }
    if (options.mm_per_px.value <= 0.0) {
        return fail(ErrorCategory::Internal, "Résolution de travail invalide");
    }

    // Masque binaire de la région, restreint à sa boîte englobante + 1 px de
    // marge éteinte (audit perf 2026-09, docs/performance-audit.md : un
    // masque pleine image par région coûtait O(régions x pixels) à
    // l'auto-numérisation). Hors de la boîte, le masque pleine image ne
    // contenait que des zéros : même parcours de `findContours` (même ordre
    // de balayage relatif), points ramenés au repère image par `offset`.
    const auto label = static_cast<std::uint32_t>(id.value);
    int minX = seg.width, minY = seg.height, maxX = -1, maxY = -1;
    for (int y = 0; y < seg.height; ++y) {
        const std::uint32_t* row =
            seg.labels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width);
        for (int x = 0; x < seg.width; ++x) {
            if (row[x] == label) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = y;
            }
        }
    }
    if (maxX < 0) {
        return fail(ErrorCategory::Internal, "La région n'a produit aucun contour");
    }
    const int x0 = minX - 1;
    const int y0 = minY - 1;
    cv::Mat mask(maxY - minY + 3, maxX - minX + 3, CV_8U, cv::Scalar(0));
    for (int y = minY; y <= maxY; ++y) {
        const std::uint32_t* row =
            seg.labels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width);
        auto* dst = mask.ptr<std::uint8_t>(y - y0);
        for (int x = minX; x <= maxX; ++x) {
            if (row[x] == label) {
                dst[x - x0] = 255;
            }
        }
    }

    // Contours extérieurs ET trous (RETR_CCOMP) ; CHAIN_APPROX_SIMPLE retire
    // déjà les points colinéaires. La hiérarchie exacte est reconstruite plus
    // loin par le nettoyage (règle pair-impair), inutile de l'exploiter ici.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE, cv::Point(x0, y0));
    if (contours.empty()) {
        return fail(ErrorCategory::Internal, "La région n'a produit aucun contour");
    }

    std::vector<geometry::Path> raw;
    raw.reserve(contours.size());
    for (const auto& contour : contours) {
        if (contour.size() < 3) {
            continue; // micro-artefact d'un pixel isolé
        }
        geometry::Path path;
        path.closed = true;
        path.nodes.reserve(contour.size());
        for (const cv::Point& pt : contour) {
            path.nodes.push_back(to_model(pt, seg.width, seg.height, options.mm_per_px.value));
        }
        // Tolérance de simplification ADAPTATIVE : un petit contour (typiquement
        // un trou fin — mât, cordage traversant une voile) ne doit pas être
        // aplati au point de disparaître, sinon le remplissage déborde par
        // dessus l'élément qui devrait rester au-dessus. On borne la tolérance à
        // une fraction du périmètre du contour.
        double perim = 0.0;
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            perim += length_um(path.nodes[(i + 1) % path.nodes.size()].pos - path.nodes[i].pos);
        }
        const auto adaptiveTol = Micrometers{static_cast<std::int32_t>(
            std::min<double>(static_cast<double>(options.simplify_tolerance.value), perim / 16.0))};
        raw.push_back(options.curve_tolerance.value > 0 ? path
                                                        : geometry::simplify(path, adaptiveTol));
    }

    auto sets = geometry::clean_to_path_sets(raw);
    if (!sets) {
        return std::unexpected(sets.error());
    }
    if (sets->empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "La région est trop petite pour produire une forme vectorielle exploitable");
    }
    if (options.curve_tolerance.value > 0) {
        // Ajustement de courbes APRÈS le nettoyage : la topologie (trous,
        // auto-intersections) est déjà corrigée sur les contours denses.
        const auto fit = [&](const geometry::Path& path) {
            double perim = 0.0;
            for (std::size_t i = 0; i < path.nodes.size(); ++i) {
                perim += length_um(path.nodes[(i + 1) % path.nodes.size()].pos - path.nodes[i].pos);
            }
            const auto tol = Micrometers{static_cast<std::int32_t>(
                std::max(1.0, std::min<double>(static_cast<double>(options.curve_tolerance.value),
                                               perim / 16.0)))};
            return geometry::fit_curves(path, {.tolerance = tol});
        };
        for (geometry::PathSet& set : *sets) {
            set.outer = fit(set.outer);
            for (geometry::Path& hole : set.holes) {
                hole = fit(hole);
            }
        }
    }
    return sets;
}

std::vector<Result<std::vector<geometry::PathSet>>>
vectorize_regions(const segmentation::Segmentation& seg, const std::vector<RegionId>& ids,
                  const VectorizeOptions& options) {
    // Result<T> n'est pas constructible par défaut : cases optionnelles remplies en parallèle.
    std::vector<std::optional<Result<std::vector<geometry::PathSet>>>> slots(ids.size());
    parallel_for(ids.size(),
                 [&](std::size_t i) { slots[i] = vectorize_region(seg, ids[i], options); });
    std::vector<Result<std::vector<geometry::PathSet>>> out;
    out.reserve(ids.size());
    for (auto& slot : slots) {
        out.push_back(std::move(*slot));
    }
    return out;
}

} // namespace openstitch::vectorization
