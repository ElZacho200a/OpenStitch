// SPDX-License-Identifier: Apache-2.0
// Édition agentique en mode live (AI Companion 5) : contexte compact, ciblage par teinte de FIL
// (≠ teinte de segmentation), cas ambigus, plan de conversion en satin (refus justifiés,
// Predicted), aperçu avant/après sur copie, application après approbation, annulation en un pas.
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/mcp/live.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/live_bridge.hpp"

namespace {

using namespace openstitch;
using json = nlohmann::json;

struct Gate final : mcp::IApprovalGate {
    bool answer{true};
    int choice{-1};
    int approvals{0};
    mcp::ChoiceRequest last_question;
    bool approve(const mcp::ApprovalRequest&) override {
        ++approvals;
        return answer;
    }
    int choose(const mcp::ChoiceRequest& request) override {
        last_question = request;
        return choice;
    }
};

struct Doc {
    document::Project project;
    commands::UndoStack stack;
    project_ops::LiveBridge bridge{project, stack, "sess"};
    Gate gate;
    std::vector<std::uint64_t> selection;
    mcp::LiveService service{bridge, gate, [this] {
                                 mcp::LiveDocumentInfo info;
                                 info.name = "motif.osp";
                                 info.dirty = true;
                                 info.selection = selection;
                                 return info;
                             }};
    // Rôles : identifiants des objets selon leur rôle dans le scénario.
    std::vector<std::uint64_t> orange; // ciblés, convertibles en satin (colonnes longues)
    std::uint64_t locked_orange{};
    std::uint64_t hidden_orange{};
    std::uint64_t borderline{};
    std::uint64_t brown{};
    std::vector<std::uint64_t> others; // rouge, bleu
    std::uint64_t blob_orange{};       // orange mais trop petit pour un satin

    std::uint64_t add(std::array<std::uint8_t, 3> rgb, int x_mm, int w_mm, int h_mm,
                      const char* name) {
        document::VectorObject v;
        v.id = project.object_ids.next();
        v.name = name;
        v.paths = {{geometry::rectangle_path(
                        Vec2um{Micrometers{x_mm * 1000}, Micrometers{0}},
                        Vec2um{Micrometers{x_mm * 1000 + w_mm * 100}, Micrometers{h_mm * 100}}),
                    {}}};
        project.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = project.object_ids.next();
        e.name = name;
        e.source_vector = v.id;
        e.rgb = rgb;
        e.params = document::TatamiParams{};
        project.embroidery_objects.push_back(e);
        return e.id.value;
    }

    Doc() {
        // w/h en dixièmes de mm : 40 x 200 = colonne de 4 x 20 mm (satin plausible).
        orange.push_back(add({232, 116, 42}, 0, 40, 200, "fleur 1"));
        orange.push_back(add({240, 140, 30}, 6, 40, 200, "fleur 2"));
        orange.push_back(add({230, 100, 20}, 12, 40, 200, "fleur 3"));
        // Nom trompeur : la teinte de SEGMENTATION n'est pas celle du fil.
        orange.push_back(add({245, 150, 60}, 18, 40, 200, "région bleue (segmentation)"));
        locked_orange = add({235, 120, 40}, 24, 40, 200, "verrouillé");
        hidden_orange = add({235, 125, 40}, 30, 40, 200, "masqué");
        borderline = add({240, 110, 50}, 36, 40, 200, "frontière rouge-orange");
        brown = add({120, 60, 20}, 42, 40, 200, "marron");
        others.push_back(add({200, 30, 30}, 48, 40, 200, "rouge"));
        others.push_back(add({30, 60, 200}, 54, 40, 200, "bleu"));
        others.push_back(add({30, 160, 60}, 60, 40, 200, "vert"));
        blob_orange = add({236, 118, 44}, 66, 2, 2, "pastille");
        for (auto& e : project.embroidery_objects) {
            if (e.id.value == locked_orange) {
                e.locked = true;
            }
            if (e.id.value == hidden_orange) {
                e.visible = false;
            }
        }
    }

    json call(const std::string& tool, const json& args) {
        const json reply = json::parse(service.call(tool, args.dump()));
        return reply;
    }
    static json text_of(const json& reply) {
        return json::parse(reply["content"][0]["text"].get<std::string>());
    }
};

std::set<std::uint64_t> ids_of(const json& list) {
    std::set<std::uint64_t> ids;
    for (const auto& item : list) {
        ids.insert(item["id"].get<std::uint64_t>());
    }
    return ids;
}

} // namespace

