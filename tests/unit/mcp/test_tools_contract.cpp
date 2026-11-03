// SPDX-License-Identifier: Apache-2.0
// HP-QA-020 : outils project_diff / project_validate, format d'export en énumération,
// outputSchema et structuredContent des outils stables.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/mcp/server.hpp"
#include "openstitch/project_ops/project_file.hpp"
#include "openstitch/project_ops/quality.hpp"
#include "openstitch/project_ops/report.hpp"

using json = nlohmann::json;
using openstitch::mcp::Server;
namespace fs = std::filesystem;
namespace po = openstitch::project_ops;

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

json text_of(const json& response) {
    return json::parse(response.at("result").at("content").at(0).at("text").get<std::string>());
}

json tools_list(Server& server) {
    return rpc(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}})
        .at("result")
        .at("tools");
}

json tool_named(const json& tools, const std::string& name) {
    for (const auto& t : tools) {
        if (t.at("name") == name) {
            return t;
        }
    }
    FAIL("outil absent : " << name);
    return {};
}

fs::path work_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "openstitch_mcp_contract" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string generic(const fs::path& p) {
    return p.generic_string();
}

// Session avec un projet contenant un rectangle, déjà écrit dans `osp`.
void make_project(Server& server, const fs::path& osp) {
    REQUIRE(call(server, "project_new", {{"width_mm", 60}, {"height_mm", 40}})
                .at("result")
                .at("isError") == false);
    const auto applied = call(server, "project_apply",
                              {{"operations", json::array({{{"op", "add_shape"},
                                                            {"shape", "rect"},
                                                            {"width_mm", 20},
                                                            {"height_mm", 10},
                                                            {"center_mm", json::array({30, 20})},
                                                            {"stitch_type", "tatami"}}})}});
    REQUIRE(applied.at("result").at("isError") == false);
    REQUIRE(call(server, "project_save", {{"path", generic(osp)}}).at("result").at("isError") ==
            false);
}

} // namespace

TEST_CASE("mcp tools/list annonce project_diff, project_validate et le format en enumeration",
          "[mcp][contract]") {
    Server server;
    const auto tools = tools_list(server);
    for (const char* name : {"project_diff", "project_validate"}) {
        const auto tool = tool_named(tools, name);
        CHECK(tool.at("annotations").at("readOnlyHint") == true);
        CHECK(tool.at("outputSchema").at("type") == "object");
    }
    const auto format =
        tool_named(tools, "project_export").at("inputSchema").at("properties").at("format");
    std::set<std::string> values;
    for (const auto& v : format.at("enum")) {
        values.insert(v.get<std::string>());
    }
    for (const char* expected : {"DST", "PES", "JEF", "EXP", "SVG"}) {
        CHECK(values.contains(expected));
    }
}

TEST_CASE("mcp project_export refuse un format inconnu avec un code stable", "[mcp][contract]") {
    Server server;
    const auto dir = work_dir("export_refus");
    make_project(server, dir / "p.osp");
    for (const char* bad : {"BMP", "vp3", "", "DST ", "../x", "PNG"}) {
        CAPTURE(bad);
        const auto r =
            call(server, "project_export", {{"path", generic(dir / "out.bin")}, {"format", bad}});
        CHECK(r.at("result").at("isError") == true);
        CHECK(text_of(r).at("error").at("code") == "E_UNSUPPORTED_FORMAT");
        CHECK(text_of(r).at("schema") == "openstitch.error/1");
        CHECK_FALSE(fs::exists(dir / "out.bin"));
    }
    // Sans projet ouvert, le refus de format passe avant : une session vide donne E_NO_PROJECT
    // pour un format valide, pas pour un format invalide.
    Server empty;
    CHECK(text_of(call(empty, "project_export", {{"path", "x.dst"}, {"format", "BMP"}}))
              .at("error")
              .at("code") == "E_UNSUPPORTED_FORMAT");
    CHECK(text_of(call(empty, "project_export", {{"path", "x.dst"}, {"format", "DST"}}))
              .at("error")
              .at("code") == "E_NO_PROJECT");
}

TEST_CASE("mcp project_export ecrit DST, PES, JEF, EXP et SVG", "[mcp][contract]") {
    Server server;
    const auto dir = work_dir("export_ok");
    make_project(server, dir / "p.osp");
    for (const char* format : {"DST", "PES", "JEF", "EXP", "SVG", "dst"}) {
        CAPTURE(format);
        const auto out = dir / (std::string("out_") + format + ".bin");
        const auto r = call(server, "project_export", {{"path", generic(out)}, {"format", format}});
        REQUIRE(r.at("result").at("isError") == false);
        CHECK(fs::exists(out));
        CHECK(fs::file_size(out) > 0);
        CHECK(r.at("result").at("structuredContent").at("ok") == true);
    }
    std::ifstream svg(dir / "out_SVG.bin");
    std::stringstream text;
    text << svg.rdbuf();
    CHECK(text.str().find("<svg") != std::string::npos);
}

