// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 / Studio -- état des QUATRE ÉTAPES du pipeline, lu dans le document :
// image -> segmentation -> vectorisation -> points. Pure fonction du `Project` (et d'un drapeau
// d'interface), sans Qt : l'adaptateur `WorkspaceController` n'ajoute que la plomberie.
//
// Règles (l'amont l'emporte : une étape ne peut pas être « à jour » si son amont est obsolète) :
//  - image        : vide sans image ; à jour sinon ;
//  - segmentation : bloquée sans image ; vide sans segmentation ; obsolète si des corrections de
//                   régions ne sont pas répercutées sur les objets (`segmentationDirty`, la même
//                   notion que `GuideInput::segmentationDirty` du guide de parcours) ; à jour sinon
//                   ;
//  - vectorisation: vide sans objet vectoriel ; obsolète si la segmentation est obsolète ou si un
//                   objet issu d'une région (`source_region`) ne retrouve plus sa région ; à jour
//                   sinon (les formes dessinées à la main n'ont pas de région source) ;
//  - points       : vide sans objet de broderie ; obsolète si la vectorisation est obsolète ou si
//  un
//                   objet de broderie ne retrouve plus son objet vectoriel ; à jour sinon.
// Les points ne sont jamais stockés (`effective_sequence`) : « à jour » signifie « leurs objets
// suivent leurs formes ».

#include <array>
#include <string>
#include <string_view>

#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

enum class StageId { Image, Segmentation, Vector, Points };
enum class StageState { Empty, UpToDate, Stale, Blocked };

struct StageStatus {
    StageId id{StageId::Image};
    StageState state{StageState::Empty};
    std::string detail; // une phrase française : pourquoi cet état
};

struct StageInputs {
    // Corrections de régions pas encore répercutées sur les objets (état d'interface, comme
    // `GuideInput::segmentationDirty`).
    bool segmentationDirty{false};
    std::string dirtyReason; // « des régions ont été retouchées »… (facultatif)
};

[[nodiscard]] std::array<StageStatus, 4> computeStageStatus(const document::Project& project,
                                                            const StageInputs& inputs = {});

// Identifiants stables : "image" | "segmentation" | "vector" | "points".
[[nodiscard]] std::string_view stageIdName(StageId id);
// "empty" | "upToDate" | "stale" | "blocked".
[[nodiscard]] std::string_view stageStateName(StageState state);

} // namespace openstitch::desktop_v2
