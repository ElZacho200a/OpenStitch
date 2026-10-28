// SPDX-License-Identifier: Apache-2.0
// Serveur MCP : protocole JSON-RPC et enchaînement d'outils sur une session (sans stdio).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/mcp/server.hpp"

using json = nlohmann::json;
using openstitch::mcp::Server;

namespace {

json rpc(Server& server, const json& request) {
    const auto response = server.handle(request.dump());
    REQUIRE(response.has_value());
    return json::parse(*response);
}

json call(Server& server, const std::string& tool, const json& args = json::object()) {
    return rpc(server, {{"jsonrpc", "2.0"},
                        {"id", 1},
                        {"method", "tools/call"},
                        {"params", {{"name", tool}, {"arguments", args}}}});
}

// Texte du premier bloc `text` du résultat d'un outil, analysé comme JSON.
json text_of(const json& response) {
    for (const auto& block : response.at("result").at("content")) {
        if (block.at("type") == "text") {
            return json::parse(block.at("text").get<std::string>());
        }
    }
    throw std::runtime_error("aucun bloc texte");
}

} // namespace

TEST_CASE("mcp initialize annonce les outils et rejoue la version du client", "[mcp]") {
    Server server;
    const auto r = rpc(server, {{"jsonrpc", "2.0"},
                                {"id", 7},
                                {"method", "initialize"},
                                {"params", {{"protocolVersion", "2024-11-05"}}}});
    CHECK(r.at("id") == 7);
    CHECK(r.at("result").at("protocolVersion") == "2024-11-05");
    CHECK(r.at("result").at("capabilities").contains("tools"));
    CHECK(r.at("result").at("serverInfo").at("name") == "openstitch-mcp");
}

TEST_CASE("mcp notification sans reponse, ping et methode inconnue", "[mcp]") {
    Server server;
    CHECK_FALSE(server.handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
    CHECK(rpc(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}}).contains("result"));
    const auto unknown = rpc(server, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "nope"}});
    CHECK(unknown.at("error").at("code") == -32601);
    const auto garbage = json::parse(*server.handle("{pas du json"));
    CHECK(garbage.at("error").at("code") == -32700);
}

TEST_CASE("mcp tools/list expose des schemas JSON valides", "[mcp]") {
    Server server;
    const auto r = rpc(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    const auto& tools = r.at("result").at("tools");
    REQUIRE(tools.size() >= 9);
    for (const auto& tool : tools) {
        CAPTURE(tool.at("name"));
        CHECK_FALSE(tool.at("description").get<std::string>().empty());
        CHECK(tool.at("inputSchema").at("type") == "object");
    }
}

TEST_CASE("mcp outil sans projet ouvert donne un refus actionnable", "[mcp]") {
    Server server;
    const auto r = call(server, "project_inspect");
    CHECK(r.at("result").at("isError") == true);
    const auto err = text_of(r).at("error");
    CHECK(err.at("code") == "E_NO_PROJECT");
    CHECK_FALSE(err.at("hint").get<std::string>().empty());
}

TEST_CASE("mcp outil inconnu et arguments mal types", "[mcp]") {
    Server server;
    CHECK(text_of(call(server, "zzz")).at("error").at("code") == "E_UNKNOWN_TOOL");
    CHECK(text_of(call(server, "project_open")).at("error").at("code") == "E_USAGE");
    CHECK(text_of(call(server, "project_new", {{"width_mm", "large"}})).at("error").at("code") ==
          "E_USAGE");
}

TEST_CASE("mcp numerisation: reglage inconnu liste les reglages valides", "[mcp]") {
    Server server;
    const auto r = call(server, "project_digitize",
                        {{"image", "absente.png"}, {"settings", {{"couleurs", 4}}}});
    CHECK(r.at("result").at("isError") == true);
    CHECK(text_of(r).at("error").at("hint").get<std::string>().find("max_colors") !=
          std::string::npos);
}

TEST_CASE("mcp session: creer, modifier par lot, controler, rendre", "[mcp]") {
    Server server;
    const auto created = call(server, "project_new", {{"width_mm", 60}, {"height_mm", 40}});
    REQUIRE(created.at("result").at("isError") == false);
    CHECK(text_of(created).at("dirty") == true);

    const auto applied = call(
        server, "project_apply",
        {{"operations", json::array({{{"op", "add_shape"},
                                      {"shape", "rect"},
                                      {"width_mm", 20},
                                      {"height_mm", 10},
                                      {"center_mm", json::array({30, 20})},
                                      {"stitch_type", "tatami"},
                                      {"as", "r"}},
                                     {{"op", "rename"}, {"object", "$r"}, {"name", "Carre"}}})}});
    REQUIRE(applied.at("result").at("isError") == false);
    CHECK(text_of(applied).at("applied") == true);

    // Un lot refusé laisse le projet intact et nomme l'opération fautive.
    const auto refused = call(
        server, "project_apply",
        {{"operations", json::array({{{"op", "rename"}, {"object", 999999}, {"name", "x"}}})}});
    CHECK(refused.at("result").at("isError") == true);
    CHECK(text_of(refused).at("applied") == false);

    const auto inspected = text_of(call(server, "project_inspect", {{"detail", "full"}}));
    CHECK(inspected.dump().find("Carre") != std::string::npos);

    const auto checked = call(server, "project_check");
    CHECK(checked.at("result").at("isError") == false);

    const auto rendered = call(server, "project_render", {{"px_per_mm", 2}});
    REQUIRE(rendered.at("result").at("isError") == false);
    const auto& blocks = rendered.at("result").at("content");
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].at("type") == "image");
    CHECK(blocks[0].at("mimeType") == "image/png");
    // Signature PNG en base64 : « iVBORw0KGgo ».
    CHECK(blocks[0].at("data").get<std::string>().rfind("iVBORw0KGgo", 0) == 0);
}

