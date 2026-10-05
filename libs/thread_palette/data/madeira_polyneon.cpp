// SPDX-License-Identifier: Apache-2.0
//
// ATTENTION — DONNEES PLACEHOLDER (voir source_note ci-dessous et le plan
// d'implementation specs/plans/thread-palette-implementation.md, section 4 et
// "Questions ouvertes" / section 8 point 4). Cet environnement d'execution
// n'a pas acces au web : il est impossible de transcrire ici le vrai nuancier
// Madeira Polyneon depuis la page officielle du fabricant (S1-POLICY-1). Les
// entrees ci-dessous ont la FORME d'un nuancier reel (codes a 4 chiffres,
// noms de couleur plausibles, RGB coherents avec le nom) mais sont
// INVENTEES. Elles ne doivent PAS etre utilisees comme reference colorimetrique
// produit. A remplacer par le nuancier officiel transcrit depuis le site
// Madeira avant toute utilisation hors developpement/test.
#include "../src/chart_data.hpp"

namespace openstitch::thread_palette::detail {

ThreadChart make_madeira_polyneon_chart() {
    ThreadChart chart;
    chart.chart_id = "madeira_polyneon";
    chart.display_name = "Madeira Polyneon 40";
    chart.source_note = "DONNEES PLACEHOLDER -- a remplacer par le nuancier officiel transcrit "
                        "depuis https://www.madeira.co.uk (fiche couleurs Polyneon 40), voir "
                        "plan specs/plans/thread-palette-implementation.md section 4. Aucune "
                        "transcription reelle n'a ete faite (pas d'acces web depuis cet "
                        "environnement) ; codes, noms et RGB ci-dessous sont inventes pour "
                        "donner au registre une forme realiste pendant le developpement.";

    chart.threads = {
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1801"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "White",
               .rgb = {255, 255, 255}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1800"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Black",
               .rgb = {20, 20, 20}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1919"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Poppy Red",
               .rgb = {196, 30, 40}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1724"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Marine Blue",
               .rgb = {20, 50, 110}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1621"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Meadow Green",
               .rgb = {40, 130, 60}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1700"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Lemon Yellow",
               .rgb = {240, 220, 40}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1912"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Tangerine Orange",
               .rgb = {235, 120, 30}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1744"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Lavender Purple",
               .rgb = {120, 80, 160}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1660"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Dusty Rose",
               .rgb = {200, 140, 150}},
        Thread{.key = ThreadKey{.chart_id = "madeira_polyneon", .code = "1666"},
               .brand = "Madeira",
               .range = "Polyneon 40",
               .name = "Taupe Grey",
               .rgb = {150, 140, 130}},
    };

    return chart;
}

} // namespace openstitch::thread_palette::detail
