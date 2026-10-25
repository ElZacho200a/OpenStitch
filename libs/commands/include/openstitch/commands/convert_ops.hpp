// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Conversions entre types de couture qui changent AUSSI la géométrie source (HP-OBJ-022).
// Chacune est UNE commande annulable ; fil (couleur, nuancier), nom, ordre de couture, jonction
// et connecteur de l'objet sont conservés, ainsi que les points d'entrée/sortie quand le type
// d'arrivée en porte (tatami et satin). Les retouches manuelles de points sont abandonnées (elles
// désignent des points du type d'origine).
enum class ConversionKind {
    // Colonne satin à rails -> point droit sur la LIGNE CENTRALE des rails (milieu de chaque
    // paire de points homologues). Les sections soeurs d'un même plan satin donnent chacune un
    // tracé ouvert (ou fermé) du même objet.
    SatinToCenterline,
    // Colonne satin à rails -> FORME remplie en tatami : contour extérieur de la colonne
    // (rail A puis rail B inversé ; anneau extérieur + trou pour un satin de bordure fermé).
    SatinToFilledShape,
    // Contour cousu (droit, blanket, motif) -> satin de BORDURE sur le même tracé (largeur
    // `border_width`, centré, coins vifs) ; un anneau par objet (les trous de la forme en
    // ajoutent un chacun, juste après).
    ContourToBorderSatin,
    // Remplissage (tatami, directionnel, concentrique...) -> contour cousu sur le bord de la
    // forme.
    FillToContour,
};

struct ConversionPlan {
    std::unique_ptr<ICommand> command; // nulle si la conversion est impossible
    std::string error;                 // raison lisible quand `command` est nulle
};

// Vrai si `kind` s'applique à ce type d'objet (aucune géométrie calculée).
[[nodiscard]] bool conversion_applicable(const document::EmbroideryObject& object,
                                         ConversionKind kind);

// Construit la commande de conversion de l'objet de broderie `embroidery_id`. Prend le projet
// non constant : les identifiants des objets créés sont réservés dans `project.object_ids`.
[[nodiscard]] ConversionPlan make_conversion_command(document::Project& project,
                                                     ObjectId embroidery_id, ConversionKind kind,
                                                     Micrometers border_width = Micrometers{3'000});

} // namespace openstitch::commands
