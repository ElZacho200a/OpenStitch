// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- édition directe des nœuds du contour d'un objet : poignées, aperçu d'un glisser
// (pure géométrie, aucune régénération de points) et validation en UNE commande annulable
// (`TransformNodesCommand`, `InsertNodeCommand`, `ReshapeSegmentCommand` du moteur). Sans Qt.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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
    bool open{false};   // le chemin ne se referme pas (rail de satin) : pas de segment de retour
};

struct NodeView {
    std::uint64_t vectorId{0};
    std::vector<NodeHandle> nodes;
    // Contours aplatis (courbes comprises), un par chemin, en mm modèle : tracé de l'aperçu.
    std::vector<std::vector<PointMm>> outlines;
    // Colonne satin : `ref.set` vaut 0 pour le rail A et 1 pour le rail B (`ref.path` = 0).
    bool satin{false};
    std::vector<std::pair<PointMm, PointMm>> rungs; // barreaux (rail A, rail B), mm modèle
    double widthMinMm{0.0};                         // largeur de la colonne aux barreaux
    double widthMaxMm{0.0};
    double recommendedMaxMm{0.0}; // au-delà : satin trop large
};

// Phrase de diagnostic de la largeur d'une colonne satin (vide si ce n'est pas un satin).
[[nodiscard]] std::string satinWidthSummary(const NodeView& view);

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

// Segment de courbe (droit OU cubique) le plus proche de `at`, à moins de `maxDistMm` : l'abscisse
// `t` est celle de la vraie courbe (pas de la corde), donc adaptée au remodelage direct.
// Rien pour une colonne satin (ses rails se manipulent nœud par nœud).
[[nodiscard]] std::optional<SegmentHit> curveSegmentAt(const document::Project& project,
                                                       std::uint64_t objectId, PointMm at,
                                                       double maxDistMm);

// Contours aplatis APRÈS remodelage du segment (tiré de `deltaMm`), sans toucher au document.
// `nullopt` si le résultat se croiserait ou si le segment n'existe pas.
[[nodiscard]] std::optional<std::vector<std::vector<PointMm>>>
previewSegmentReshape(const document::Project& project, std::uint64_t objectId,
                      const SegmentHit& hit, PointMm deltaMm);

// Convertit les contours des objets en courbes de Bézier / les lisse : UNE commande annulable pour
// tous les objets. Refus clair si rien ne change.
ServiceResult convertToCurves(DocumentSession& session,
                              const std::vector<std::uint64_t>& objectIds);
ServiceResult smoothContours(DocumentSession& session, const std::vector<std::uint64_t>& objectIds);

// Supprime des nœuds du contour (même chemin ou non) en UNE commande annulable. Refus clairs : un
// contour fermé garde au moins 3 nœuds, la suppression ne doit pas faire croiser le contour, les
// rails de satin se modifient autrement.
ServiceResult deleteNodes(DocumentSession& session, std::uint64_t objectId,
                          const std::vector<NodeRef>& nodes);

// Tire le segment au point `hit.t` de `deltaMm` (remodelage d'un segment en UNE commande).
ServiceResult reshapeSegment(DocumentSession& session, std::uint64_t objectId,
                             const SegmentHit& hit, PointMm deltaMm);

} // namespace openstitch::desktop_v2