TEST_CASE("mcp inspect: resume compact par defaut, complet sur demande", "[mcp]") {
    Server server;
    call(server, "project_new", {{"width_mm", 60}, {"height_mm", 40}});
    call(server, "project_apply",
         {{"operations", json::array({{{"op", "add_shape"},
                                       {"shape", "rect"},
                                       {"width_mm", 20},
                                       {"height_mm", 10},
                                       {"center_mm", json::array({0, 0})},
                                       {"stitch_type", "tatami"}},
                                      {{"op", "add_shape"},
                                       {"shape", "rect"},
                                       {"width_mm", 5},
                                       {"height_mm", 5},
                                       {"center_mm", json::array({10, 10})},
                                       {"stitch_type", "running"}}})}});
    const auto summary = text_of(call(server, "project_inspect"));
    CHECK(summary.at("detail") == "summary");
    CHECK(summary.at("embroidery_objects") == 2);
    CHECK(summary.at("groups").contains("tatami"));
    CHECK(summary.at("groups").contains("running"));
    CHECK_FALSE(summary.contains("vector_objects"));
    const auto full = text_of(call(server, "project_inspect", {{"detail", "full"}}));
    CHECK(full.contains("vector_objects"));
    CHECK(full.dump().size() > summary.dump().size());
    CHECK(text_of(call(server, "project_inspect", {{"detail", "zzz"}})).at("error").at("code") ==
          "E_USAGE");
}

TEST_CASE("mcp select filtre par critere et refuse un critere inconnu", "[mcp]") {
    Server server;
    call(server, "project_new");
    call(server, "project_apply",
         {{"operations", json::array({{{"op", "add_shape"},
                                       {"shape", "rect"},
                                       {"width_mm", 20},
                                       {"height_mm", 10},
                                       {"center_mm", json::array({0, 0})},
                                       {"stitch_type", "tatami"}}})}});
    const auto all =
        text_of(call(server, "project_select", {{"where", json::array({"type=tatami"})}}));
    CHECK(all.at("count") == 1);
    const auto none =
        text_of(call(server, "project_select", {{"where", json::array({"type=running"})}}));
    CHECK(none.at("count") == 0);
    CHECK(text_of(call(server, "project_select", {{"where", json::array({"couleur=rouge"})}}))
              .at("error")
              .at("code") == "E_USAGE");
}