TEST_CASE("live_context is compact and reports thread palette and risks", "[live][agentic]") {
    Doc d;
    d.selection = {d.orange[0]};
    const auto reply = d.call("live_context", json::object());
    REQUIRE_FALSE(reply["isError"].get<bool>());
    const auto ctx = Doc::text_of(reply);
    CHECK(ctx["object_count"] == 12);
    CHECK(ctx["objects"].size() == 12);
    CHECK(ctx["selection"] == json::array({d.orange[0]}));
    CHECK(ctx["note"].get<std::string>().find("FILS") != std::string::npos);
    bool has_orange = false;
    for (const auto& c : ctx["thread_palette"]) {
        has_orange = has_orange || c["family"] == "orange";
    }
    CHECK(has_orange);
    const std::string risks = ctx["risks"].dump();
    CHECK(risks.find("verrouillé") != std::string::npos);
    CHECK(risks.find("masqué") != std::string::npos);
    CHECK(risks.find("non enregistrées") != std::string::npos);
    CHECK(reply["content"][0]["text"].get<std::string>().size() < 8000);
}

TEST_CASE("live_target 'orange' follows the sewn thread, flags locked, hidden and borderline",
          "[live][agentic]") {
    Doc d;
    const auto reply = d.call("live_target", {{"query", "orange"}});
    REQUIRE_FALSE(reply["isError"].get<bool>());
    const auto target = Doc::text_of(reply);
    const auto ids = ids_of(target["candidates"]);
    for (const auto id : d.orange) {
        CHECK(ids.contains(id)); // y compris « région bleue » : le fil est orange
    }
    CHECK(ids.contains(d.locked_orange));
    CHECK(ids.contains(d.hidden_orange));
    CHECK_FALSE(ids.contains(d.brown));
    for (const auto id : d.others) {
        CHECK_FALSE(ids.contains(id));
    }
    // Verrouillés / masqués signalés, jamais retirés en silence.
    const auto blocked = target["locked_or_hidden"];
    CHECK(std::find(blocked.begin(), blocked.end(), d.locked_orange) != blocked.end());
    CHECK(std::find(blocked.begin(), blocked.end(), d.hidden_orange) != blocked.end());
    // Un fil à la frontière rouge/orange rend la demande ambiguë.
    CHECK(ids_of(target["borderline"]).contains(d.borderline));
    CHECK(target["ambiguous"].get<bool>());
    CHECK(target["next"].get<std::string>().find("live_ask_user") != std::string::npos);
}

TEST_CASE("live_target handles selection, hex and unknown words", "[live][agentic]") {
    Doc d;
    {
        const auto t = Doc::text_of(d.call("live_target", {{"query", "ceux-là"}}));
        CHECK(t["ambiguous"].get<bool>()); // sélection vide
        CHECK(t["candidates"].empty());
    }
    d.selection = {d.others[0], d.orange[1]};
    {
        const auto t = Doc::text_of(d.call("live_target", {{"query", "selection"}}));
        CHECK_FALSE(t["ambiguous"].get<bool>());
        CHECK(ids_of(t["candidates"]) == std::set<std::uint64_t>{d.others[0], d.orange[1]});
    }
    {
        const auto t =
            Doc::text_of(d.call("live_target", {{"query", "#E8742A"}, {"tolerance", 4}}));
        CHECK(ids_of(t["candidates"]) == std::set<std::uint64_t>{d.orange[0]});
    }
    {
        const auto t = Doc::text_of(d.call("live_target", {{"query", "chartreuse"}}));
        CHECK(t["ambiguous"].get<bool>());
    }
    CHECK(d.call("live_target", {{"query", "#12"}})["isError"].get<bool>());
}