TEST_CASE("mcp project_diff rend le JSON de la CLI et structuredContent", "[mcp][contract]") {
    Server server;
    const auto dir = work_dir("diff");
    make_project(server, dir / "a.osp");
    // b : variante modifiée.
    REQUIRE(call(server, "project_apply",
                 {{"operations", json::array({{{"op", "add_shape"},
                                               {"shape", "rect"},
                                               {"width_mm", 5},
                                               {"height_mm", 5},
                                               {"center_mm", json::array({10, 10})},
                                               {"stitch_type", "tatami"}}})}})
                .at("result")
                .at("isError") == false);
    REQUIRE(call(server, "project_save", {{"path", generic(dir / "b.osp")}})
                .at("result")
                .at("isError") == false);

    const auto r = call(server, "project_diff",
                        {{"a", generic(dir / "a.osp")}, {"b", generic(dir / "b.osp")}});
    REQUIRE(r.at("result").at("isError") == false);
    const auto body = text_of(r);
    CHECK(body.at("schema") == "openstitch.project-diff/1");
    CHECK(body.at("identical") == false);
    CHECK(r.at("result").at("structuredContent") == body);

    // Même contenu que la fonction partagée avec la CLI.
    auto a = po::open_project(dir / "a.osp");
    auto b = po::open_project(dir / "b.osp");
    REQUIRE(a);
    REQUIRE(b);
    const auto expected = po::diff_projects_json(a->project, b->project);
    REQUIRE(expected);
    CHECK(body == json::parse(*expected));

    // « a » omis : référence = projet de la session (identique à b.osp).
    const auto same = call(server, "project_diff", {{"b", generic(dir / "b.osp")}});
    CHECK(text_of(same).at("identical") == true);
}

TEST_CASE("mcp project_diff et project_validate refusent proprement", "[mcp][contract]") {
    Server server;
    const auto dir = work_dir("refus");
    // Sans projet de session et sans « a » : E_NO_PROJECT ; fichier « b » absent ou « a »
    // illisible : E_INVALID_PROJECT.
    CHECK(text_of(call(server, "project_diff", {{"b", generic(dir / "x.osp")}}))
              .at("error")
              .at("code") == "E_NO_PROJECT");
    CHECK(text_of(call(server, "project_diff",
                       {{"a", generic(dir / "x.osp")}, {"b", generic(dir / "y.osp")}}))
              .at("error")
              .at("code") == "E_INVALID_PROJECT");
    CHECK(text_of(call(server, "project_diff", {})).at("error").at("code") == "E_USAGE");
    CHECK(text_of(call(server, "project_validate", {})).at("error").at("code") == "E_NO_PROJECT");
    CHECK(text_of(call(server, "project_validate", {{"path", generic(dir / "absent.osp")}}))
              .at("error")
              .at("code") == "E_INVALID_PROJECT");
    std::ofstream(dir / "faux.osp") << "pas un zip";
    CHECK(text_of(call(server, "project_validate", {{"path", generic(dir / "faux.osp")}}))
              .at("error")
              .at("code") == "E_INVALID_PROJECT");
    const auto r = call(server, "project_validate", {{"path", 3}});
    CHECK(r.at("result").at("isError") == true);
    CHECK(r.at("result").contains("structuredContent") == false);
}

TEST_CASE("mcp project_validate rend le JSON de la CLI", "[mcp][contract]") {
    Server server;
    const auto dir = work_dir("validate");
    make_project(server, dir / "p.osp");
    const auto r = call(server, "project_validate", {{"path", generic(dir / "p.osp")}});
    REQUIRE(r.at("result").at("isError") == false);
    const auto body = text_of(r);
    CHECK(body.at("schema") == "openstitch.validation/1");
    CHECK(body.at("valid").is_boolean());
    CHECK(body.at("integrity").is_array());
    CHECK(body.at("quality").at("schema") == "openstitch.quality/1");
    CHECK(r.at("result").at("structuredContent") == body);

    auto opened = po::open_project(dir / "p.osp");
    REQUIRE(opened);
    const auto expected = po::build_validation_report(opened->project);
    REQUIRE(expected);
    CHECK(body == json::parse(expected->json));

    // Sans « path » : projet de la session, même verdict.
    CHECK(text_of(call(server, "project_validate")) == body);
}

TEST_CASE("mcp outputSchema: chaque schema declare existe dans docs/schemas", "[mcp][contract]") {
    Server server;
    const auto tools = tools_list(server);
    int with_output = 0;
    for (const auto& tool : tools) {
        if (!tool.contains("outputSchema")) {
            continue;
        }
        ++with_output;
        CAPTURE(tool.at("name"));
        const auto& out = tool.at("outputSchema");
        CHECK(out.at("type") == "object");
        const auto& props = out.at("properties");
        if (props.contains("schema")) {
            std::string id = props.at("schema").at("const").get<std::string>();
            for (auto& c : id) {
                if (c == '/') {
                    c = '.';
                }
            }
            const fs::path file = fs::path(OPENSTITCH_SCHEMAS_DIR) / (id + ".schema.json");
            CHECK(fs::exists(file));
        }
    }
    CHECK(with_output >= 7);
}

TEST_CASE("mcp structuredContent: present pour un outil stable, absent pour une erreur",
          "[mcp][contract]") {
    Server server;
    const auto dir = work_dir("structured");
    make_project(server, dir / "p.osp");
    const auto ok = call(server, "project_check");
    REQUIRE(ok.at("result").at("isError") == false);
    CHECK(ok.at("result").at("structuredContent").at("schema") == "openstitch.quality/1");
    const auto select = call(server, "project_select", {{"where", json::array({"type=tatami"})}});
    CHECK(select.at("result").at("structuredContent").at("schema") == "openstitch.selection/1");
    // Outil sans outputSchema : pas de structuredContent.
    CHECK_FALSE(call(server, "project_status").at("result").contains("structuredContent"));
    const auto bad = call(server, "project_check", {{"object", 9999}});
    CHECK(bad.at("result").at("isError") == true);
    CHECK_FALSE(bad.at("result").contains("structuredContent"));
}