TEST_CASE("mcp fit_canvas agrandit le cadre et supprime les points hors cadre", "[mcp]") {
    Server server;
    call(server, "project_new", {{"width_mm", 20}, {"height_mm", 20}});
    call(server, "project_apply",
         {{"operations", json::array({{{"op", "add_shape"},
                                       {"shape", "rect"},
                                       {"width_mm", 60},
                                       {"height_mm", 30},
                                       {"center_mm", json::array({0, 0})},
                                       {"stitch_type", "tatami"}}})}});
    const auto before = text_of(call(server, "project_check")).at("summary").at("errors");
    CHECK(before.get<int>() > 0);
    const auto fitted = text_of(call(server, "project_fit_canvas", {{"margin_mm", 2}}));
    CHECK(fitted.at("canvas").at("width_mm").get<double>() >= 60.0);
    CHECK(text_of(call(server, "project_check")).at("summary").at("errors") == 0);
}

TEST_CASE("mcp numerisation reelle: cadre ajuste et notes sur rings_satin", "[mcp][slow]") {
    Server server;
    const std::string image = std::string(OPENSTITCH_FIXTURES_DIR) + "/tentabrode.png";
    const auto r = call(server, "project_digitize",
                        {{"image", image}, {"settings", {{"dpi", 284}, {"rings_satin", true}}}});
    REQUIRE(r.at("result").at("isError") == false);
    const auto report = text_of(r);
    CHECK(report.contains("canvas_fit"));
    CHECK(report.at("notes").dump().find("dark_lines") != std::string::npos);
    CHECK(text_of(call(server, "project_check")).at("summary").at("errors") == 0);
    const auto summary = call(server, "project_inspect");
    CHECK(text_of(summary).dump().size() < 20000);
}

TEST_CASE("mcp apply skip_refused ecarte les refus et applique le reste", "[mcp]") {
    Server server;
    call(server, "project_new");
    const json good = {{"op", "add_shape"}, {"shape", "rect"},     {"width_mm", 10},
                       {"height_mm", 10},   {"center_mm", {0, 0}}, {"stitch_type", "tatami"}};
    const json bad = {{"op", "rename"}, {"object", 999999}, {"name", "x"}};
    const auto strict = call(server, "project_apply", {{"operations", json::array({bad, good})}});
    CHECK(strict.at("result").at("isError") == true);
    CHECK(text_of(strict).at("applied") == false);

    const auto lenient =
        call(server, "project_apply",
             {{"operations", json::array({good, bad, good})}, {"skip_refused", true}});
    REQUIRE(lenient.at("result").at("isError") == false);
    const auto report = text_of(lenient);
    CHECK(report.at("applied") == true);
    CHECK(report.at("requested") == 3);
    REQUIRE(report.at("skipped").size() == 1);
    CHECK(report.at("skipped")[0].at("index") == 1);
    CHECK(report.at("skipped")[0].at("code") == "E_UNKNOWN_OBJECT");
    CHECK(text_of(call(server, "project_inspect")).at("embroidery_objects") == 2);
}

TEST_CASE("mcp arguments non objet et identifiants invalides sont refuses", "[mcp]") {
    Server server;
    call(server, "project_new");
    CHECK(text_of(call(server, "project_new", json::array({1}))).at("error").at("code") ==
          "E_USAGE");
    for (const json& bad : {json(-1), json(0), json(1.5), json("12")}) {
        CAPTURE(bad.dump());
        CHECK(text_of(call(server, "project_inspect", {{"object", bad}})).at("error").at("code") ==
              "E_USAGE");
    }
    CHECK(text_of(call(server, "project_check", {{"object", 424242}})).at("error").at("code") ==
          "E_UNKNOWN_OBJECT");
}

TEST_CASE("mcp rendu: zone de recadrage hors bornes refusee", "[mcp]") {
    Server server;
    call(server, "project_new");
    for (const json& crop : {json::array({0.9, 0.9, 0.1, 0.1}), json::array({-1, 0, 1, 1}),
                             json::array({0, 0, 2, 1}), json::array({0.5, 0, 0.5, 1})}) {
        CAPTURE(crop.dump());
        CHECK(text_of(call(server, "project_render", {{"crop", crop}})).at("error").at("code") ==
              "E_USAGE");
    }
}

