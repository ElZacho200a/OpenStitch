// SPDX-License-Identifier: Apache-2.0
// Guide de numérisation embarqué (skill openstitch-digitize) : instructions, outil, ressources,
// prompt et palette réelle.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
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

std::string block_text(const json& response) {
    return response.at("result").at("content")[0].at("text").get<std::string>();
}

std::string guide_text(Server& server, const std::string& topic) {
    const auto r = call(server, "digitize_guide", {{"topic", topic}});
    REQUIRE(r.at("result").at("isError") == false);
    return block_text(r);
}

std::string read_normalized(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string out;
    for (const char c : text) {
        if (c != '\r') {
            out += c;
        }
    }
    return out;
}

} // namespace

TEST_CASE("guide: initialize pousse instructions, ressources et prompts", "[mcp][guide]") {
    Server server;
    const auto r = rpc(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
    const auto& result = r.at("result");
    CHECK(result.at("instructions").get<std::string>().find("digitize_guide") != std::string::npos);
    CHECK(result.at("capabilities").contains("resources"));
    CHECK(result.at("capabilities").contains("prompts"));
}

TEST_CASE("guide: sommaire, sujets et refus d'un sujet inconnu", "[mcp][guide]") {
    Server server;
    const auto index = block_text(call(server, "digitize_guide"));
    for (const char* topic : {"workflow", "mcp", "selection-satins", "parametres"}) {
        CAPTURE(topic);
        CHECK(index.find(std::string("- ") + topic) != std::string::npos);
    }
    const auto workflow = guide_text(server, "workflow");
    CHECK(workflow.find("Phase 5") != std::string::npos);
    CHECK(workflow.rfind("---", 0) != 0); // front matter YAML retiré
    CHECK(guide_text(server, "mcp").find("project_palette") != std::string::npos);

    const auto bad = call(server, "digitize_guide", {{"topic", "zzz"}});
    CHECK(bad.at("result").at("isError") == true);
    const auto error = json::parse(block_text(bad)).at("error");
    CHECK(error.at("hint").get<std::string>().find("workflow") != std::string::npos);
}

TEST_CASE("guide: texte embarque identique au skill openstitch-digitize", "[mcp][guide]") {
    namespace fs = std::filesystem;
    Server server;
    const fs::path skill = OPENSTITCH_SKILL_DIR;

    std::size_t checked = 0;
    for (const auto& entry : fs::directory_iterator(skill / "references")) {
        if (entry.path().extension() != ".md") {
            continue;
        }
        CAPTURE(entry.path().filename().string());
        CHECK(guide_text(server, entry.path().stem().string()) == read_normalized(entry.path()));
        ++checked;
    }
    CHECK(checked >= 7);

    std::string body = read_normalized(skill / "SKILL.md");
    REQUIRE(body.rfind("---\n", 0) == 0);
    body = body.substr(body.find("\n---\n", 3) + 5);
    body = body.substr(body.find_first_not_of('\n'));
    CHECK(guide_text(server, "workflow") == body);
}

TEST_CASE("guide: ressources et prompt", "[mcp][guide]") {
    Server server;
    const auto list = rpc(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "resources/list"}});
    bool has_workflow = false;
    for (const auto& r : list.at("result").at("resources")) {
        has_workflow = has_workflow || r.at("uri") == "openstitch://guide/workflow";
    }
    CHECK(has_workflow);

    const auto read = rpc(server, {{"jsonrpc", "2.0"},
                                   {"id", 2},
                                   {"method", "resources/read"},
                                   {"params", {{"uri", "openstitch://guide/workflow"}}}});
    CHECK(read.at("result").at("contents")[0].at("text").get<std::string>().find("Phase 5") !=
          std::string::npos);
    const auto unknown = rpc(server, {{"jsonrpc", "2.0"},
                                      {"id", 3},
                                      {"method", "resources/read"},
                                      {"params", {{"uri", "openstitch://guide/zzz"}}}});
    CHECK(unknown.at("error").at("code") == -32602);

    const auto prompts = rpc(server, {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "prompts/list"}});
    CHECK(prompts.at("result").at("prompts")[0].at("name") == "digitize");
    const auto prompt =
        rpc(server, {{"jsonrpc", "2.0"},
                     {"id", 5},
                     {"method", "prompts/get"},
                     {"params",
                      {{"name", "digitize"},
                       {"arguments", {{"image", "tentacule.png"}, {"width_mm", "100"}}}}}});
    const auto text =
        prompt.at("result").at("messages")[0].at("content").at("text").get<std::string>();
    CHECK(text.find("tentacule.png") != std::string::npos);
    CHECK(text.find("100 mm") != std::string::npos);
    CHECK(text.find("Phase 5") != std::string::npos);
    const auto bad = rpc(
        server,
        {{"jsonrpc", "2.0"}, {"id", 6}, {"method", "prompts/get"}, {"params", {{"name", "zzz"}}}});
    CHECK(bad.at("error").at("code") == -32602);
}

TEST_CASE("guide: project_palette donne les blocs dans l'ordre de couture", "[mcp][guide]") {
    Server server;
    call(server, "project_new", {{"width_mm", 60}, {"height_mm", 40}});
    const auto shape = [](double x, const char* alias) {
        return json{{"op", "add_shape"}, {"shape", "rect"},     {"width_mm", 8},
                    {"height_mm", 8},    {"center_mm", {x, 0}}, {"stitch_type", "tatami"},
                    {"as", alias}};
    };
    const auto color = [](const char* alias, const char* rgb) {
        return json{{"op", "set_color"}, {"object", std::string("$") + alias}, {"rgb", rgb}};
    };
    const auto applied =
        call(server, "project_apply",
             {{"operations", json::array({shape(-10, "a"), color("a", "#FF0000"), shape(10, "b"),
                                          color("b", "#0000FF")})}});
    REQUIRE(applied.at("result").at("isError") == false);

    const auto palette = json::parse(block_text(call(server, "project_palette")));
    CHECK(palette.at("block_count") == 2);
    CHECK(palette.at("blocks_in_sewing_order") == json::array({"#FF0000", "#0000FF"}));
    REQUIRE(palette.at("colors").size() == 2);
    CHECK(palette.at("colors")[0].at("objects") == 1);
    CHECK(palette.at("colors")[0].at("area_mm2").get<double>() > 50.0);
}
