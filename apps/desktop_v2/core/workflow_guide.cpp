// SPDX-License-Identifier: Apache-2.0
#include "workflow_guide.hpp"

#include <algorithm>

namespace openstitch::desktop_v2 {

std::string stepStateLabel(StepState state) {
    switch (state) {
    case StepState::Todo:
        return "non commencée";
    case StepState::Current:
        return "en cours";
    case StepState::Done:
        return "terminée";
    case StepState::Stale:
        return "obsolète";
    case StepState::Blocked:
        return "bloquée par une erreur";
    case StepState::Warning:
        return "terminée avec des avertissements";
    case StepState::Skipped:
        return "ignorée";
    }
    return {};
}

namespace {

bool passed(StepState s) {
    return s == StepState::Done || s == StepState::Warning || s == StepState::Skipped;
}

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

} // namespace

Guide buildGuide(const GuideInput& in) {
    Guide g;
    g.steps = {{"prepare", "Préparer", StepState::Todo, {}, NextAction::None, {}},
               {"digitize", "Numériser", StepState::Todo, {}, NextAction::None, {}},
               {"check", "Contrôler", StepState::Todo, {}, NextAction::None, {}},
               {"produce", "Produire", StepState::Todo, {}, NextAction::None, {}}};
    auto& prepare = g.steps[0];
    auto& digitize = g.steps[1];
    auto& check = g.steps[2];
    auto& produce = g.steps[3];

    // --- Phase 1 : Préparer -------------------------------------------------------------------
    if (!in.hasObjects && !in.hasSegmentation) {
        prepare.state = StepState::Current;
        prepare.detail = in.documentOpen
                             ? "Le projet est vide : dessinez des formes ou ouvrez une image."
                             : "Commencez par ouvrir une image à broder.";
        prepare.fix = NextAction::OpenImage;
        prepare.fixLabel = "Ouvrir une image…";
        g.next = NextAction::OpenImage;
        g.nextLabel = prepare.fixLabel;
        g.headline = prepare.detail;
        g.currentIndex = 0;
        return g;
    }
    prepare.state = StepState::Done;
    prepare.detail = in.hasSegmentation ? "Image segmentée." : "Formes dessinées.";

    // --- Phase 2 : Numériser ------------------------------------------------------------------
    if (!in.hasObjects) {
        digitize.state = StepState::Current;
        digitize.detail = "La segmentation n'a produit aucun objet de broderie.";
        digitize.fix = NextAction::Regenerate;
        digitize.fixLabel = "Générer les objets";
    } else if (in.segmentationDirty) {
        digitize.state = StepState::Stale;
        digitize.detail =
            in.handEdits ? "Les objets ne suivent plus la segmentation corrigée ; vos retouches "
                           "manuelles seront remplacées (annulable)."
                         : "Les objets ne suivent plus la segmentation corrigée.";
        digitize.fix = NextAction::Regenerate;
        digitize.fixLabel = "Régénérer les objets";
    } else {
        digitize.state = StepState::Done;
        digitize.detail = "Objets à jour.";
    }

    // --- Phase 3 : Contrôler ------------------------------------------------------------------
    if (!in.hasObjects) {
        check.state = StepState::Todo;
    } else if (in.checkSkipped) {
        check.state = StepState::Skipped;
        check.detail = "Contrôle ignoré à votre demande.";
    } else if (!in.analysedEver) {
        check.state = StepState::Todo;
        check.detail = "Le diagnostic n'a pas encore été lancé.";
        check.fix = NextAction::Analyse;
        check.fixLabel = "Contrôler le motif";
    } else if (!in.analysedCurrent) {
        check.state = StepState::Stale;
        check.detail = "Le document a changé depuis le dernier diagnostic.";
        check.fix = NextAction::Analyse;
        check.fixLabel = "Relancer le contrôle";
    } else if (in.analysisErrors > 0) {
        check.state = StepState::Blocked;
        check.detail = plural(in.analysisErrors, "erreur", "erreurs") + " à corriger.";
        check.fix = NextAction::ShowDiagnostics;
        check.fixLabel = "Voir les erreurs";
    } else if (in.analysisWarnings > 0) {
        check.state = StepState::Warning;
        check.detail = plural(in.analysisWarnings, "avertissement", "avertissements") +
                       " non bloquant(s) ; aucune erreur.";
    } else {
        check.state = StepState::Done;
        check.detail = "Aucune erreur détectée par le diagnostic (cela ne remplace pas le contrôle "
                       "machine).";
    }

    // --- Phase 4 : Produire -------------------------------------------------------------------
    if (!in.hasObjects) {
        produce.state = StepState::Todo;
    } else if (in.exportedCurrent && in.machineBlockers == 0) {
        produce.state = StepState::Done;
        produce.detail = "Fichier machine exporté pour la version actuelle du motif.";
    } else if (in.machineCheckedCurrent && in.machineBlockers > 0) {
        produce.state = StepState::Blocked;
        produce.detail = plural(in.machineBlockers, "problème", "problèmes") +
                         " machine à corriger (cadre, champ de la machine ou format).";
        produce.fix = NextAction::CheckMachine;
        produce.fixLabel = "Revoir le contrôle machine";
    } else if (in.exportedEver && !in.exportedCurrent) {
        produce.state = StepState::Stale;
        produce.detail = "Le fichier exporté ne correspond plus au motif : exportez de nouveau.";
        produce.fix = in.machineCheckedCurrent ? NextAction::Export : NextAction::CheckMachine;
        produce.fixLabel = in.machineCheckedCurrent ? "Exporter de nouveau…" : "Contrôle machine";
    } else if (!in.machineCheckedCurrent) {
        produce.state = StepState::Todo;
        produce.detail = "Vérifiez le cadre, la machine et le format avant d'exporter.";
        produce.fix = NextAction::CheckMachine;
        produce.fixLabel = "Contrôle machine";
    } else {
        produce.state = in.machineWarnings > 0 ? StepState::Warning : StepState::Current;
        produce.detail = in.machineWarnings > 0
                             ? plural(in.machineWarnings, "avertissement", "avertissements") +
                                   " machine non bloquant(s)."
                             : "Contrôle machine passé : prêt à exporter.";
        produce.fix = NextAction::Export;
        produce.fixLabel = "Exporter…";
    }

    // --- Problèmes, du plus grave au moins grave ----------------------------------------------
    if (in.hasObjects && in.segmentationDirty) {
        g.issues.push_back({true, digitize.detail, NextAction::Regenerate, digitize.fixLabel});
    }
    if (check.state == StepState::Blocked) {
        g.issues.push_back({true, check.detail, check.fix, check.fixLabel});
    }
    if (produce.state == StepState::Blocked) {
        g.issues.push_back({true, produce.detail, produce.fix, produce.fixLabel});
    }
    if (check.state == StepState::Stale) {
        g.issues.push_back({false, check.detail, check.fix, check.fixLabel});
    }
    if (produce.state == StepState::Stale) {
        g.issues.push_back({false, produce.detail, produce.fix, produce.fixLabel});
    }
    if (check.state == StepState::Warning) {
        g.issues.push_back(
            {false, check.detail, NextAction::ShowDiagnostics, "Voir les avertissements"});
    }
    if (produce.state == StepState::Warning) {
        g.issues.push_back({false, produce.detail, NextAction::CheckMachine, "Revoir le contrôle"});
    }

    // --- Action principale : toujours la plus pertinente ---------------------------------------
    // Ordre : ce qui est obsolète ou bloquant d'abord, puis ce qui fait avancer.
    const GuideStep* pick = nullptr;
    for (const GuideStep* s : {&digitize, &check, &produce}) {
        if ((s->state == StepState::Stale || s->state == StepState::Blocked) &&
            s->fix != NextAction::None) {
            pick = s;
            break;
        }
    }
    if (pick == nullptr) {
        for (const GuideStep* s : {&digitize, &check, &produce}) {
            if ((s->state == StepState::Todo || s->state == StepState::Current) &&
                s->fix != NextAction::None) {
                pick = s;
                break;
            }
        }
    }
    if (pick == nullptr && produce.state == StepState::Warning) {
        pick = &produce;
    }
    // Phase à traiter (indice) : la première qui n'est pas passée.
    g.currentIndex = 3;
    for (int i = 0; i < 4; ++i) {
        if (!passed(g.steps[static_cast<std::size_t>(i)].state)) {
            g.currentIndex = i;
            break;
        }
    }
    auto& cur = g.steps[static_cast<std::size_t>(g.currentIndex)];
    if (cur.state == StepState::Todo) {
        cur.state = StepState::Current;
    }
    if (pick != nullptr) {
        g.next = pick->fix;
        g.nextLabel = pick->fixLabel;
        g.headline = pick->detail;
    } else {
        g.next = NextAction::None;
        g.headline = "Fichier exporté. Une modification rouvrira le contrôle et l'export.";
    }
    return g;
}

} // namespace openstitch::desktop_v2
