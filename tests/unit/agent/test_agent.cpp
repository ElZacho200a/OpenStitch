// SPDX-License-Identifier: Apache-2.0
// Agent IA du compagnon : fournisseur Anthropic (faux HTTP), boucle d'agent (faux fournisseur,
// faux outils), politique d'outils, interruption, erreurs, secrets. Aucun réseau, aucune clé.
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openstitch/agent/agent.hpp"

namespace {

using namespace openstitch::agent;
using json = nlohmann::json;

constexpr const char* kKey = "sk-ant-SECRET-1234567890";

struct FakeHttp final : IHttpClient {
    std::deque<HttpResponse> script;
    std::vector<std::string> bodies;
    std::vector<std::vector<HttpHeader>> headers;
    int calls{0};
    HttpResponse post(const std::string&, const std::vector<HttpHeader>& h, const std::string& body,
                      std::chrono::milliseconds, const CancelToken&) override {
        ++calls;
        bodies.push_back(body);
        headers.push_back(h);
        if (script.empty()) {
            return {};
        }
        HttpResponse r = script.front();
        script.pop_front();
        return r;
    }
};

HttpResponse ok_text(const std::string& text) {
    return {200,
            json{{"content", json::array({json{{"type", "text"}, {"text", text}}})},
                 {"stop_reason", "end_turn"},
                 {"usage", {{"input_tokens", 10}, {"output_tokens", 5}}}}
                .dump(),
            "",
            false,
            false,
            0};
}

AnthropicOptions fast_options(int* sleeps = nullptr) {
    AnthropicOptions o;
    o.max_retries = 2;
    o.sleeper = [sleeps](int, const CancelToken&) {
        if (sleeps != nullptr) {
            ++*sleeps;
        }
    };
    return o;
}

ProviderRequest simple_request() {
    ProviderRequest r;
    r.model = "claude-sonnet-5-5";
    r.system = "Tu es le compagnon OpenStitch.";
    ContentBlock text;
    text.text = "Bonjour";
    r.messages.push_back({"user", {text}});
    r.tools.push_back({"project_inspect", "inspecte", "{\"type\":\"object\",\"properties\":{}}"});
    return r;
}

// Fournisseur scripté pour la boucle.
struct ScriptedProvider final : IAgentProvider {
    std::deque<ProviderResult> script;
    std::vector<ProviderRequest> requests;
    [[nodiscard]] std::string name() const override { return "Faux"; }
    [[nodiscard]] ProviderCapabilities capabilities() const override { return {}; }
    ProviderResult complete(const ProviderRequest& request, const CancelToken&) override {
        requests.push_back(request);
        if (script.empty()) {
            ProviderResult r;
            r.error = {code::kNetwork, "plus de réponse scriptée", true, 0};
            return r;
        }
        ProviderResult r = script.front();
        script.pop_front();
        return r;
    }
};

ProviderResult final_text(const std::string& text) {
    ProviderResult r;
    r.ok = true;
    ContentBlock b;
    b.text = text;
    r.response.blocks = {b};
    r.response.stop_reason = "end_turn";
    r.response.usage = {20, 10};
    return r;
}

ProviderResult tool_use(const std::string& id, const std::string& name,
                        const std::string& args = "{}") {
    ProviderResult r;
    r.ok = true;
    ContentBlock b;
    b.kind = ContentBlock::Kind::ToolUse;
    b.id = id;
    b.name = name;
    b.input_json = args;
    r.response.blocks = {b};
    r.response.stop_reason = "tool_use";
    r.response.usage = {20, 10};
    return r;
}

struct FakeHost final : IToolHost {
    std::vector<std::string> called;
    std::string reply{"{\"objets\":2}"};
    bool is_error{false};
    std::vector<ToolSpec> tools() override {
        return {{"project_inspect", "inspecte", "{\"type\":\"object\"}"},
                {"project_palette", "palette", "{\"type\":\"object\"}"},
                {"project_open", "ouvre", "{\"type\":\"object\"}"},
                {"bash", "interdit", "{\"type\":\"object\"}"}};
    }
    ToolHostResult call(const std::string& name, const std::string&) override {
        called.push_back(name);
        ToolHostResult r;
        ContentBlock b;
        b.text = reply;
        r.content = {b};
        r.is_error = is_error;
        return r;
    }
};

struct Recorder final : IAgentObserver {
    std::vector<std::string> texts;
    std::vector<std::string> tool_names;
    std::vector<std::string> refusals;
    Usage last_total;
    void on_text(const std::string& t) override { texts.push_back(t); }
    void on_tool_call(const std::string& name, const std::string&, const ToolHostResult&,
                      const std::string& refusal) override {
        tool_names.push_back(name);
        refusals.push_back(refusal);
    }
    void on_usage(const Usage&, const Usage& total) override { last_total = total; }
};

ToolPolicy read_only() {
    return ToolPolicy({"project_inspect", "project_palette"}, {"bash"});
}

// Chaque tool_use de l'historique doit avoir son tool_result dans le message suivant.
bool history_is_valid(const std::vector<Message>& history) {
    for (std::size_t i = 0; i < history.size(); ++i) {
        std::size_t uses = 0;
        for (const auto& b : history[i].blocks) {
            uses += b.kind == ContentBlock::Kind::ToolUse ? 1 : 0;
        }
        if (uses == 0) {
            continue;
        }
        if (i + 1 >= history.size()) {
            return false;
        }
        std::size_t results = 0;
        for (const auto& b : history[i + 1].blocks) {
            results += b.kind == ContentBlock::Kind::ToolResult ? 1 : 0;
        }
        if (results != uses) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("anthropic request body carries model, system, tools and tool results", "[agent]") {
    FakeHttp http;
    AnthropicProvider provider(http, kKey);
    ProviderRequest req = simple_request();
    ContentBlock use;
    use.kind = ContentBlock::Kind::ToolUse;
    use.id = "t1";
    use.name = "project_inspect";
    use.input_json = "{}";
    req.messages.push_back({"assistant", {use}});
    ContentBlock result;
    result.kind = ContentBlock::Kind::ToolResult;
    result.id = "t1";
    ContentBlock text;
    text.text = "ok";
    result.content = {text};
    req.messages.push_back({"user", {result}});

    const json body = json::parse(provider.build_body(req));
    CHECK(body["model"] == "claude-sonnet-5-5");
    CHECK(body["system"] == "Tu es le compagnon OpenStitch.");
    CHECK(body["tools"][0]["name"] == "project_inspect");
    CHECK(body["tools"][0]["input_schema"]["type"] == "object");
    CHECK(body["messages"][1]["content"][0]["type"] == "tool_use");
    CHECK(body["messages"][2]["content"][0]["type"] == "tool_result");
    CHECK(body["messages"][2]["content"][0]["tool_use_id"] == "t1");
    CHECK(provider.build_body(req).find(kKey) == std::string::npos); // la clé reste dans l'en-tête
}

TEST_CASE("anthropic parses text, tool use and usage", "[agent]") {
    const auto text = AnthropicProvider::parse_response(200, ok_text("salut").body);
    REQUIRE(text.ok);
    CHECK(text.response.blocks.front().text == "salut");
    CHECK(text.response.usage.input_tokens == 10);
    CHECK(text.response.usage.output_tokens == 5);

    const json tool{{"content", json::array({json{{"type", "tool_use"},
                                                  {"id", "x1"},
                                                  {"name", "project_inspect"},
                                                  {"input", {{"detail", "full"}}}}})},
                    {"stop_reason", "tool_use"}};
    const auto parsed = AnthropicProvider::parse_response(200, tool.dump());
    REQUIRE(parsed.ok);
    CHECK(parsed.response.blocks.front().kind == ContentBlock::Kind::ToolUse);
    CHECK(json::parse(parsed.response.blocks.front().input_json)["detail"] == "full");
}

TEST_CASE("anthropic maps HTTP errors to stable actionable codes", "[agent]") {
    CHECK(AnthropicProvider::parse_response(401, "{}").error.code == code::kAuth);
    CHECK_FALSE(AnthropicProvider::parse_response(401, "{}").error.retryable);
    CHECK(AnthropicProvider::parse_response(429, "{}").error.code == code::kQuota);
    CHECK(AnthropicProvider::parse_response(429, "{}").error.retryable);
    CHECK(AnthropicProvider::parse_response(529, "{}").error.retryable);
    const auto bad = AnthropicProvider::parse_response(
        400, json{{"error", {{"message", "max_tokens trop grand"}}}}.dump());
    CHECK(bad.error.code == code::kProvider);
    CHECK(bad.error.message.find("max_tokens trop grand") != std::string::npos);
    CHECK(AnthropicProvider::parse_response(200, "pas du json").error.code == code::kBadResponse);
}

TEST_CASE("anthropic sends the key only in headers and retries a bounded number of times",
          "[agent]") {
    FakeHttp http;
    http.script = {HttpResponse{0, "", "connexion refusée", false, false, 0},
                   HttpResponse{500, "{}", "", false, false, 0}, ok_text("enfin")};
    int sleeps = 0;
    AnthropicProvider provider(http, kKey, fast_options(&sleeps));
    const auto result = provider.complete(simple_request(), CancelToken{});
    REQUIRE(result.ok);
    CHECK(http.calls == 3);
    CHECK(sleeps == 2);
    bool in_header = false;
    for (const auto& h : http.headers.front()) {
        in_header = in_header || (h.name == "x-api-key" && h.value == kKey);
    }
    CHECK(in_header);
    for (const auto& b : http.bodies) {
        CHECK(b.find("SECRET") == std::string::npos);
    }
}

TEST_CASE("anthropic gives up after max_retries and does not retry auth errors", "[agent]") {
    {
        FakeHttp http;
        http.script = {{0, "", "x", false, false, 0},
                       {0, "", "x", false, false, 0},
                       {0, "", "x", false, false, 0},
                       {0, "", "x", false, false, 0}};
        AnthropicProvider provider(http, kKey, fast_options());
        const auto r = provider.complete(simple_request(), CancelToken{});
        CHECK_FALSE(r.ok);
        CHECK(r.error.code == code::kNetwork);
        CHECK(http.calls == 3); // 1 + 2 reprises, jamais plus
    }
    {
        FakeHttp http;
        http.script = {HttpResponse{401, "{}", "", false, false, 0}};
        AnthropicProvider provider(http, kKey, fast_options());
        const auto r = provider.complete(simple_request(), CancelToken{});
        CHECK(r.error.code == code::kAuth);
        CHECK(http.calls == 1);
    }
    {
        FakeHttp http;
        AnthropicProvider provider(http, "", fast_options());
        CHECK(provider.complete(simple_request(), CancelToken{}).error.code == code::kAuth);
        CHECK(http.calls == 0); // pas de clé : aucune requête partie
    }
}

TEST_CASE("anthropic timeout and cancellation are distinct and prompt", "[agent]") {
    FakeHttp http;
    http.script = {HttpResponse{0, "", "", true, false, 0}, HttpResponse{0, "", "", true, false, 0},
                   HttpResponse{0, "", "", true, false, 0}};
    AnthropicProvider provider(http, kKey, fast_options());
    CHECK(provider.complete(simple_request(), CancelToken{}).error.code == code::kTimeout);

    FakeHttp http2;
    AnthropicProvider provider2(http2, kKey, fast_options());
    CancelToken cancel;
    cancel.cancel();
    CHECK(provider2.complete(simple_request(), cancel).error.code == code::kCancelled);
    CHECK(http2.calls == 0);
}

TEST_CASE("agent answers a question with read-only tools and the project stays untouched",
          "[agent]") {
    ScriptedProvider provider;
    provider.script = {tool_use("a", "project_inspect"), tool_use("b", "project_palette"),
                       final_text("Le projet a 2 objets.")};
    FakeHost host;
    Recorder rec;
    std::vector<Message> history;
    AgentRunner runner(provider, host, read_only(), {});
    const auto outcome = runner.run(history, "Combien d'objets ?", rec, CancelToken{});
    CHECK(outcome.status == RunOutcome::Status::Done);
    CHECK(outcome.steps == 3);
    CHECK(host.called == std::vector<std::string>{"project_inspect", "project_palette"});
    CHECK(rec.texts == std::vector<std::string>{"Le projet a 2 objets."});
    CHECK(rec.last_total.input_tokens == 60);
    CHECK(history_is_valid(history));
    // Seuls les outils autorisés sont proposés au modèle.
    for (const auto& req : provider.requests) {
        REQUIRE(req.tools.size() == 2);
        for (const auto& t : req.tools) {
            CHECK((t.name == "project_inspect" || t.name == "project_palette"));
        }
    }
}

TEST_CASE("agent never executes tools outside the allowlist or inside the denylist", "[agent]") {
    ScriptedProvider provider;
    provider.script = {tool_use("a", "bash", "{\"cmd\":\"rm -rf /\"}"),
                       tool_use("b", "project_open", "{\"path\":\"x\"}"), final_text("Refusé.")};
    FakeHost host;
    Recorder rec;
    std::vector<Message> history;
    AgentRunner runner(provider, host, read_only(), {});
    const auto outcome = runner.run(history, "ouvre un fichier", rec, CancelToken{});
    CHECK(outcome.status == RunOutcome::Status::Done);
    CHECK(host.called.empty());
    CHECK(rec.refusals.size() == 2);
    CHECK_FALSE(rec.refusals[0].empty());
    CHECK(history_is_valid(history));
    // Le refus est renvoyé au modèle comme résultat d'erreur.
    const auto& results = history[2].blocks;
    CHECK(results[0].is_error);

    ToolPolicy deny_wins({"project_inspect"}, {"project_inspect"});
    CHECK_FALSE(deny_wins.permits("project_inspect"));
    CHECK_FALSE(ToolPolicy{}.permits("project_inspect"));
}

TEST_CASE("agent stops at the step limit and at the token budget", "[agent]") {
    {
        ScriptedProvider provider;
        for (int i = 0; i < 10; ++i) {
            provider.script.push_back(tool_use("t" + std::to_string(i), "project_inspect"));
        }
        FakeHost host;
        Recorder rec;
        std::vector<Message> history;
        RunnerOptions opts;
        opts.max_steps = 3;
        AgentRunner runner(provider, host, read_only(), opts);
        const auto outcome = runner.run(history, "boucle", rec, CancelToken{});
        CHECK(outcome.status == RunOutcome::Status::StepLimit);
        CHECK(outcome.error.code == code::kStepLimit);
        CHECK(host.called.size() == 3);
        CHECK(history_is_valid(history));
    }
    {
        ScriptedProvider provider;
        provider.script = {tool_use("a", "project_inspect"), final_text("x")};
        FakeHost host;
        Recorder rec;
        std::vector<Message> history;
        RunnerOptions opts;
        opts.token_budget = 20; // dépassé dès la première étape (30 jetons)
        AgentRunner runner(provider, host, read_only(), opts);
        CHECK(runner.run(history, "q", rec, CancelToken{}).status ==
              RunOutcome::Status::BudgetExceeded);
    }
}

TEST_CASE("agent interruption keeps a valid history and stops executing tools", "[agent]") {
    ScriptedProvider provider;
    ProviderResult two = tool_use("a", "project_inspect");
    ContentBlock second;
    second.kind = ContentBlock::Kind::ToolUse;
    second.id = "b";
    second.name = "project_palette";
    second.input_json = "{}";
    two.response.blocks.push_back(second);
    provider.script = {two, final_text("jamais")};

    struct CancelOnFirstTool final : IToolHost {
        FakeHost inner;
        CancelToken token;
        std::vector<ToolSpec> tools() override { return inner.tools(); }
        ToolHostResult call(const std::string& n, const std::string& a) override {
            token.cancel(); // l'utilisateur interrompt pendant le premier outil
            return inner.call(n, a);
        }
    } host;
    Recorder rec;
    std::vector<Message> history;
    AgentRunner runner(provider, host, read_only(), {});
    const auto outcome = runner.run(history, "q", rec, host.token);
    CHECK(outcome.status == RunOutcome::Status::Cancelled);
    CHECK(host.inner.called == std::vector<std::string>{"project_inspect"}); // le 2e n'a pas tourné
    CHECK(history_is_valid(history));
    CHECK(provider.script.size() == 1); // aucune étape supplémentaire demandée au modèle
}

TEST_CASE("agent reports provider failures without losing the conversation", "[agent]") {
    ScriptedProvider provider;
    ProviderResult quota;
    quota.error = {code::kQuota, "limite atteinte", true, 0};
    provider.script = {quota};
    FakeHost host;
    Recorder rec;
    std::vector<Message> history;
    AgentRunner runner(provider, host, read_only(), {});
    const auto outcome = runner.run(history, "bonjour", rec, CancelToken{});
    CHECK(outcome.status == RunOutcome::Status::Failed);
    CHECK(outcome.error.code == code::kQuota);
    REQUIRE(history.size() == 1);
    CHECK(history.front().role == "user");
}

TEST_CASE("agent truncates huge tool results before sending them back", "[agent]") {
    ScriptedProvider provider;
    provider.script = {tool_use("a", "project_inspect"), final_text("ok")};
    FakeHost host;
    host.reply = std::string(500'000, 'x');
    Recorder rec;
    std::vector<Message> history;
    RunnerOptions opts;
    opts.max_result_chars = 1000;
    AgentRunner runner(provider, host, read_only(), opts);
    REQUIRE(runner.run(history, "q", rec, CancelToken{}).status == RunOutcome::Status::Done);
    const std::string sent = history[2].blocks[0].content[0].text;
    CHECK(sent.size() < 1200);
    CHECK(sent.find("tronqué") != std::string::npos);
}

TEST_CASE("secrets are redacted and the memory store round trips", "[agent]") {
    const std::string leaked = std::string("erreur avec ") + kKey + " dans le texte";
    const std::string clean = redact(leaked, {kKey});
    CHECK(clean.find("SECRET") == std::string::npos);
    CHECK(clean.find("sk-ant-S") == std::string::npos);
    CHECK(redact("court", {"abc"}) == "court"); // secret trop court : rien à masquer

    MemorySecretStore store;
    CHECK_FALSE(store.read("anthropic").has_value());
    CHECK(store.write("anthropic", kKey));
    CHECK(store.read("anthropic") == std::string(kKey));
    CHECK(store.remove("anthropic"));
    CHECK_FALSE(store.read("anthropic").has_value());
}

TEST_CASE("an error message from the provider never contains the key", "[agent]") {
    FakeHttp http;
    http.script = {HttpResponse{
        400, json{{"error", {{"message", std::string("mauvaise clé ") + kKey}}}}.dump(), "", false,
        false, 0}};
    AnthropicProvider provider(http, kKey, fast_options());
    const auto r = provider.complete(simple_request(), CancelToken{});
    CHECK_FALSE(r.ok);
    CHECK(r.error.message.find("SECRET") == std::string::npos);
}

TEST_CASE("diagnostic log is off by default and never records content without consent",
          "[agent][log]") {
    DiagnosticLog log;
    log.record("demarrage");
    CHECK(log.size() == 0); // desactive par defaut : rien n'est ecrit

    log.set_enabled(true);
    log.record("demande", "chars=42");
    log.record_content("message", "mon projet secret de rose");
    CHECK(log.size() == 1);
    CHECK(log.dump().find("secret de rose") == std::string::npos);

    log.set_content_consent(true);
    log.record_content("message", "bonjour");
    CHECK(log.dump().find("bonjour") != std::string::npos);
}

TEST_CASE("diagnostic log masks keys and absolute paths", "[agent][log]") {
    DiagnosticLog log;
    log.set_enabled(true);
    log.set_content_consent(true);
    log.add_secret(kKey);
    log.record("erreur",
               std::string("cle ") + kKey +
                   " fichier C:\\Users\\zache\\Documents\\motif.osp et /home/zache/x.osp");
    log.record_content("outil",
                       std::string("{\"path\":\"D:/Projets/rose.osp\",\"k\":\"") + kKey + "\"}");
    const std::string dump = log.dump();
    CHECK(dump.find("SECRET") == std::string::npos);
    CHECK(dump.find("zache") == std::string::npos);
    CHECK(dump.find("motif.osp") == std::string::npos);
    CHECK(dump.find("rose.osp") == std::string::npos);
    CHECK(dump.find("<chemin>") != std::string::npos);
}

TEST_CASE("diagnostic log is bounded", "[agent][log]") {
    DiagnosticLog log(5);
    log.set_enabled(true);
    for (int i = 0; i < 50; ++i) {
        log.record("evt", std::to_string(i));
    }
    CHECK(log.size() == 5);
    CHECK(log.dump().find("evt 49") != std::string::npos);
    CHECK(log.dump().find("evt 0\n") == std::string::npos);
    log.clear();
    CHECK(log.size() == 0);
}

TEST_CASE("claude workspace carries the mcp server, guide, hook and skills", "[agent][workspace]") {
    const auto dir = std::filesystem::temp_directory_path() / "openstitch-ws-test";
    std::filesystem::remove_all(dir);
    REQUIRE(write_claude_workspace(dir.string(),
                                   "C:\\Program Files\\OpenStitch\\mcp\\openstitch-mcp.exe"));
    REQUIRE(write_claude_workspace(dir.string(), "C:\\x\\openstitch-mcp.exe")); // idempotent

    const auto read = [&](const char* rel) {
        std::ifstream in(dir / rel, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const json mcp = json::parse(read(".mcp.json"));
    CHECK(mcp["mcpServers"]["openstitch"]["command"] == "C:\\x\\openstitch-mcp.exe");
    CHECK(mcp["mcpServers"]["openstitch"]["args"] == json::array({"--live"}));
    REQUIRE(write_claude_workspace(dir.string(), "openstitch-mcp.exe", 43210));
    const json scoped = json::parse(read(".mcp.json"));
    CHECK(scoped["mcpServers"]["openstitch"]["args"] == json::array({"--live", "--pid", "43210"}));
    const json settings = json::parse(read(".claude/settings.json"));
    CHECK(settings["hooks"]["SessionStart"][0]["hooks"][0]["type"] == "command");
    CHECK(settings["hooks"]["SessionStart"][0]["hooks"][0]["command"].get<std::string>().find(
              "openstitch-live-guide.md") != std::string::npos);
    CHECK(read("CLAUDE.md").find("live_context") != std::string::npos);
    CHECK(read(".claude/openstitch-live-guide.md") == read("CLAUDE.md"));
    CHECK(
        read(".claude/skills/openstitch-live-satin/SKILL.md").find("name: openstitch-live-satin") !=
        std::string::npos);
    CHECK(read(".claude/skills/openstitch-live-edit/SKILL.md").find("project_apply") !=
          std::string::npos);
    CHECK(read("CLAUDE.md").find("EMPIRIQUE") != std::string::npos);
    CHECK(read("CLAUDE.md").find("analyse sémantique") != std::string::npos);
    CHECK(read(".claude/skills/openstitch-live-semantic/SKILL.md").find("live_object_views") !=
          std::string::npos);
    std::filesystem::remove_all(dir);
}
