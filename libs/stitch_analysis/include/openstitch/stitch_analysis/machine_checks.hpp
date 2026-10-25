// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"

namespace openstitch::stitch_analysis {

// HP-HOOP-003 : vérifications selon le profil machine du projet (`Project::machine_id`).
// Pur : ne modifie rien. Sans machine (identifiant vide) : aucun constat. Catégories :
//  - machine-inconnue  : identifiant non reconnu (Warning) ;
//  - machine-format    : `export_format` (identifiant de format, vide = non vérifié) non
//                        accepté par la machine (Warning) ;
//  - machine-champ     : design plus grand que le champ de broderie maximal (Error) ;
//  - machine-cadre     : aucun cadre de la machine ne contient le design, forme comprise
//                        (Warning) ;
//  - machine-aiguilles : plus de couleurs distinctes que d'aiguilles (Warning) ;
//  - machine-point-long: points plus longs que la limite de la machine (Warning) ;
//  - machine-coupe     : coupes demandées mais machine sans coupe-fil automatique (Info).
[[nodiscard]] std::vector<Finding> check_machine(const document::Project& project,
                                                 const stitch::StitchSequence& sequence,
                                                 std::string_view export_format = {});

} // namespace openstitch::stitch_analysis
