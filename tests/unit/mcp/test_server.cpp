// SPDX-License-Identifier: Apache-2.0
// Serveur MCP : protocole JSON-RPC et enchaînement d'outils sur une session (sans stdio).
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

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

    const auto inspected = text_of(call(server, "project_inspect"));
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
