// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- guide de parcours : « où en suis-je, que faire ensuite ? ». Quatre PHASES :
// Préparer (image, palette, segmentation), Numériser (objets et points), Contrôler (diagnostic,
// optimisation), Produire (machine, cadre, export). Ce ne sont PAS des écrans verrouillés : le
// guide lit l'état réel du document et propose toujours l'action la plus utile, y compris un retour
// direct vers ce qui est devenu obsolète ou bloquant. Pure fonction de l'état ; l'interface
// l'affiche. Sans Qt.
//
// Un contrôle sans erreur n'est PAS une garantie de préparation machine : la phase Produire exige
// son propre contrôle (cadre, champ de la machine, format) pour la révision courante.

#include <string>
#include <vector>

namespace openstitch::desktop_v2 {

// État d'une phase :
//  - Todo : non commencée ;
//  - Current : en cours (la prochaine à traiter) ;
//  - Done : terminée ;
//  - Stale : un résultat existe mais le document a changé depuis ;
//  - Blocked : une erreur bloque la suite ;
//  - Warning : terminée, avec des avertissements non bloquants ;
//  - Skipped : phase facultative ignorée explicitement par l'utilisateur.
enum class StepState { Todo, Current, Done, Stale, Blocked, Warning, Skipped };

// Action proposée par le bouton principal du guide.
enum class NextAction {
    OpenImage,       // ouvrir une image ou créer un projet
    Regenerate,      // les objets ne suivent plus la segmentation corrigée
    Analyse,         // lancer / relancer le diagnostic
    ShowDiagnostics, // aller aux erreurs du diagnostic
    CheckMachine,    // contrôle avant export (cadre, champ machine, format)
    Export,          // écrire le fichier machine
    None
};

struct GuideInput {
    bool documentOpen{false};
    bool hasObjects{false};
    bool analysedCurrent{false}; // une analyse existe pour la révision courante du document
    int analysisErrors{0};
    bool exportedCurrent{false}; // un fichier machine a été écrit pour la révision courante

    // --- état réel du projet (valeurs par défaut = ancien comportement) -------------------------
    bool hasSegmentation{false};
    bool segmentationDirty{false}; // corrections de régions pas encore répercutées sur les objets
    bool analysedEver{false};      // une analyse a existé (même périmée)
    int analysisWarnings{0};
    bool checkSkipped{false};          // le contrôle (facultatif) est explicitement ignoré
    bool machineCheckedCurrent{false}; // contrôle avant export fait pour la révision courante
    int machineBlockers{0};            // hors cadre, hors champ machine, format refusé
    int machineWarnings{0};
    bool exportedEver{false};
    bool handEdits{false}; // des retouches manuelles existent (une régénération est à protéger)
};

struct GuideStep {
    std::string id; // prepare | digitize | check | produce
    std::string title;
    StepState state{StepState::Todo};
    std::string detail;               // une phrase : pourquoi cet état
    NextAction fix{NextAction::None}; // l'action qui répare / fait avancer CETTE phase
    std::string fixLabel;
};

// Un problème à signaler, avec son retour direct.
struct GuideIssue {
    bool blocking{false};
    std::string text;
    NextAction fix{NextAction::None};
    std::string fixLabel;
};

struct Guide {
    std::vector<GuideStep> steps; // toujours 4, dans l'ordre
    NextAction next{NextAction::None};
    std::string nextLabel; // libellé du bouton principal ; vide si rien à faire
    std::string headline;  // une phrase : ce qu'il faut faire maintenant
    int currentIndex{0};   // phase à traiter (0..3), 3 si tout est fini
    std::vector<GuideIssue>
        issues; // obsolescences, erreurs, avertissements, du plus grave au moins
};

[[nodiscard]] Guide buildGuide(const GuideInput& input);

// Libellé français d'un état (« terminée », « obsolète »…), pour l'infobulle et l'accessibilité.
[[nodiscard]] std::string stepStateLabel(StepState state);

} // namespace openstitch::desktop_v2
