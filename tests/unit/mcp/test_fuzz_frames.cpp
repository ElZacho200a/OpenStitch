// SPDX-License-Identifier: Apache-2.0
// HP-QA-014 : fuzzing déterministe (graine fixe) des trames JSON-RPC du serveur MCP.
// Contrat : jamais de crash, d'exception ni de blocage ; une réponse est toujours un JSON-RPC
// 2.0 bien formé ; tout refus d'outil porte un code E_* (enveloppe openstitch.error/1) ; une
// erreur de protocole porte un code JSON-RPC négatif et un message.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fuzz_mutator.hpp"
#include "openstitch/mcp/server.hpp"

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
using openstitch::mcp::Server;

constexpr std::uint64_t kBaseSeed = 0x5EED0000F4A3E5ULL;
constexpr std::size_t kMutationsPerSeed = 40;
constexpr auto kPerFrameLimit = std::chrono::seconds(20);

// Les outils de fichiers (project_new/save/export…) écrivent des chemins relatifs : on se place
// dans un dossier jetable profond (un « ../x » reste dans l'arbre temporaire) le temps du test.
struct ScopedCwd {
    fs::path previous = fs::current_path();
    fs::path dir;
    explicit ScopedCwd(const char* name)
        : dir(fs::temp_directory_path() / "openstitch_fuzz_frames" / name / "a" / "b") {
        fs::remove_all(dir.parent_path().parent_path());
        fs::create_directories(dir);
        fs::current_path(dir);
    }
    ~ScopedCwd() {
        std::error_code ec;
        fs::current_path(previous, ec);
    }
};

json frame(int id, const std::string& method, const json& params = json::object()) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

json tool_call(int id, const std::string& tool, const json& args) {
    return frame(id, "tools/call", {{"name", tool}, {"arguments", args}});
}

// Trames valides servant de graines.
std::vector<json> seed_frames() {
    return {
        frame(1, "initialize",
              {{"protocolVersion", "2024-11-05"}, {"capabilities", json::object()}}),
        frame(2, "ping"),
        frame(3, "tools/list"),
        frame(4, "resources/list"),
        frame(5, "resources/read", {{"uri", "openstitch://guide/workflow"}}),
        frame(6, "prompts/list"),
        frame(7, "prompts/get", {{"name", "digitize"}, {"arguments", {{"image", "x.png"}}}}),
        json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}},
        tool_call(10, "project_new", {{"width_mm", 60}, {"height_mm", 40}, {"margin_mm", 3}}),
        tool_call(11, "project_status", json::object()),
        tool_call(12, "project_operations", json::object()),
        tool_call(13, "digitize_guide", {{"topic", "mcp"}}),
        tool_call(
            14, "project_apply",
            {{"operations", json::array({{{"op", "add_shape"},
                                          {"shape", "rect"},
                                          {"width_mm", 20},
                                          {"height_mm", 10},
                                          {"center_mm", json::array({30, 20})},
                                          {"stitch_type", "tatami"},
                                          {"as", "r"}},
                                         {{"op", "rename"}, {"object", "$r"}, {"name", "A"}}})},
             {"dry_run", true}}),
        tool_call(15, "project_inspect", {{"detail", "summary"}}),
        tool_call(16, "project_select", {{"where", json::array({"type=tatami"})}}),
        tool_call(17, "project_check", json::object()),
        tool_call(18, "project_palette", json::object()),
        tool_call(19, "project_diagnose", json::object()),
        tool_call(20, "project_locate", {{"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 5}}),
        tool_call(21, "project_checkpoint", {{"name", "v1"}}),
        tool_call(22, "project_restore", {{"name", "v1"}}),
        tool_call(23, "project_checkpoints", json::object()),
        tool_call(24, "project_fit_canvas", {{"margin_mm", 2}}),
        tool_call(25, "project_diff", {{"a", "a.osp"}, {"b", "b.osp"}}),
        tool_call(26, "project_validate", json::object()),
        tool_call(27, "project_export", {{"path", "out.dst"}, {"format", "DST"}}),
        tool_call(28, "project_save", {{"path", "p.osp"}}),
        tool_call(29, "project_close", {{"discard_changes", true}}),
    };
}

bool coded(const json& code) {
    return code.is_string() && code.get<std::string>().rfind("E_", 0) == 0 &&
           code.get<std::string>().size() > 2;
}

// Vérifie le contrat sur la réponse (éventuelle) à une trame.
void check_response(const std::optional<std::string>& response, const std::string& frame_text) {
    INFO("trame : " << frame_text.substr(0, 400));
    if (!response) {
        // Seule une notification (sans « id ») peut ne rien produire.
        const json parsed = json::parse(frame_text, nullptr, false);
        CHECK((parsed.is_object() && !parsed.contains("id")));
        return;
    }
    const json r = json::parse(*response, nullptr, /*allow_exceptions=*/false);
    REQUIRE_FALSE(r.is_discarded());
    REQUIRE(r.is_object());
    CHECK(r.at("jsonrpc") == "2.0");
    CHECK(r.contains("id"));
    const bool has_result = r.contains("result");
    const bool has_error = r.contains("error");
    CHECK(has_result != has_error);
    if (has_error) {
        CHECK(r.at("error").at("code").is_number_integer());
        CHECK(r.at("error").at("code").get<int>() < 0);
        CHECK(r.at("error").at("message").is_string());
        CHECK_FALSE(r.at("error").at("message").get<std::string>().empty());
        return;
    }
    const json& result = r.at("result");
    if (result.is_object() && result.contains("isError") && result.at("isError") == true) {
        // Refus d'outil : enveloppe d'erreur stable, code E_*.
        REQUIRE(result.at("content").is_array());
        REQUIRE_FALSE(result.at("content").empty());
        const json body =
            json::parse(result.at("content").at(0).value("text", std::string{}), nullptr, false);
        // Un lot refusé (project_apply) rend un rapport openstitch.batch-report/1 : son code est
        // dans error.code ou dans une opération fautive.
        if (body.is_object() && body.contains("schema") &&
            body.at("schema") == "openstitch.batch-report/1") {
            bool any = false;
            if (body.contains("error") && body["error"].is_object()) {
                any = any || coded(body["error"].value("code", json{}));
            }
            if (body.contains("outcomes")) {
                for (const auto& o : body["outcomes"]) {
                    if (o.contains("error") && o["error"].is_object()) {
                        any = any || coded(o["error"].value("code", json{}));
                    }
                }
            }
            CHECK(any);
        } else if (body.is_object() && body.contains("error") && body["error"].is_object()) {
            CHECK(coded(body["error"].value("code", json{})));
        } else {
            // Autres résultats d'erreur textuels (guide…) : au moins un texte non vide.
            CHECK_FALSE(result.at("content").at(0).value("text", std::string{}).empty());
        }
    }
}

void drive(Server& server, const std::string& text) {
    const auto start = std::chrono::steady_clock::now();
    const auto response = server.handle(text);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    INFO("trame : " << text.substr(0, 400));
    CHECK(elapsed < kPerFrameLimit);
    check_response(response, text);
}

} // namespace

