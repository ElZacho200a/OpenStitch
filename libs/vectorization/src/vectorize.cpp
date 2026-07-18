// SPDX-License-Identifier: Apache-2.0
#include "openstitch/vectorization/vectorize.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>

#include "openstitch/geometry/clean.hpp"
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

}  // namespace

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

    // Masque binaire de la région.
    cv::Mat mask(seg.height, seg.width, CV_8U, cv::Scalar(0));
    const auto label = static_cast<std::uint32_t>(id.value);
    for (int y = 0; y < seg.height; ++y) {
        for (int x = 0; x < seg.width; ++x) {
            if (seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                           static_cast<std::size_t>(x)] == label) {
                mask.at<std::uint8_t>(y, x) = 255;
            }
        }
    }

    // Contours extérieurs ET trous (RETR_CCOMP) ; CHAIN_APPROX_SIMPLE retire
    // déjà les points colinéaires. La hiérarchie exacte est reconstruite plus
    // loin par le nettoyage (règle pair-impair), inutile de l'exploiter ici.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        return fail(ErrorCategory::Internal, "La région n'a produit aucun contour");
    }

    std::vector<geometry::Path> raw;
    raw.reserve(contours.size());
    for (const auto& contour : contours) {
        if (contour.size() < 3) {
            continue;  // micro-artefact d'un pixel isolé
        }
        geometry::Path path;
        path.closed = true;
        path.nodes.reserve(contour.size());
        for (const cv::Point& pt : contour) {
            path.nodes.push_back(to_model(pt, seg.width, seg.height, options.mm_per_px.value));
        }
        raw.push_back(geometry::simplify(path, options.simplify_tolerance));
    }

    auto sets = geometry::clean_to_path_sets(raw);
    if (!sets) {
        return std::unexpected(sets.error());
    }
    if (sets->empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "La région est trop petite pour produire une forme vectorielle exploitable");
    }
    return sets;
}

}  // namespace openstitch::vectorization
