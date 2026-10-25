// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/stitch_analysis/production_sheet.hpp"

namespace openstitch::stitch_analysis {

// HP-PROD-001 : fiche de production en PDF (A4 portrait, PDF 1.4, polices standard
// Helvetica), écrite SANS Qt pour que le CLI produise un PDF imprimable. Contenu :
// titre, client / date, chiffres clés, aperçu vectoriel du motif, tableau des blocs
// de couleur (pastille, référence de fil, points, temps), consommation de fil,
// avertissements et notes ; plusieurs pages si besoin. Sortie déterministe (aucune
// date ni identifiant aléatoire dans le fichier). Texte en WinAnsi : les caractères
// hors Latin-1 / cp1252 sont remplacés par « ? ».
[[nodiscard]] std::string production_to_pdf(const ProductionSheet& sheet);

} // namespace openstitch::stitch_analysis