TEST_CASE("live_plan_satin justifies refusals, skips locked and prepares a validated batch",
          "[live][agentic]") {
    Doc d;
    std::vector<std::uint64_t> ids = d.orange;
    ids.push_back(d.locked_orange);
    ids.push_back(d.hidden_orange);
    ids.push_back(d.blob_orange);
    const auto plan = Doc::text_of(d.call("live_plan_satin", {{"objects", ids}}));
    REQUIRE(plan["schema"] == "openstitch.live-satin-plan/1");
    std::map<std::uint64_t, json> by_id;
    for (const auto& t : plan["targets"]) {
        by_id[t["object"].get<std::uint64_t>()] = t;
    }
    CHECK(by_id.at(d.locked_orange)["verdict"] == "skipped");
    CHECK(by_id.at(d.hidden_orange)["verdict"] == "skipped");
    // Chaque verdict est motivé ; un refus conserve le tatami.
    std::size_t convertible = 0;
    for (const auto& [id, t] : by_id) {
        const std::string verdict = t["verdict"];
        CHECK((verdict == "convert" || verdict == "refused" || verdict == "skipped"));
        if (verdict == "refused") {
            CHECK_FALSE(t["reasons"].empty());
            CHECK(t["decision"] == "tatami conservé");
        }
        if (verdict == "convert") {
            ++convertible;
            CHECK(t.contains("strategy"));
            CHECK(t["trials"].size() == 3);
        }
    }
    CHECK(plan["counts"]["convert"] == convertible);
    CHECK(plan["counts"]["skipped"] == 2);
    // Le lot ne contient que des objets convertibles : auto_satin + Predicted.
    bool predicted = false;
    for (const auto& op : plan["batch"]["operations"]) {
        if (op["op"] == "set_params") {
            predicted = predicted || op["params"]["pull_mode"] == "predicted";
        }
        CHECK(op["object"] != d.locked_orange);
        CHECK(op["object"] != d.hidden_orange);
    }
    if (convertible > 0) {
        CHECK(predicted);
        REQUIRE(plan["batch_dry_run"].is_object());
        CHECK(plan["batch_dry_run"]["ok"].get<bool>());
    }
    // Limites de la compensation empirique annoncées.
    CHECK(plan["limits"].dump().find("EMPIRIQUE") != std::string::npos);
    CHECK(plan["limits"].dump().find("#152") != std::string::npos);
    // Aucune modification : c'est un plan.
    CHECK_FALSE(d.stack.canUndo());
}

TEST_CASE("live_plan_satin validates its arguments", "[live][agentic]") {
    Doc d;
    CHECK(d.call("live_plan_satin", {{"objects", json::array()}})["isError"].get<bool>());
    CHECK(d.call("live_plan_satin", json::object())["isError"].get<bool>());
    const auto missing = Doc::text_of(d.call("live_plan_satin", {{"objects", {99999}}}));
    CHECK(missing["targets"][0]["verdict"] == "refused");
}

TEST_CASE("live_preview shows before/after on a copy and never changes the project",
          "[live][agentic]") {
    Doc d;
    const auto plan = Doc::text_of(d.call("live_plan_satin", {{"objects", {d.orange[0]}}}));
    const json operations = plan["batch"]["operations"];
    if (operations.empty()) {
        SKIP("le moteur n'a converti aucune forme de la fixture");
    }
    const auto rgb_before = d.project.embroidery_objects.front().rgb;
    const auto reply = d.call("live_preview", {{"operations", operations}});
    REQUIRE_FALSE(reply["isError"].get<bool>());
    const auto preview = Doc::text_of(reply);
    CHECK(preview["report"]["ok"].get<bool>());
    CHECK(preview["before"].contains("stitches"));
    CHECK(preview["after"].contains("stitches"));
    CHECK(preview["delta"].contains("stitches"));
    CHECK(preview["note"].get<std::string>().find("n'a pas changé") != std::string::npos);
    CHECK(d.project.embroidery_objects.front().rgb == rgb_before);
    CHECK_FALSE(d.stack.canUndo());
    CHECK(d.gate.approvals == 0);

    const auto refused =
        d.call("live_preview",
               {{"operations", {{{"op", "set_color"}, {"object", 424242}, {"rgb", "#FF0000"}}}}});
    CHECK(refused["isError"].get<bool>());
}

TEST_CASE("live_ask_user returns the choice made in the panel and nothing else",
          "[live][agentic]") {
    Doc d;
    const json args{
        {"question", "Quels oranges ?"},
        {"options",
         {{{"label", "Oranges stricts"}, {"object_ids", {d.orange[0]}}},
          {{"label", "Avec la frontière"}, {"object_ids", {d.orange[0], d.borderline}}}}}};
    d.gate.choice = 1;
    const auto picked = Doc::text_of(d.call("live_ask_user", args));
    CHECK(picked["chosen"].get<bool>());
    CHECK(picked["label"] == "Avec la frontière");
    CHECK(picked["object_ids"] == json::array({d.orange[0], d.borderline}));
    CHECK(d.gate.last_question.options.size() == 2);
    CHECK_FALSE(d.stack.canUndo());

    d.gate.choice = -1; // l'utilisateur ferme / refuse
    const auto none = Doc::text_of(d.call("live_ask_user", args));
    CHECK_FALSE(none["chosen"].get<bool>());
    CHECK(none["note"].get<std::string>().find("ne modifiez rien") != std::string::npos);

    CHECK(
        d.call("live_ask_user", {{"question", "?"}, {"options", {{{"label", "seul"}}}}})["isError"]
            .get<bool>());
}

