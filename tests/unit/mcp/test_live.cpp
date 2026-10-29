// SPDX-License-Identifier: Apache-2.0
// Mode live du serveur MCP (ADR 0001) : politique d'outils, lecture sur copie, mutation avec
// approbation en une étape d'annulation, refus périmé, protocole hôte/relais, découverte.
#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/mcp/live.hpp"
#include "openstitch/mcp/server.hpp"
#include "openstitch/project_ops/live_bridge.hpp"

namespace {

using namespace openstitch;
using json = nlohmann::json;

struct Gate final : mcp::IApprovalGate {
    bool answer{true};
    int asked{0};
    mcp::ApprovalRequest last;
    bool approve(const mcp::ApprovalRequest& request) override {
        ++asked;
        last = request;
        return answer;
    }
};

struct Doc {
    document::Project project;
    commands::UndoStack stack;
    project_ops::LiveBridge bridge{project, stack, "sess"};
    Gate gate;
    mcp::LiveService service{bridge, gate, [] {
                                 mcp::LiveDocumentInfo info;
                                 info.name = "demo.osp";
                                 info.dirty = true;
                                 info.selection = {2};
                                 return info;
                             }};
    ObjectId first;

    Doc() {
        for (int i = 0; i < 2; ++i) {
            document::VectorObject v;
            v.id = project.object_ids.next();
            v.paths = {{geometry::rectangle_path(
                            Vec2um{Micrometers{i * 20'000}, Micrometers{0}},
                            Vec2um{Micrometers{i * 20'000 + 10'000}, Micrometers{10'000}}),
                        {}}};
            project.vector_objects.push_back(v);
            document::EmbroideryObject e;
            e.id = project.object_ids.next();
            e.name = "Broderie";
            e.source_vector = v.id;
            e.rgb = {10, 20, 30};
            e.params = document::TatamiParams{};
            if (i == 0) {
                first = e.id;
            }
            project.embroidery_objects.push_back(e);
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 3> color() const {
        return project.embroidery_objects.front().rgb;
    }
    [[nodiscard]] std::string recolor(bool dry) const {
        return "{\"operations\":[{\"op\":\"set_color\",\"object\":" + std::to_string(first.value) +
               ",\"rgb\":\"#FF0000\"}],\"dry_run\":" + (dry ? "true" : "false") + "}";
    }
};

// Transport en mémoire : relais -> hôte, sans pipe.
struct Loopback final : mcp::ILiveTransport {
    mcp::LiveHost& host;
    bool down{false};
    explicit Loopback(mcp::LiveHost& h) : host(h) {}
    std::optional<std::string> exchange(const std::string& line) override {
        if (down) {
            return std::nullopt;
        }
        auto reply = host.begin(line);
        if (reply.call) {
            reply.call->run();
        }
        return host.finish(reply);
    }
};

json parse(const std::string& s) {
    return json::parse(s);
}

} // namespace

TEST_CASE("live tool schemas distinguish object, objects, crop and operations", "[live][skills]") {
    const json tools = json::parse(mcp::live_tools_json());
    const auto schema = [&](const std::string& name) {
        const auto found = std::find_if(tools.begin(), tools.end(), [&](const json& item) {
            return item.value("name", std::string{}) == name;
        });
        REQUIRE(found != tools.end());
        return found->at("input_schema");
    };
    const json render = schema("project_render");
    CHECK(render["properties"].contains("crop"));
    CHECK_FALSE(render["properties"].contains("object"));
    CHECK_FALSE(render["properties"].contains("objects"));
    const json views = schema("live_object_views");
    CHECK(views["properties"].contains("objects"));
    CHECK_FALSE(views["properties"].contains("object"));
    CHECK(views["required"] == json::array({"objects"}));
    CHECK(schema("project_inspect")["properties"].contains("object"));
    CHECK(schema("project_check")["properties"].contains("object"));
    CHECK(schema("live_preview")["required"] == json::array({"operations"}));
    CHECK(schema("project_apply")["required"] == json::array({"operations"}));
    CHECK(schema("project_select")["required"] == json::array({"where"}));
}

TEST_CASE("live policy classifies tools", "[live]") {
    CHECK(mcp::live_policy("project_inspect") == mcp::LivePolicy::Read);
    CHECK(mcp::live_policy("project_render") == mcp::LivePolicy::Read);
    CHECK(mcp::live_policy("project_apply") == mcp::LivePolicy::Apply);
    for (const char* tool : {"project_open", "project_new", "project_digitize", "project_save",
                             "project_export", "project_restore", "project_checkpoint", "bash"}) {
        CHECK(mcp::live_policy(tool) == mcp::LivePolicy::Denied);
    }
}

TEST_CASE("live read tool runs on a copy and never asks approval", "[live]") {
    Doc d;
    const auto rev = d.bridge.revision();
    const auto out = parse(d.service.call("project_inspect", "{}"));
    CHECK_FALSE(out["isError"].get<bool>());
    CHECK(out["live"]["revision"].get<std::uint64_t>() == rev);
    CHECK_FALSE(out["live"]["applied"].get<bool>());
    CHECK(d.gate.asked == 0);
    CHECK_FALSE(d.stack.canUndo());
}

TEST_CASE("live dry-run apply leaves the document untouched", "[live]") {
    Doc d;
    const auto out = parse(d.service.call("project_apply", d.recolor(true)));
    CHECK_FALSE(out["isError"].get<bool>());
    CHECK(d.gate.asked == 0);
    CHECK(d.color() == std::array<std::uint8_t, 3>{10, 20, 30});
    CHECK_FALSE(d.stack.canUndo());
}

TEST_CASE("live apply needs approval then is one undo step", "[live]") {
    Doc d;
    const auto rev = d.bridge.revision();
    const auto out = parse(d.service.call("project_apply", d.recolor(false)));
    REQUIRE_FALSE(out["isError"].get<bool>());
    CHECK(out["live"]["applied"].get<bool>());
    CHECK(out["live"]["revision"].get<std::uint64_t>() > rev);
    CHECK(d.gate.asked == 1);
    CHECK(d.gate.last.revision == rev);
    CHECK(d.color() == std::array<std::uint8_t, 3>{255, 0, 0});
    CHECK(d.stack.undoNames().size() == 1);
    REQUIRE(d.stack.undo(d.project));
    CHECK(d.color() == std::array<std::uint8_t, 3>{10, 20, 30});
}

TEST_CASE("live apply refused by the user changes nothing", "[live]") {
    Doc d;
    d.gate.answer = false;
    const auto out = parse(d.service.call("project_apply", d.recolor(false)));
    CHECK(out["isError"].get<bool>());
    CHECK(out["content"][0]["text"].get<std::string>().find("E_APPROVAL_DENIED") !=
          std::string::npos);
    CHECK(d.color() == std::array<std::uint8_t, 3>{10, 20, 30});
    CHECK_FALSE(d.stack.canUndo());
}

TEST_CASE("live apply is refused when the document changed during computation", "[live]") {
    Doc d;
    auto pending = d.service.begin("project_apply", d.recolor(false));
    pending->run();
    // Modification manuelle concurrente entre le calcul et la fin.
    REQUIRE(d.bridge
                .commit("sess", d.bridge.revision(),
                        "{\"schema\":\"openstitch.batch/"
                        "1\",\"operations\":[{\"op\":\"set_color\",\"object\":" +
                            std::to_string(d.first.value) + ",\"rgb\":\"#0000FF\"}]}",
                        "manuel", true)
                .ok);
    const auto out = parse(d.service.finish(*pending));
    CHECK(out["isError"].get<bool>());
    CHECK(out["content"][0]["text"].get<std::string>().find("E_STALE_REVISION") !=
          std::string::npos);
    CHECK(d.gate.asked == 0);
    CHECK(d.color() == std::array<std::uint8_t, 3>{0, 0, 255});
}

TEST_CASE("live denied tools never touch the document", "[live]") {
    Doc d;
    const auto out = parse(d.service.call("project_open", "{\"path\":\"x.osp\"}"));
    CHECK(out["isError"].get<bool>());
    CHECK(d.project.embroidery_objects.size() == 2);
    CHECK(parse(d.service.call("bash", "{}"))["isError"].get<bool>());
}

TEST_CASE("live_status reports selection, dirty and revision without paths", "[live]") {
    Doc d;
    const auto out = parse(d.service.call("live_status", "{}"));
    const auto doc = parse(out["content"][0]["text"].get<std::string>());
    CHECK(doc["session_id"] == "sess");
    CHECK(doc["document"]["name"] == "demo.osp");
    CHECK(doc["document"]["dirty"].get<bool>());
    CHECK(doc["selection"] == json::array({2}));
}

TEST_CASE("live host rejects bad token, version and malformed lines", "[live]") {
    Doc d;
    mcp::LiveHost host(d.service, "secret");
    auto bad = host.begin("pas du json");
    CHECK(parse(bad.immediate)["ok"] == false);
    auto wrong = host.begin("{\"v\":1,\"id\":1,\"token\":\"nope\",\"op\":\"ping\"}");
    CHECK(parse(wrong.immediate)["error"]["code"] == "E_UNAUTHORIZED");
    auto old = host.begin("{\"v\":9,\"id\":1,\"token\":\"secret\",\"op\":\"ping\"}");
    CHECK(parse(old.immediate)["error"]["code"] == "E_UNSUPPORTED_VERSION");
    auto ok = host.begin("{\"v\":1,\"id\":7,\"token\":\"secret\",\"op\":\"attach\"}");
    const auto attach = parse(ok.immediate);
    CHECK(attach["ok"].get<bool>());
    CHECK(attach["id"] == 7);
    CHECK(attach["result"]["session_id"] == "sess");
    auto unknown = host.begin("{\"v\":1,\"id\":1,\"token\":\"secret\",\"op\":\"rm\"}");
    CHECK(parse(unknown.immediate)["error"]["code"] == "E_USAGE");
}

TEST_CASE("live relay round trip and host-down error", "[live]") {
    Doc d;
    mcp::LiveHost host(d.service, "secret");
    Loopback transport(host);
    mcp::LiveRelay relay(transport, [] { return std::string("secret"); });
    CHECK_FALSE(relay.attach().has_value());

    const auto ok = parse(relay.call("project_apply", d.recolor(false)));
    CHECK_FALSE(ok["isError"].get<bool>());
    CHECK(d.color() == std::array<std::uint8_t, 3>{255, 0, 0});

    transport.down = true;
    const auto closed = parse(relay.call("project_inspect", "{}"));
    CHECK(closed["isError"].get<bool>());
    CHECK(closed["content"][0]["text"].get<std::string>().find("E_SESSION_CLOSED") !=
          std::string::npos);
    CHECK(relay.attach().has_value());
}

TEST_CASE("live relay with a wrong token is refused actionably", "[live]") {
    Doc d;
    mcp::LiveHost host(d.service, "secret");
    Loopback transport(host);
    mcp::LiveRelay relay(transport, [] { return std::string("faux"); });
    const auto out = parse(relay.call("project_inspect", "{}"));
    CHECK(out["isError"].get<bool>());
    CHECK(d.gate.asked == 0);
}

TEST_CASE("live mode server lists only allowed tools plus live_status", "[live]") {
    Doc d;
    mcp::LiveHost host(d.service, "secret");
    Loopback transport(host);
    mcp::LiveRelay relay(transport, [] { return std::string("secret"); });
    mcp::Server live(&relay);
    const auto reply = live.handle("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
    REQUIRE(reply.has_value());
    std::set<std::string> names;
    const json listing = parse(*reply); // pas de temporaire dans la boucle (référence pendante)
    for (const auto& t : listing["result"]["tools"]) {
        names.insert(t["name"].get<std::string>());
    }
    CHECK(names.contains("project_inspect"));
    CHECK(names.contains("project_apply"));
    CHECK(names.contains("live_status"));
    CHECK_FALSE(names.contains("project_open"));
    CHECK_FALSE(names.contains("project_export"));

    const auto call =
        live.handle("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":"
                    "{\"name\":\"live_status\",\"arguments\":{}}}");
    REQUIRE(call.has_value());
    CHECK_FALSE(parse(*call)["result"]["isError"].get<bool>());
}

TEST_CASE("live discovery files round trip and pick the requested pid", "[live]") {
    const auto dir = std::filesystem::temp_directory_path() / "openstitch-live-test";
    std::filesystem::remove_all(dir);
    mcp::LiveEndpoint a{111, "pipe-a", "tok-a"};
    mcp::LiveEndpoint b{222, "pipe-b", "tok-b"};
    REQUIRE(mcp::write_live_endpoint(dir, a));
    REQUIRE(mcp::write_live_endpoint(dir, b));
    const auto only_a = mcp::find_live_endpoint(dir, 111);
    REQUIRE(only_a.has_value());
    CHECK(only_a->endpoint == "pipe-a");
    CHECK(only_a->token == "tok-a");
    REQUIRE(mcp::find_live_endpoint(dir).has_value());
    mcp::remove_live_endpoint(dir, 111);
    mcp::remove_live_endpoint(dir, 222);
    CHECK_FALSE(mcp::find_live_endpoint(dir).has_value());
    std::filesystem::remove_all(dir);
    CHECK(mcp::make_live_token().size() == 64);
    CHECK(mcp::make_live_token() != mcp::make_live_token());
}

TEST_CASE("live host refuses oversized requests", "[live]") {
    Doc d;
    mcp::LiveHost host(d.service, "secret");
    const std::string huge(mcp::kLiveMaxRequestBytes + 1, 'x');
    const auto reply = host.begin(huge);
    CHECK(parse(reply.immediate)["ok"] == false);
    CHECK(parse(reply.immediate)["error"]["code"] == "E_USAGE");
}

TEST_CASE("two concurrent clients see consistent revisions and only one commit wins", "[live]") {
    Doc d;
    mcp::LiveHost host(d.service, "secret");
    Loopback t1(host);
    Loopback t2(host);
    mcp::LiveRelay a(t1, [] { return std::string("secret"); });
    mcp::LiveRelay b(t2, [] { return std::string("secret"); });
    // Les deux lisent la meme revision, calculent, puis tentent d'appliquer : la 2e est perimee.
    auto first = d.service.begin("project_apply", d.recolor(false));
    auto second = d.service.begin("project_apply", d.recolor(false));
    first->run();
    second->run();
    const auto ra = parse(d.service.finish(*first));
    const auto rb = parse(d.service.finish(*second));
    CHECK_FALSE(ra["isError"].get<bool>());
    CHECK(rb["isError"].get<bool>());
    CHECK(rb["content"][0]["text"].get<std::string>().find("E_STALE_REVISION") !=
          std::string::npos);
    CHECK(d.stack.undoNames().size() == 1);
    // Les clients ordinaires de lecture continuent de fonctionner.
    CHECK_FALSE(parse(a.call("project_inspect", "{}"))["isError"].get<bool>());
    CHECK_FALSE(parse(b.call("live_status", "{}"))["isError"].get<bool>());
}

TEST_CASE("app closing or document replaced mid-call never applies the change", "[live]") {
    {
        Doc d;
        auto pending = d.service.begin("project_apply", d.recolor(false));
        pending->run();
        d.bridge.close(); // l'application se ferme pendant le calcul
        const auto reply = parse(d.service.finish(*pending));
        CHECK(reply["isError"].get<bool>());
        CHECK(d.color() == std::array<std::uint8_t, 3>{10, 20, 30});
        CHECK(d.gate.asked == 0);
    }
    {
        Doc d;
        auto pending = d.service.begin("project_apply", d.recolor(false));
        pending->run();
        d.bridge.reset("autre-document"); // nouveau document pendant le calcul
        const auto reply = parse(d.service.finish(*pending));
        CHECK(reply["isError"].get<bool>());
        CHECK(d.color() == std::array<std::uint8_t, 3>{10, 20, 30});
    }
}

TEST_CASE("digitize skill files are embedded with their front matter", "[live][skills]") {
    const auto files = mcp::digitize_skill_files();
    REQUIRE(files.size() >= 5);
    const auto skill = std::find_if(files.begin(), files.end(),
                                    [](const auto& f) { return f.first == "SKILL.md"; });
    REQUIRE(skill != files.end());
    CHECK(skill->second.starts_with("---"));
    CHECK(skill->second.find("name: openstitch-digitize") != std::string::npos);
    std::size_t references = 0;
    for (const auto& [path, content] : files) {
        CHECK_FALSE(content.empty());
        references += path.starts_with("references/") ? 1 : 0;
    }
    CHECK(references >= 4);
}
