// SPDX-License-Identifier: Apache-2.0
// Outil MCP `ai_backends` : diagnostic en lecture seule des backends déclarés par l'utilisateur.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/mcp/server.hpp"

using json = nlohmann::json;
using openstitch::mcp::Server;
namespace fs = std::filesystem;

namespace {

json call_tool(Server& server, const std::string& tool, const json& args = json::object()) {
    const json request = {{"jsonrpc", "2.0"},
                          {"id", 1},
                          {"method", "tools/call"},
                          {"params", {{"name", tool}, {"arguments", args}}}};
    const auto response = server.handle(request.dump());
    REQUIRE(response.has_value());
    return json::parse(*response);
}

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) {
        unsetenv(name);
    } else {
        setenv(name, value.c_str(), 1);
    }
#endif
}

struct EnvGuard {
    ~EnvGuard() { set_env("OPENSTITCH_AI_BACKENDS", ""); }
};

} // namespace

TEST_CASE("mcp ai_backends without settings falls back to classical and launches nothing",
          "[mcp][ai]") {
    EnvGuard guard;
    set_env("OPENSTITCH_AI_BACKENDS", "");
    Server server;
    const auto r = call_tool(server, "ai_backends");
    CHECK_FALSE(r.at("result").at("isError").get<bool>());
    const auto report =
        json::parse(r.at("result").at("content").at(0).at("text").get<std::string>());
    CHECK(report.at("schema") == "openstitch.ai.backends-report/1");
    CHECK(report.at("backends").empty());
    CHECK(report.at("resolution").at("classical_fallback").get<bool>());
}

TEST_CASE("mcp ai_backends refuses an agent-chosen command and an unreadable settings file",
          "[mcp][ai]") {
    EnvGuard guard;
    const fs::path bad = fs::temp_directory_path() / "openstitch-ai-backends-bad.json";
    std::ofstream(bad) << "{ pas du json";
    set_env("OPENSTITCH_AI_BACKENDS", bad.string());
    Server server;
    const auto r = call_tool(server, "ai_backends");
    CHECK(r.at("result").at("isError").get<bool>());
    // un argument « config » ou « command » n'existe pas dans le schéma : il est ignoré
    const auto r2 = call_tool(server, "ai_backends", {{"command", "calc.exe"}});
    CHECK(r2.at("result").at("isError").get<bool>());
    fs::remove(bad);
}

TEST_CASE("mcp ai_backends reports a missing declared backend without crashing", "[mcp][ai]") {
    EnvGuard guard;
    const fs::path cfg = fs::temp_directory_path() / "openstitch-ai-backends-ghost.json";
    std::ofstream(cfg)
        << R"({"schema":"openstitch.ai.backends/1","preferred":"ghost","backends":[{"id":"ghost","command":"openstitch-no-such-interpreter-xyz"}]})";
    set_env("OPENSTITCH_AI_BACKENDS", cfg.string());
    Server server;
    const auto r = call_tool(server, "ai_backends");
    CHECK_FALSE(r.at("result").at("isError").get<bool>());
    const auto report =
        json::parse(r.at("result").at("content").at(0).at("text").get<std::string>());
    REQUIRE(report.at("backends").size() == 1);
    CHECK(report.at("backends").at(0).at("state") == "failed");
    CHECK(report.at("resolution").at("classical_fallback").get<bool>());
    fs::remove(cfg);
}