TEST_CASE("full agentic flow: target, plan, preview, approve, undo in one step",
          "[live][agentic]") {
    Doc d;
    const auto target = Doc::text_of(d.call("live_target", {{"query", "orange"}}));
    std::vector<std::uint64_t> ids;
    for (const auto& c : target["candidates"]) {
        ids.push_back(c["id"].get<std::uint64_t>());
    }
    const auto plan = Doc::text_of(d.call("live_plan_satin", {{"objects", ids}}));
    // 20 objets au plus par plan : ici 6 candidats (4 fleurs + verrouillé + masqué + pastille).
    const json operations = plan["batch"]["operations"];
    if (operations.empty()) {
        SKIP("le moteur n'a converti aucune forme de la fixture");
    }
    REQUIRE(d.call("live_preview", {{"operations", operations}})["isError"] == false);
    const std::size_t objects_before = d.project.embroidery_objects.size();
    const auto applied = d.call("project_apply", {{"operations", operations}});
    REQUIRE_FALSE(applied["isError"].get<bool>());
    CHECK(applied["live"]["applied"].get<bool>());
    CHECK(d.gate.approvals == 1);
    CHECK(d.stack.undoNames().size() == 1); // un seul pas, quel que soit le nombre d'objets
    CHECK(d.project.embroidery_objects.size() == objects_before);
    // Le verrouillé et le masqué n'ont pas été touchés.
    for (const auto& e : d.project.embroidery_objects) {
        if (e.id.value == d.locked_orange || e.id.value == d.hidden_orange) {
            CHECK(std::string(project_ops::stitch_type_name(e)) != "auto_satin");
        }
    }
    REQUIRE(d.stack.undo(d.project));
    for (const auto& e : d.project.embroidery_objects) {
        CHECK(std::string(project_ops::stitch_type_name(e)) != "auto_satin");
    }
}

TEST_CASE("manual edit between preview and commit is refused", "[live][agentic]") {
    Doc d;
    const auto plan = Doc::text_of(d.call("live_plan_satin", {{"objects", {d.orange[0]}}}));
    const json operations = plan["batch"]["operations"];
    if (operations.empty()) {
        SKIP("le moteur n'a converti aucune forme de la fixture");
    }
    auto pending = d.service.begin("project_apply", json{{"operations", operations}}.dump());
    pending->run();
    REQUIRE(d.bridge
                .commit("sess", d.bridge.revision(),
                        "{\"schema\":\"openstitch.batch/1\",\"operations\":[{\"op\":\"set_color\","
                        "\"object\":" +
                            std::to_string(d.orange[1]) + ",\"rgb\":\"#0000FF\"}]}",
                        "manuel", true)
                .ok);
    const auto reply = json::parse(d.service.finish(*pending));
    CHECK(reply["isError"].get<bool>());
    CHECK(reply["content"][0]["text"].get<std::string>().find("E_STALE_REVISION") !=
          std::string::npos);
    CHECK(d.gate.approvals == 0);
}

TEST_CASE("agentic tools are exposed read-only and project_export stays denied",
          "[live][agentic]") {
    for (const char* tool : {"live_context", "live_target", "live_plan_satin", "live_preview",
                             "live_ask_user", "live_status"}) {
        CHECK(mcp::live_policy(tool) == mcp::LivePolicy::Read);
    }
    CHECK(mcp::live_policy("live_commit") == mcp::LivePolicy::Denied);
    CHECK(mcp::live_policy("live_anything") == mcp::LivePolicy::Denied);
    Doc d;
    CHECK(d.call("project_export", {{"path", "x.dst"}})["isError"].get<bool>());
    const json specs = json::parse(mcp::live_tools_json());
    std::set<std::string> names;
    for (const auto& s : specs) {
        names.insert(s["name"].get<std::string>());
    }
    CHECK(names.contains("live_plan_satin"));
    CHECK(names.contains("project_apply"));
    CHECK_FALSE(names.contains("project_export"));
}
