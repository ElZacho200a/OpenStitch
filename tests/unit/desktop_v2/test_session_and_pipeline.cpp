// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "openstitch/formats/dst.hpp"
#include "task_manager.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {

fs::path fixture() {
    return fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
}

// Numérisation de Tentabrode à 300 mm de haut, calculée une seule fois pour toute la suite.
const DigitizeOutput& tentabrode300() {
    static const DigitizeOutput output = [] {
        DigitizeRequest request;
        request.image = fixture();
        request.target_height_mm = 300.0;
        request.settings.max_colors = 6;
        auto r = run_digitize(request, TaskContext{});
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return output;
}

} // namespace

TEST_CASE("pipeline digitizes tentabrode at 300 mm and exports a readable DST",
          "[desktop_v2][pipeline]") {
    const auto& out = tentabrode300();
    CAPTURE(out.width_mm, out.height_mm, out.stats.stitches, out.stats.jumps, out.stats.trims,
            out.stats.color_changes, out.report.embroidery_objects);
    CHECK(out.report.embroidery_objects > 0);
    CHECK(out.project.embroidery_objects.size() == out.report.embroidery_objects);
    CHECK(out.stats.stitches > 1000);
    // La hauteur physique est celle demandée à l'import (le fond ignoré peut réduire l'emprise
    // cousue : on borne par le haut strictement et on exige l'essentiel de la hauteur).
    CHECK(out.height_mm <= 300.5);
    CHECK(out.height_mm >= 0.6 * 300.0);

    const fs::path dst = fs::temp_directory_path() / "openstitch_v2_tentabrode_300.dst";
    fs::remove(dst);
    const auto written = export_dst(out.sequence, dst);
    REQUIRE(written.has_value());
    REQUIRE(fs::exists(dst));
    CHECK(fs::file_size(dst) > 512); // en-tête DST = 512 octets
    const auto reread = formats::read_dst_file(dst);
    REQUIRE(reread.has_value());
    CHECK(reread->commands.size() > 1000);
    fs::remove(dst);
}

TEST_CASE("pipeline is deterministic byte for byte", "[desktop_v2][pipeline]") {
    DigitizeRequest request;
    request.image = fixture();
    request.target_height_mm = 120.0;
    request.settings.max_colors = 4;
    const auto a = run_digitize(request, TaskContext{});
    const auto b = run_digitize(request, TaskContext{});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const fs::path pa = fs::temp_directory_path() / "openstitch_v2_det_a.dst";
    const fs::path pb = fs::temp_directory_path() / "openstitch_v2_det_b.dst";
    REQUIRE(export_dst(a->sequence, pa).has_value());
    REQUIRE(export_dst(b->sequence, pb).has_value());
    const auto slurp = [](const fs::path& p) { // flux fermé avant remove() (Windows verrouille)
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), {});
    };
    const std::string ba = slurp(pa);
    const std::string bb = slurp(pb);
    CHECK(!ba.empty());
    // Comparaison en booléen : Catch2 ne sait pas afficher deux gros blocs binaires.
    const bool identical = (ba == bb);
    CHECK(ba.size() == bb.size());
    CHECK(identical);
    fs::remove(pa);
    fs::remove(pb);
}

TEST_CASE("pipeline refuses a missing image with an actionable error", "[desktop_v2][pipeline]") {
    DigitizeRequest request;
    request.image = fs::path("does/not/exist.png");
    const auto r = run_digitize(request, TaskContext{});
    REQUIRE_FALSE(r.has_value());
    CHECK_FALSE(r.error().message.empty());
}

TEST_CASE("pipeline honours cancellation before doing any work", "[desktop_v2][pipeline]") {
    std::stop_source source;
    source.request_stop();
    TaskContext ctx;
    ctx.stop = source.get_token();
    DigitizeRequest request;
    request.image = fixture();
    const auto r = run_digitize(request, ctx);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::Cancelled);
}

TEST_CASE("pipeline runs through the task manager without touching the live session",
          "[desktop_v2][pipeline][tasks]") {
    DocumentSession session;
    const auto revisionBefore = session.revision();
    TaskManager tasks;
    bool installed = false;
    DigitizeRequest request;
    request.image = fixture();
    request.target_height_mm = 100.0;
    request.settings.max_colors = 4;
    tasks.submit<DigitizeOutput>(
        "Numérisation", [request](const TaskContext& ctx) { return run_digitize(request, ctx); },
        [&](Result<DigitizeOutput>&& r) {
            REQUIRE(r.has_value());
            session.replaceProject(std::move(r->project), false);
            installed = true;
        });
    CHECK(session.revision() == revisionBefore); // rien n'est installé avant pump()
    tasks.waitIdle();
    CHECK(installed);
    CHECK(session.revision() > revisionBefore);
    CHECK_FALSE(session.project().embroidery_objects.empty());
    CHECK(session.dirty());
    CHECK_FALSE(session.canUndo()); // l'ouverture ne s'annule pas
}

