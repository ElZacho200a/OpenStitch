// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- optimisation de l'ordre de couture AVEC preuve : on calcule une proposition sur une
// copie du projet, on mesure la séquence réelle (`effective_sequence`) avant et après, puis
// l'utilisateur accepte ou refuse. L'acceptation est UNE commande annulable
// (`ReorderEmbroideryCommand`) ; une proposition calculée pour une ancienne révision est refusée.
// Sans Qt.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

enum class PathStrategy : std::uint8_t {
    ByColor,            // moins de changements de fil
    ByProximity,        // moins de déplacements
    ColorThenProximity, // couleurs groupées, puis proximité
    Layered             // fond d'abord, détails ensuite (couches), couleurs groupées
};

[[nodiscard]] std::string pathStrategyLabel(PathStrategy strategy);

// Mesures RÉELLES d'une séquence de couture (pas d'estimation par les centres).
struct PathMetrics {
    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    std::size_t colorChanges{0};
    double jumpLengthMm{0.0}; // distance totale parcourue à vide entre les objets
    double threadM{0.0};
};

struct PathProposal {
    PathStrategy strategy{PathStrategy::ColorThenProximity};
    bool improve{false};
    std::vector<std::uint64_t> order; // nouvel ordre des objets de broderie
    PathMetrics before;
    PathMetrics after;
    std::uint64_t revision{0}; // révision du document sur laquelle la proposition a été calculée

    // Vrai si l'ordre proposé diffère de l'ordre actuel.
    bool changes{false};
    // Vrai si la proposition améliore au moins un indicateur sans en dégrader un autre
    // sensiblement.
    [[nodiscard]] bool improves() const;
    // Phrases lisibles « Sauts : 42 → 30 (−12) ».
    [[nodiscard]] std::vector<std::string> summary() const;
};

[[nodiscard]] Result<PathProposal> proposeOrder(std::shared_ptr<const document::Project> project,
                                                std::uint64_t revision, PathStrategy strategy,
                                                bool improve, const TaskContext& task);

// Accepte la proposition : UNE étape d'annulation. Refus clair si le document a changé depuis le
// calcul (révision différente) ou si la proposition ne change rien.
ServiceResult applyProposal(DocumentSession& session, const PathProposal& proposal);

} // namespace openstitch::desktop_v2