TEST_CASE("mcp chemins UTF-8: enregistrer et rouvrir sous un dossier accentue", "[mcp]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / fs::path(u8"openstitch-mcp-tést-é");
    fs::create_directories(dir);
    const auto u8 = (dir / fs::path(u8"projet.osp")).generic_u8string();
    const std::string path(reinterpret_cast<const char*>(u8.data()), u8.size());

    Server server;
    call(server, "project_new", {{"width_mm", 30}, {"height_mm", 30}});
    const auto saved = call(server, "project_save", {{"path", path}});
    CHECK(saved.at("result").at("isError") == false);
    CHECK(text_of(saved).at("path") == path);
    const auto opened = call(server, "project_open", {{"path", path}});
    CHECK(opened.at("result").at("isError") == false);
    fs::remove_all(dir);
}


// Contrats et sécurité de session (aucune dependance sur un chemin de fixture).
TEST_CASE("mcp request contract enforces envelope and size", "[mcp][contract]") {
    Server s;
    CHECK(rpc(s, {{"id", 1}, {"method", "ping"}}).at("error").at("code") == -32600);
    CHECK(rpc(s, {{"jsonrpc", "2.0"}, {"id", true}, {"method", "ping"}})
              .at("error").at("code") == -32600);
    CHECK(rpc(s, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"},
                  {"params", json::array()}}).at("error").at("code") == -32602);
    const auto init = rpc(s, {{"jsonrpc", "2.0"}, {"id", 1},
                              {"method", "initialize"},
                              {"params", {{"protocolVersion", "2099-01-01"}}}});
    CHECK(init.at("result").at("protocolVersion") == "2025-06-18");
    CHECK(json::parse(*s.handle(std::string(8 * 1024 * 1024 + 1, 'x')))
              .at("error").at("code") == -32600);
}

TEST_CASE("mcp tool arguments and annotations are actionable", "[mcp][contract]") {
    Server s;
    CHECK(text_of(call(s, "project_new", {{"unknown", 1}}))
              .at("error").at("code") == "E_USAGE");
    CHECK(text_of(call(s, "project_new", {{"lock", "banana"}}))
              .at("error").at("code") == "E_USAGE");
    const auto list = rpc(s, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    bool status_readonly = false, close_destructive = false;
    for (const auto& item : list.at("result").at("tools")) {
        if (item.at("name") == "project_status") {
            status_readonly = item.at("annotations").at("readOnlyHint") == true;
        }
        if (item.at("name") == "project_close") {
            close_destructive = item.at("annotations").at("destructiveHint") == true;
        }
    }
    CHECK(status_readonly);
    CHECK(close_destructive);
}

TEST_CASE("mcp unsaved edits cannot be replaced silently", "[mcp][contract]") {
    Server s;
    CHECK(text_of(call(s, "project_status")).at("has_project") == false);
    REQUIRE(call(s, "project_new").at("result").at("isError") == false);
    REQUIRE(call(s, "project_checkpoint", {{"name", "old"}})
                .at("result").at("isError") == false);
    CHECK(text_of(call(s, "project_new")).at("error").at("code") == "E_UNSAVED_CHANGES");
    CHECK(text_of(call(s, "project_open", {{"path", "missing.osp"}}))
              .at("error").at("code") == "E_UNSAVED_CHANGES");
    CHECK(text_of(call(s, "project_digitize", {{"image", "missing.png"}}))
              .at("error").at("code") == "E_UNSAVED_CHANGES");
    CHECK(text_of(call(s, "project_close")).at("error").at("code") ==
          "E_UNSAVED_CHANGES");
    REQUIRE(call(s, "project_new", {{"discard_changes", true}})
                .at("result").at("isError") == false);
    CHECK(text_of(call(s, "project_status")).at("checkpoints").empty());
    REQUIRE(call(s, "project_close", {{"discard_changes", true}})
                .at("result").at("isError") == false);
    CHECK(text_of(call(s, "project_status")).at("has_project") == false);
}
