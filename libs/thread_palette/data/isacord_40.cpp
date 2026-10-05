// SPDX-License-Identifier: Apache-2.0
//
// ATTENTION — DONNEES PLACEHOLDER (voir source_note ci-dessous et le plan
// d'implementation specs/plans/thread-palette-implementation.md, section 4 et
// "Questions ouvertes" / section 8 point 4). Cet environnement d'execution
// n'a pas acces au web : il est impossible de transcrire ici le vrai nuancier
// Isacord 40 depuis la page officielle du fabricant (S1-POLICY-1). Les
// entrees ci-dessous ont la FORME d'un nuancier reel (codes a 4 chiffres,
// noms de couleur plausibles, RGB coherents avec le nom) mais sont
// INVENTEES. Elles ne doivent PAS etre utilisees comme reference colorimetrique
// produit. A remplacer par le nuancier officiel transcrit depuis le site
// Isacord/Zweigart avant toute utilisation hors developpement/test.
#include "../src/chart_data.hpp"

namespace openstitch::thread_palette::detail {

ThreadChart make_isacord_40_chart() {
    ThreadChart chart;
    chart.chart_id = "isacord_40";
    chart.display_name = "Isacord 40";
    chart.source_note = "DONNEES PLACEHOLDER -- a remplacer par le nuancier officiel transcrit "
                        "depuis https://www.isacord.com (fiche couleurs Isacord 40), voir "
                        "plan specs/plans/thread-palette-implementation.md section 4. Aucune "
                        "transcription reelle n'a ete faite (pas d'acces web depuis cet "
                        "environnement) ; codes, noms et RGB ci-dessous sont inventes pour "
                        "donner au registre une forme realiste pendant le developpement.";

    chart.threads = {
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "0100"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "White",
               .rgb = {255, 255, 255}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "0200"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Black",
               .rgb = {25, 25, 25}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "1900"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Red",
               .rgb = {200, 25, 35}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "3620"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Royal Blue",
               .rgb = {30, 60, 150}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "5200"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Grass Green",
               .rgb = {45, 140, 55}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "0301"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Sun Yellow",
               .rgb = {245, 215, 35}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "1200"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Pumpkin Orange",
               .rgb = {230, 115, 25}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "2600"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Violet",
               .rgb = {110, 70, 150}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "2110"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Dusty Pink",
               .rgb = {205, 145, 155}},
        Thread{.key = ThreadKey{.chart_id = "isacord_40", .code = "0612"},
               .brand = "Isacord",
               .range = "Isacord 40",
               .name = "Stone Grey",
               .rgb = {145, 145, 140}},
    };

    return chart;
}

} // namespace openstitch::thread_palette::detail
