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
    CHECK(schema("live_preview")["properties"].contains("objects"));
    CHECK(schema("live_preview")["properties"].contains("crop"));
    CHECK(schema("live_preview")["properties"].contains("px_per_mm"));
    CHECK(schema("project_apply")["required"] == json::array({"operations"}));
    CHECK(schema("project_select")["required"] == json::array({"where"}));
}

TEST_CASE("live policy classifies tools", "[live]") {
    CHECK(mcp::live_policy("project_inspect") == mcp::LivePolicy::Read);
    CHECK(mcp::live_policy("project_render") == mcp::LivePolicy::Read);
    CHECK(mcp::live_policy("project_apply") == mcp::LivePolicy::Apply);
    CHECK(mcp::live_policy("project_digitize") == mcp::LivePolicy::Replace);
    for (const char* tool : {"project_open", "project_save", "project_export"}) {
        CHECK(mcp::live_policy(tool) == mcp::LivePolicy::HostFile);
    }
    for (const char* tool : {"project_new", "project_close", "project_restore",
                             "project_checkpoint", "bash"}) {
        CHECK(mcp::live_policy(tool) == mcp::LivePolicy::Denied);
    }
}

namespace {

std::string error_text(const json& out) {
    return out["content"][0]["text"].get<std::string>();
}

std::filesystem::path scratch_dir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / (std::string("os-live-") + name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string path_arg(const std::filesystem::path& path) {
    return json(path.generic_string()).dump();
}

} // namespace

TEST_CASE("live digitize replaces the open document in one approved undo step", "[live]") {
    Doc d;
    const auto image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "cli" / "logo_small.png";
    const std::string args = "{\"image\":" + path_arg(image) + "}";
    d.gate.answer = false;
    const auto refused = parse(d.service.call("project_digitize", args));
    CHECK(refused["isError"].get<bool>());
    CHECK(error_text(refused).find("E_APPROVAL_DENIED") != std::string::npos);
    CHECK(d.project.embroidery_objects.size() == 2);
    CHECK_FALSE(d.stack.canUndo());

    d.gate.answer = true;
    const auto out = parse(d.service.call("project_digitize", args));
    REQUIRE_FALSE(out["isError"].get<bool>());
    CHECK(out["live"]["applied"].get<bool>());
    CHECK(d.gate.last.tool == "project_digitize");
    CHECK(!d.gate.last.warnings.empty());
    REQUIRE_FALSE(d.project.embroidery_objects.empty());
    CHECK(d.project.embroidery_objects.front().name != "Broderie"); // contenu remplacé
    CHECK(d.stack.undoNames().size() == 1);
    REQUIRE(d.stack.undo(d.project));
    CHECK(d.project.embroidery_objects.size() == 2);
}

TEST_CASE("live digitize refuses to write a file by itself", "[live]") {
    Doc d;
    const auto out =
        parse(d.service.call("project_digitize", "{\"image\":\"x.png\",\"out\":\"y.osp\"}"));
    CHECK(out["isError"].get<bool>());
    CHECK(d.gate.asked == 0);
}

TEST_CASE("live export writes a machine file only after approval", "[live]") {
    Doc d;
    const auto dir = scratch_dir("export");
    const auto target = dir / "motif.dst";
    const std::string args = "{\"path\":" + path_arg(target) + ",\"format\":\"dst\"}";

    d.gate.answer = false;
    const auto refused = parse(d.service.call("project_export", args));
    CHECK(refused["isError"].get<bool>());
    CHECK_FALSE(std::filesystem::exists(target));

    d.gate.answer = true;
    const auto out = parse(d.service.call("project_export", args));
    REQUIRE_FALSE(out["isError"].get<bool>());
    CHECK(std::filesystem::exists(target));
    CHECK(std::filesystem::file_size(target) > 0);
    CHECK_FALSE(std::filesystem::exists(dir / "motif.dst.part"));
    CHECK(d.stack.canUndo() == false); // rien ne change dans le document

    const auto wrong = parse(d.service.call(
        "project_export", "{\"path\":" + path_arg(dir / "motif.pes") + ",\"format\":\"dst\"}"));
    CHECK(wrong["isError"].get<bool>()); // l'extension doit suivre le format
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("live save and open go through the host and need approval", "[live]") {
    Doc d;
    const auto dir = scratch_dir("savefile");
    std::string saved;
    std::string opened;
    mcp::LiveHostActions host;
    host.current_path = [] { return std::string{}; };
    host.save_project = [&saved](const std::string& path, std::string&) {
        saved = path;
        return true;
    };
    host.open_project = [&opened](const std::string& path, std::string&) {
        opened = path;
        return true;
    };
    d.service.set_host_actions(host);

    const auto target = dir / "demo.osp";
    const auto no_path = parse(d.service.call("project_save", "{}"));
    CHECK(no_path["isError"].get<bool>()); // jamais enregistré : « path » requis
    const auto bad_ext =
        parse(d.service.call("project_save", "{\"path\":" + path_arg(dir / "demo.txt") + "}"));
    CHECK(bad_ext["isError"].get<bool>());
    CHECK(d.gate.asked == 0);

    d.gate.answer = false;
    CHECK(parse(d.service.call("project_save", "{\"path\":" + path_arg(target) + "}"))["isError"]
              .get<bool>());
    CHECK(saved.empty());
    d.gate.answer = true;
    CHECK_FALSE(parse(d.service.call("project_save", "{\"path\":" + path_arg(target) + "}"))["isError"]
                    .get<bool>());
    CHECK(saved == target.generic_string());

    // Document modifié : l'ouverture exige discard_changes (Doc::info.dirty = true).
    const auto guarded =
        parse(d.service.call("project_open", "{\"path\":" + path_arg(target) + "}"));
    CHECK(guarded["isError"].get<bool>());
    CHECK(opened.empty());
    // Fichier introuvable : refusé avant toute demande à l'utilisateur.
    const int before = d.gate.asked;
    const auto missing = parse(d.service.call(
        "project_open", "{\"path\":" + path_arg(dir / "absent.osp") + ",\"discard_changes\":true}"));
    CHECK(missing["isError"].get<bool>());
    CHECK(d.gate.asked == before);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
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
    CHECK(names.contains("project_open"));
    CHECK(names.contains("project_save"));
    CHECK(names.contains("project_export"));
    CHECK(names.contains("project_digitize"));
    CHECK_FALSE(names.contains("project_new"));
    CHECK_FALSE(names.contains("project_checkpoint"));

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

TEST_CASE("live permission asks the user for disk reads and remembers the answer", "[live]") {
    Doc d;
    const auto ask = [&d](const std::string& tool, const std::string& path) {
        const json args{{"tool_name", tool}, {"input", {{"file_path", path}}}};
        const auto out = parse(d.service.call("live_permission", args.dump()));
        return json::parse(out["content"][0]["text"].get<std::string>());
    };
    // Outil non lecture : refus d'office, sans déranger l'utilisateur.
    CHECK(ask("Bash", "x")["behavior"] == "deny");
    CHECK(d.gate.asked == 0);
    // Secrets : refus d'office.
    CHECK(ask("Read", "C:/Users/me/.ssh/id_rsa")["behavior"] == "deny");
    CHECK(d.gate.asked == 0);
    // Fichier ordinaire : l'utilisateur décide.
    d.gate.answer = false;
    CHECK(ask("Read", "D:/pack/manifest.json")["behavior"] == "deny");
    CHECK(d.gate.asked == 1);
    CHECK(d.gate.last.tool == "live_permission");
    CHECK(!d.gate.last.warnings.empty());
    d.gate.answer = true;
    const auto allowed = ask("Read", "D:/pack/manifest.json");
    CHECK(allowed["behavior"] == "allow");
    CHECK(allowed["updatedInput"]["file_path"] == "D:/pack/manifest.json");
    CHECK(d.gate.asked == 2);
    // Déjà accordé : plus de question.
    CHECK(ask("Read", "D:/pack/manifest.json")["behavior"] == "allow");
    CHECK(d.gate.asked == 2);
    CHECK_FALSE(d.stack.canUndo()); // aucune modification du document
}

TEST_CASE("live granted folders open reads without asking, except secrets", "[live]") {
    Doc d;
    const auto dir = std::filesystem::temp_directory_path() / "os-live-grant";
    std::filesystem::create_directories(dir / ".ssh");
    CHECK(d.service.grant_read_dir(dir.generic_string()));
    CHECK_FALSE(d.service.grant_read_dir((dir / "absent").generic_string()));
    CHECK_FALSE(d.service.grant_read_dir((dir / ".ssh").generic_string())); // sensible
    const auto ask = [&d](const std::string& path) {
        const json args{{"tool_name", "Read"}, {"input", {{"file_path", path}}}};
        const auto out = parse(d.service.call("live_permission", args.dump()));
        return json::parse(out["content"][0]["text"].get<std::string>())["behavior"];
    };
    CHECK(ask((dir / "notes.md").generic_string()) == "allow");
    CHECK(ask((dir / "sub" / "plan.png").generic_string()) == "allow");
    CHECK(ask((dir / ".ssh" / "id_rsa").generic_string()) == "deny");
    CHECK(d.gate.asked == 0);
    d.gate.answer = false; // hors dossier accordé : l'utilisateur est sollicité
    CHECK(ask("D:/ailleurs/autre.txt") == "deny");
    CHECK(d.gate.asked == 1);
    // « .. » ne sort pas du dossier accordé : l'utilisateur est sollicité.
    CHECK(ask(dir.generic_string() + "/../../secret.txt") == "deny");
    CHECK(d.gate.asked == 2);
    // Résultat MCP trop long enregistré par Claude Code : relu sans question ; le reste de
    // ~/.claude (transcriptions, autres projets) reste soumis à l'utilisateur.
    const std::string proj = "C:/Users/u/.claude/projects/"
                             "C--Users-u-AppData-Local-OpenStitch-Studio-companion-workspace-1";
    CHECK(ask(proj + "/abc/tool-results/mcp-openstitch-project_check-1.txt") == "allow");
    CHECK(d.gate.asked == 2);
    CHECK(ask(proj + "/abc.jsonl") == "deny");
    CHECK(ask("C:/Users/u/.claude/projects/autre/abc/tool-results/x.txt") == "deny");
    CHECK(ask(proj + "/abc/tool-results/../../x.jsonl") == "deny");
    CHECK(d.gate.asked == 5);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
