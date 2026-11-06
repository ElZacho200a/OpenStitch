// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "file_io.hpp"
#include "thread_palette_service.hpp"
#include "thread_plan.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
fs::path writeFile(const char* name, const std::string& text) {
    const auto p = fs::temp_directory_path() / name;
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
    return p;
}
} // namespace

TEST_CASE("a user thread chart is imported, searchable, assignable, persisted and removable",
          "[desktop_v2][threads]") {
    const fs::path dir = fs::temp_directory_path() / "os_v2_charts";
    fs::remove_all(dir);
    const auto csv = writeFile("os_v2_mes_fils_test.csv",
                               "code,name,hex\n1001,Rouge vif,#C8102E\n1002,Bleu nuit,#0B1F4D\n"
                               "1003,Vert mousse,#5B8C3A\n");
    const auto imported = importThreadChart(csv, dir);
    REQUIRE(imported.has_value());
    CHECK(imported->user);
    CHECK(imported->count == 3);
    CHECK(imported->id.rfind("user_", 0) == 0);       // jamais le nom d'un nuancier intégré
    CHECK(fs::exists(dir / (imported->id + ".csv"))); // copie pour le prochain démarrage

    // Il apparaît dans la liste, avec ses fils cherchables par nom ou référence.
    const auto infos = chartInfos();
    CHECK(std::any_of(infos.begin(), infos.end(),
                      [&](const ChartInfo& c) { return c.id == imported->id && c.user; }));
    const auto found = searchThreads(imported->id, "bleu");
    REQUIRE(found.size() == 1);
    CHECK(found.front().code == "1002");
    CHECK(searchThreads(imported->id, "1003").size() == 1);
    // Le fil le plus proche d'un rouge est le rouge du nuancier importé.
    const auto nearest = nearestThreads({200, 20, 50}, imported->id, 1);
    REQUIRE(nearest.size() == 1);
    CHECK(nearest.front().code == "1001");

    // Assigner ce fil à un fil du plan, en une étape annulable.
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 40.0;
    request.settings.max_colors = 3;
    auto out = run_digitize(request, TaskContext{});
    REQUIRE(out.has_value());
    DocumentSession session;
    session.replaceProject(std::move(out->project), true);
    const auto depth = session.undoNames().size();
    REQUIRE(assignChartThread(session, 0, imported->id, "1001").ok);
    CHECK(session.undoNames().size() == depth + 1);
    CHECK(threadEntries(session.project()).front().rgb == std::array<std::uint8_t, 3>{200, 16, 46});
    CHECK_FALSE(assignChartThread(session, 0, imported->id, "9999").ok); // fil inconnu

    // Le même fichier ne s'importe pas deux fois (jamais de remplacement silencieux).
    const auto again = importThreadChart(csv, dir);
    CHECK_FALSE(again.has_value());
    // Un fichier invalide est refusé avec sa cause.
    const auto bad = writeFile("os_v2_mauvais_test.csv", "code,name,hex\n,Sans code,#FFFFFF\n");
    CHECK_FALSE(importThreadChart(bad, dir).has_value());

    // Retrait : le nuancier disparaît de la liste et sa copie aussi ; un intégré ne se retire pas.
    CHECK(removeUserChart(imported->id, dir));
    CHECK_FALSE(fs::exists(dir / (imported->id + ".csv")));
    CHECK_FALSE(removeUserChart(imported->id, dir));
    CHECK_FALSE(removeUserChart(chartInfos().front().id, dir));

    // Redémarrage simulé : relire le dossier retrouve les nuanciers copiés.
    fs::create_directories(dir);
    fs::copy_file(csv, dir / "user_mes_fils_test.csv", fs::copy_options::overwrite_existing);
    CHECK(loadUserCharts(dir).empty());
    const auto reloaded = chartInfos();
    CHECK(std::any_of(reloaded.begin(), reloaded.end(), [](const ChartInfo& c) { return c.user; }));
    for (const auto& c : reloaded) {
        if (c.user) {
            (void)removeUserChart(c.id, dir);
        }
    }
    fs::remove_all(dir);
    fs::remove(csv);
    fs::remove(bad);
}

TEST_CASE("the thread list exports to CSV with one row per thread", "[desktop_v2][threads]") {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 40.0;
    request.settings.max_colors = 4;
    auto out = run_digitize(request, TaskContext{});
    REQUIRE(out.has_value());
    const fs::path file = fs::temp_directory_path() / "os_v2_fils_test.csv";
    std::size_t rows = 0;
    REQUIRE(exportThreadListCsv(out->project, file, &rows).ok);
    CHECK(rows >= 2);
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    const auto csv = text.str();
    CHECK(csv.rfind("ordre;marque;nuancier;reference;nom;couleur;objets;points;longueur_mm;duree_s",
                    0) == 0);
    std::size_t lines = 0;
    for (const char c : csv) {
        lines += c == '\n' ? 1 : 0;
    }
    CHECK(lines == rows + 1); // en-tête + une ligne par fil
    // Rien à exporter : refus clair.
    CHECK_FALSE(exportThreadListCsv(document::Project{}, file).ok);
    fs::remove(file);
}
