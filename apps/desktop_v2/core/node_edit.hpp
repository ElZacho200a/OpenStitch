// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- édition directe des nœuds du contour d'un objet : poignées, aperçu d'un glisser
// (pure géométrie, aucune régénération de points) et validation en UNE commande annulable
// (`TransformNodesCommand`, `InsertNodeCommand`, `ReshapeSegmentCommand` du moteur). Sans Qt.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "document_services.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

// Adresse d'un nœud : morceau (PathSet), chemin (0 = contour extérieur, i+1 = trou i), nœud.
struct NodeRef {
    std::size_t set{0};
    std::size_t path{0};
    std::size_t node{0};
    bool operator==(const NodeRef&) const = default;
};

struct NodeHandle {
    NodeRef ref;
    PointMm pos;        // mm « modèle » (Y vers le haut)
    bool smooth{false}; // nœud lisse (tangentes liées)
    bool curved{false}; // porte au moins une tangente de Bézier
};

struct NodeView {
    std::uint64_t vectorId{0};
    std::vector<NodeHandle> nodes;
    // Contours aplatis (courbes comprises), un par chemin, en mm modèle : tracé de l'aperçu.
    std::vector<std::vector<PointMm>> outlines;
};

// Nœuds du contour de l'objet de broderie `objectId`, ou erreur (objet absent, sans forme source).
[[nodiscard]] Result<NodeView> nodeView(const document::Project& project, std::uint64_t objectId);

// Contours aplatis APRÈS déplacement de `nodes` de `deltaMm`, sans toucher au document. `nullopt`
// si le déplacement est refusé (le contour se croiserait, nœud inconnu).
[[nodiscard]] std::optional<std::vector<std::vector<PointMm>>>
previewNodeMove(const document::Project& project, std::uint64_t objectId,
                const std::vector<NodeRef>& nodes, PointMm deltaMm, bool soft);

// Déplace les nœuds en UNE commande (tous dans le même chemin). Refus clair sinon.
ServiceResult moveNodes(DocumentSession& session, std::uint64_t objectId,
                        const std::vector<NodeRef>& nodes, PointMm deltaMm, bool soft);

// Segment le plus proche de `at` à moins de `maxDistMm` : nœud de départ et abscisse t (0..1).
struct SegmentHit {
    NodeRef start;
    double t{0.0};
};
[[nodiscard]] std::optional<SegmentHit> nearestSegment(const NodeView& view, PointMm at,
                                                       double maxDistMm);

// Insère un nœud sur le segment (subdivision exacte : la forme ne change pas).
ServiceResult insertNode(DocumentSession& session, std::uint64_t objectId, const SegmentHit& hit);

// Tire le segment au point `hit.t` de `deltaMm` (remodelage d'un segment en UNE commande).
ServiceResult reshapeSegment(DocumentSession& session, std::uint64_t objectId,
                             const SegmentHit& hit, PointMm deltaMm);

} // namespace openstitch::desktop_v2
