// SPDX-License-Identifier: Apache-2.0
#include "workflow_guide.hpp"

namespace openstitch::desktop_v2 {

Guide buildGuide(const GuideInput& in) {
    Guide g;
    g.steps = {{"import", "Importer", StepState::Todo},
               {"refine", "Retoucher", StepState::Todo},
               {"check", "Contrôler", StepState::Todo},
               {"export", "Exporter", StepState::Todo}};
    const auto setDone = [&](int upTo) {
        for (int i = 0; i < upTo; ++i) {
            g.steps[static_cast<std::size_t>(i)].state = StepState::Done;
        }
    };
    const auto setCurrent = [&](int i) {
        g.currentIndex = i;
        g.steps[static_cast<std::size_t>(i)].state = StepState::Current;
    };

    if (!in.hasObjects) {
        setCurrent(0);
        g.next = NextAction::OpenImage;
        g.nextLabel = "Ouvrir une image…";
        g.headline = in.documentOpen
                         ? "Le projet est vide : dessinez des formes ou ouvrez une image."
                         : "Commencez par ouvrir une image à broder.";
        return g;
    }
    if (!in.analysedCurrent) {
        setDone(1);
        setCurrent(1);
        g.next = NextAction::Analyse;
        g.nextLabel = "Contrôler le motif";
        g.headline = "Retouchez les objets (couleurs, types de points), puis contrôlez le motif.";
        return g;
    }
    if (in.analysisErrors > 0) {
        setDone(1);
        setCurrent(1);
        g.next = NextAction::Analyse;
        g.nextLabel = "Relancer l'analyse";
        g.headline = std::to_string(in.analysisErrors) +
                     " erreur(s) à corriger avant l'export ; relancez l'analyse ensuite.";
        return g;
    }
    if (!in.exportedCurrent) {
        setDone(3);
        setCurrent(3);
        g.next = NextAction::Export;
        g.nextLabel = "Exporter en DST…";
        g.headline = "Le motif est prêt : exportez le fichier pour la machine.";
        return g;
    }
    setDone(4);
    g.currentIndex = 3;
    g.next = NextAction::None;
    g.headline = "Fichier exporté. Une modification rouvrira l'étape de contrôle.";
    return g;
}

} // namespace openstitch::desktop_v2