TEST_CASE("fuzz mcp: mutations de trames JSON-RPC (graine fixe)", "[mcp][fuzz]") {
    const ScopedCwd cwd("frames");
    const auto seeds = seed_frames();
    std::size_t total = 0;
    std::uint64_t index = 0;
    for (const auto& seed : seeds) {
        openstitch::qa::Rng rng(kBaseSeed + index++);
        Server server;
        // Session de départ : un projet ouvert pour que les outils aillent jusqu'au bout.
        drive(server, tool_call(900, "project_new", {{"width_mm", 60}, {"height_mm", 40}}).dump());
        for (std::size_t i = 0; i < kMutationsPerSeed; ++i) {
            drive(server, openstitch::qa::mutate(seed.dump(), rng));
            ++total;
        }
        // Le serveur reste utilisable après les mutations.
        const auto alive = server.handle(frame(901, "ping").dump());
        REQUIRE(alive.has_value());
        CHECK(json::parse(*alive).contains("result"));
    }
    CHECK(total >= 1000);
}

TEST_CASE("fuzz mcp: trames degenerees refusees proprement", "[mcp][fuzz]") {
    const ScopedCwd cwd("degenerees");
    Server server;
    const std::vector<std::string> inputs = {
        "",
        " ",
        "null",
        "[]",
        "{}",
        "\"x\"",
        "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]",
        R"({"jsonrpc":"2.0","id":1})",
        R"({"jsonrpc":"2.0","id":1,"method":7})",
        R"({"jsonrpc":"1.0","id":1,"method":"ping"})",
        R"({"jsonrpc":"2.0","id":[1],"method":"ping"})",
        R"({"jsonrpc":"2.0","id":{},"method":"ping"})",
        R"({"jsonrpc":"2.0","id":1,"method":"ping","params":[]})",
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":7}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"project_new","arguments":[]}})",
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"project_new","arguments":null}})",
        R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":null}})",
        R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"digitize","arguments":7}})",
        std::string(1000, '['),
        std::string(1000, '{'),
        std::string("\0\0\0", 3),
        "\xFF\xFE\xFD",
    };
    for (const auto& input : inputs) {
        drive(server, input);
    }
}