TEST_CASE("session batch is one undo step and previews do not mutate", "[desktop_v2][session]") {
    DocumentSession session;
    session.replaceProject(document::Project(tentabrode300().project), true);
    REQUIRE_FALSE(session.project().embroidery_objects.empty());
    const auto firstId = session.project().embroidery_objects.front().id.value;
    const std::string oldName = session.project().embroidery_objects.front().name;
    const std::string batch =
        R"({"schema":"openstitch.batch/1","operations":[{"op":"rename","object":)" +
        std::to_string(firstId) + R"(,"name":"Ciel V2"}]})";

    const auto revision = session.revision();
    const auto preview = session.previewBatch(batch);
    CHECK(preview.report.ok);
    CHECK_FALSE(preview.committed);
    CHECK(session.revision() == revision);
    CHECK(session.project().embroidery_objects.front().name == oldName);

    const auto applied = session.applyBatch(batch, "Renommer");
    REQUIRE(applied.committed);
    CHECK(session.project().embroidery_objects.front().name == "Ciel V2");
    CHECK(session.undoName() == "Renommer");
    CHECK(session.dirty());

    REQUIRE(session.undo());
    CHECK(session.project().embroidery_objects.front().name == oldName);
    REQUIRE(session.redo());
    CHECK(session.project().embroidery_objects.front().name == "Ciel V2");
}

TEST_CASE("a refused batch leaves the document and history untouched", "[desktop_v2][session]") {
    DocumentSession session;
    session.replaceProject(document::Project(tentabrode300().project), true);
    const auto revision = session.revision();
    const auto refused = session.applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"rename","object":999999,"name":"x"}]})",
        "Refus");
    CHECK_FALSE(refused.committed);
    CHECK_FALSE(refused.report.ok);
    CHECK(session.revision() == revision);
    CHECK_FALSE(session.canUndo());
    CHECK_FALSE(session.dirty());
}

TEST_CASE("selection is pruned when undo removes its target", "[desktop_v2][session]") {
    DocumentSession session;
    session.replaceProject(document::Project(tentabrode300().project), true);
    const auto id = session.project().embroidery_objects.front().id.value;
    session.selection().replace({{TargetKind::EmbroideryObject, id}});
    REQUIRE(session.selection().targets().size() == 1);
    // Remplacement par un projet vide : la cible n'existe plus.
    session.replaceProject(document::Project{}, true);
    CHECK(session.selection().empty());
}

TEST_CASE("snapshot is shared within a revision and refreshed after a change",
          "[desktop_v2][session]") {
    DocumentSession session;
    session.replaceProject(document::Project(tentabrode300().project), true);
    const auto a = session.snapshot();
    const auto b = session.snapshot();
    CHECK(a == b);
    const auto id = session.project().embroidery_objects.front().id.value;
    REQUIRE(
        session
            .applyBatch(R"({"schema":"openstitch.batch/1","operations":[{"op":"rename","object":)" +
                            std::to_string(id) + R"(,"name":"N"}]})",
                        "Renommer")
            .committed);
    const auto c = session.snapshot();
    CHECK(c != a);
    CHECK(a->embroidery_objects.front().name != "N"); // l'ancien instantané reste figé
}

TEST_CASE("listeners hear every kind of change", "[desktop_v2][session]") {
    DocumentSession session;
    std::vector<ChangeKind> kinds;
    const auto token = session.subscribe([&](ChangeKind k) { kinds.push_back(k); });
    session.replaceProject(document::Project(tentabrode300().project), true);
    const auto id = session.project().embroidery_objects.front().id.value;
    (void)session.applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"rename","object":)" +
            std::to_string(id) + R"(,"name":"N"}]})",
        "Renommer");
    (void)session.undo();
    (void)session.redo();
    session.unsubscribe(token);
    (void)session.undo();
    REQUIRE(kinds.size() == 4);
    CHECK(kinds[0] == ChangeKind::Replaced);
    CHECK(kinds[1] == ChangeKind::Edited);
    CHECK(kinds[2] == ChangeKind::Undone);
    CHECK(kinds[3] == ChangeKind::Redone);
}
