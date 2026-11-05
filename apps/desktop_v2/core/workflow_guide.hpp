// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- guide de parcours : « où en suis-je, que faire ensuite ? ». Quatre étapes, de
// l'image au fichier machine : Importer, Retoucher, Contrôler, Exporter. Pure fonction de l'état
// du document ; l'interface l'affiche (bandeau d'étapes + bouton « étape suivante »). Sans Qt.

#include <string>
#include <vector>

namespace openstitch::desktop_v2 {

enum class StepState { Todo, Current, Done };
enum class NextAction { OpenImage, Analyse, Export, None };

struct GuideInput {
    bool documentOpen{false};
    bool hasObjects{false};
    bool analysedCurrent{false}; // une analyse existe pour la révision courante du document
    int analysisErrors{0};
    bool exportedCurrent{false}; // un DST a été écrit pour la révision courante
};

struct GuideStep {
    std::string id; // import | refine | check | export
    std::string title;
    StepState state{StepState::Todo};
};

struct Guide {
    std::vector<GuideStep> steps; // toujours 4, dans l'ordre
    NextAction next{NextAction::None};
    std::string nextLabel; // libellé du bouton principal ; vide si rien à faire
    std::string headline;  // une phrase : ce qu'il faut faire maintenant
    int currentIndex{0};   // étape courante (0..3), 3 si tout est fini
};

[[nodiscard]] Guide buildGuide(const GuideInput& input);

} // namespace openstitch::desktop_v2
