// SPDX-License-Identifier: Apache-2.0
// Outils MCP : import/remplacement sémantiques, bilan de diagnostics, points de contrôle,
// comparaison et benchmark des satins.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "openstitch/mcp/server.hpp"

using json = nlohmann::json;
using openstitch::mcp::Server;
namespace fs = std::filesystem;

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

bool is_error(const json& response) {
    return response.at("result").at("isError").get<bool>();
}

json text_of(const json& response) {
    for (const auto& block : response.at("result").at("content")) {
        if (block.at("type") == "text") {
            return json::parse(block.at("text").get<std::string>());
        }
    }
    throw std::runtime_error("aucun bloc texte");
}

std::size_t images_in(const json& response) {
    std::size_t n = 0;
    for (const auto& block : response.at("result").at("content")) {
        n += block.at("type") == "image" ? 1U : 0U;
    }
    return n;
}

struct Workdir {
    fs::path dir;
    explicit Workdir(const std::string& name) {
        dir = fs::temp_directory_path() /
              ("openstitch_mcp_" + name + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
    }
    ~Workdir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    Workdir(const Workdir&) = delete;
    Workdir& operator=(const Workdir&) = delete;
};

std::string u8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

struct Rect {
    int x0, y0, x1, y1;
};

constexpr int kW = 200;
constexpr int kH = 100;

void write_mask(const fs::path& file, const Rect& r) {
    openstitch::image::Image img;
    img.width = kW;
    img.height = kH;
    img.rgba.assign(static_cast<std::size_t>(kW) * kH * 4U, 0);
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i + 3] = 255;
    }
    for (int y = r.y0; y <= r.y1; ++y) {
        for (int x = r.x0; x <= r.x1; ++x) {
            auto* px = img.rgba.data() + 4U * (static_cast<std::size_t>(y) * kW + x);
            px[0] = px[1] = px[2] = 255;
        }
    }
    const auto bytes = openstitch::image::encode_png(img);
    REQUIRE(bytes.has_value());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes->data()),
              static_cast<std::streamsize>(bytes->size()));
}

// Manifeste de deux objets de MÊME couleur de fil.
fs::path write_scene(const fs::path& dir) {
    fs::create_directories(dir / "masks");
    write_mask(dir / "masks" / "a.png", {20, 20, 80, 80});
    write_mask(dir / "masks" / "b.png", {120, 20, 180, 80});
    const auto region = [](const char* sid, const char* mask) {
        return json{
            {"semantic_id", sid},      {"family", "scene"},         {"role", "test"},
            {"priority", "important"}, {"stitch_intent", "tatami"}, {"thread_rgb", "#FF0000"},
            {"mask_path", mask}};
    };
    const json doc{{"schema", "openstitch.semantic/1"},
                   {"source_image", {{"path", "s.png"}, {"width_px", kW}, {"height_px", kH}}},
                   {"design", {{"width_mm", 100}, {"height_mm", 50}}},
                   {"regions", json::array({region("voile.a", "masks/a.png"),
                                            region("voile.b", "masks/b.png")})}};
    const fs::path file = dir / "carte.json";
    std::ofstream(file) << doc.dump(2);
    return file;
}

json shape(double x, double y, double w, double h, const char* as = nullptr) {
    json op{{"op", "add_shape"}, {"shape", "rect"},     {"width_mm", w},
            {"height_mm", h},    {"center_mm", {x, y}}, {"stitch_type", "tatami"}};
    if (as != nullptr) {
        op["as"] = as;
    }
    return op;
}

} // namespace

TEST_CASE("mcp tools/list expose les outils semantiques, diagnostics et variantes", "[mcp][pr]") {
    Server server;
    const auto r = rpc(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    std::vector<std::string> names;
    for (const auto& t : r.at("result").at("tools")) {
        names.push_back(t.at("name").get<std::string>());
        CHECK(t.at("inputSchema").at("type") == "object");
    }
    for (const char* wanted :
         {"semantic_validate", "semantic_import", "semantic_replace", "semantic_map",
          "project_report", "project_diagnose", "project_density", "project_fidelity",
          "project_fabric_risk", "project_locate", "project_checkpoint", "project_restore",
          "project_checkpoints", "project_compare", "project_satin_compare"}) {
        CAPTURE(wanted);
        CHECK(std::find(names.begin(), names.end(), wanted) != names.end());
    }
}

TEST_CASE("mcp semantic: valider, importer, retrouver par identifiant stable", "[mcp][pr]") {
    Workdir w("semantic");
    const auto manifest = u8(write_scene(w.dir));
    Server server;
    call(server, "project_new", {{"width_mm", 120}, {"height_mm", 70}});

    const auto valid = call(server, "semantic_validate", {{"manifest", manifest}});
    CHECK_FALSE(is_error(valid));
    CHECK(text_of(valid).at("ok") == true);

    const auto dry = call(server, "semantic_import", {{"manifest", manifest}, {"dry_run", true}});
    CHECK_FALSE(is_error(dry));
    CHECK(text_of(call(server, "semantic_map")).at("objects").empty());

    const auto imported = call(server, "semantic_import", {{"manifest", manifest}});
    REQUIRE_FALSE(is_error(imported));
    const auto map = text_of(call(server, "semantic_map"));
    REQUIRE(map.at("objects").contains("voile.a"));
    REQUIRE(map.at("objects").contains("voile.b"));
    CHECK(map.at("objects").at("voile.a").at("ids")[0] !=
          map.at("objects").at("voile.b").at("ids")[0]);

    // Réimporter les mêmes identifiants est refusé sans toucher au projet.
    const auto again = call(server, "semantic_import", {{"manifest", manifest}});
    CHECK(is_error(again));
    CHECK(text_of(again).at("outcomes")[0].at("error").at("code") == "E_REFUSED");
    CHECK(text_of(call(server, "semantic_map")) == map);
}

TEST_CASE("mcp semantic_replace: l'autre objet ne bouge pas, retour arriere sur refus",
          "[mcp][pr]") {
    Workdir w("replace");
    const auto manifest = u8(write_scene(w.dir));
    Server server;
    call(server, "project_new", {{"width_mm", 120}, {"height_mm", 70}});
    REQUIRE_FALSE(is_error(call(server, "semantic_import", {{"manifest", manifest}})));
    const auto map_before = text_of(call(server, "semantic_map"));
    const auto b_id = map_before.at("objects").at("voile.b").at("ids")[0].get<std::uint64_t>();
    const auto b_before =
        text_of(call(server, "project_inspect", {{"object", b_id}, {"geometry", true}}));

    write_mask(w.dir / "masks" / "a2.png", {10, 10, 95, 90});
    const auto replaced =
        call(server, "semantic_replace",
             {{"manifest", manifest}, {"semantic_id", "voile.a"}, {"mask", "masks/a2.png"}});
    REQUIRE_FALSE(is_error(replaced));
    const auto map_after = text_of(call(server, "semantic_map"));
    CHECK(map_after.at("objects").at("voile.a").at("ids") !=
          map_before.at("objects").at("voile.a").at("ids"));
    CHECK(map_after.at("objects").at("voile.b") == map_before.at("objects").at("voile.b"));
    CHECK(text_of(call(server, "project_inspect", {{"object", b_id}, {"geometry", true}})) ==
          b_before);

    const auto refused =
        call(server, "semantic_replace", {{"manifest", manifest}, {"semantic_id", "inconnu"}});
    CHECK(is_error(refused));
    CHECK(text_of(call(server, "semantic_map")) == map_after);
}

TEST_CASE("mcp project_report: statuts explicites et fidelite non supportee sans image",
          "[mcp][pr]") {
    Server server;
    call(server, "project_new", {{"width_mm", 60}, {"height_mm", 40}});
    call(server, "project_apply", {{"operations", json::array({shape(0, 0, 20, 10)})}});

    const auto report = text_of(call(server, "project_report"));
    CHECK(report.at("schema") == "openstitch.mcp-report/1");
    const auto& analyses = report.at("analyses");
    for (const char* name : {"check", "diagnose", "density", "fidelity", "fabric_risk"}) {
        CAPTURE(name);
        REQUIRE(analyses.contains(name));
        const auto status = analyses.at(name).at("status").get<std::string>();
        CHECK((status == "pass" || status == "warning" || status == "error" ||
               status == "not_run" || status == "unsupported"));
    }
    CHECK(analyses.at("fidelity").at("status") == "unsupported");
    CHECK(analyses.at("fidelity").at("reason").get<std::string>().find("image source") !=
          std::string::npos);
    CHECK(report.at("totals").at("stitches").get<int>() > 0);
    CHECK(report.at("totals").at("width_mm").get<double>() > 15.0);

    const auto only = text_of(call(server, "project_report", {{"only", json::array({"check"})}}));
    CHECK(only.at("analyses").at("density").at("status") == "not_run");
    CHECK(only.at("analyses").at("check").at("status") != "not_run");
    CHECK(is_error(call(server, "project_report", {{"only", json::array({"zzz"})}})));

    // Outils unitaires : mêmes constats que le bilan, fidélité refusée proprement.
    CHECK_FALSE(is_error(call(server, "project_diagnose")));
    CHECK_FALSE(is_error(call(server, "project_density", {{"heatmap", true}})));
    const auto fid = call(server, "project_fidelity");
    CHECK_FALSE(is_error(fid));
    CHECK(text_of(fid).at("status") == "unsupported");
    CHECK_FALSE(is_error(call(server, "project_fabric_risk", {{"fabric", "jersey"}})));
    CHECK(is_error(call(server, "project_fabric_risk", {{"fabric", "soie"}})));
}

TEST_CASE("mcp project_locate retrouve l'objet qui a cousu un point", "[mcp][pr]") {
    Server server;
    call(server, "project_new", {{"width_mm", 60}, {"height_mm", 40}});
    call(server, "project_apply", {{"operations", json::array({shape(0, 0, 20, 10)})}});
    const auto found =
        text_of(call(server, "project_locate", {{"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 1}}));
    CHECK(found.dump().find("objects") != std::string::npos);
    CHECK(is_error(call(server, "project_locate", {{"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 0}})));
}

TEST_CASE("mcp checkpoints: variantes, restauration sure, comparaison a meme echelle",
          "[mcp][pr]") {
    Workdir w("checkpoints");
    Server server;
    call(server, "project_new", {{"width_mm", 80}, {"height_mm", 60}});
    call(server, "project_apply", {{"operations", json::array({shape(-15, 0, 20, 20)})}});

    CHECK_FALSE(is_error(call(server, "project_checkpoint", {{"name", "base"}})));
    CHECK(is_error(
        call(server, "project_checkpoint", {{"name", "base"}}))); // pas d'écrasement implicite
    CHECK_FALSE(
        is_error(call(server, "project_checkpoint", {{"name", "base"}, {"overwrite", true}})));
    CHECK(is_error(call(server, "project_checkpoint", {{"name", "a/b"}})));
    CHECK(is_error(call(server, "project_checkpoint", {{"name", "current"}})));

    const fs::path file = w.dir / "variante.osp";
    CHECK_FALSE(
        is_error(call(server, "project_checkpoint", {{"name", "disque"}, {"path", u8(file)}})));
    CHECK(fs::exists(file));
    CHECK(is_error(call(server, "project_checkpoint", {{"name", "disque2"}, {"path", u8(file)}})));

    call(server, "project_apply", {{"operations", json::array({shape(15, 0, 20, 20)})}});
    REQUIRE(text_of(call(server, "project_inspect")).at("embroidery_objects") == 2);

    const auto compare = call(server, "project_compare", {{"b", "base"}});
    REQUIRE_FALSE(is_error(compare));
    const auto doc = text_of(compare);
    CHECK(doc.at("delta_b_minus_a").at("stitches").get<double>() < 0.0); // base a moins de points
    CHECK(images_in(compare) == 2);
    CHECK(is_error(call(server, "project_compare", {{"b", "inconnue"}})));

    const auto restored = call(server, "project_restore", {{"name", "base"}});
    REQUIRE_FALSE(is_error(restored));
    CHECK(text_of(call(server, "project_inspect")).at("embroidery_objects") == 1);
    // L'état d'avant la restauration est gardé : on peut y revenir.
    const auto list = text_of(call(server, "project_checkpoints"));
    CHECK(list.dump().find("_avant_restauration") != std::string::npos);
    REQUIRE_FALSE(is_error(call(server, "project_restore", {{"name", "_avant_restauration"}})));
    CHECK(text_of(call(server, "project_inspect")).at("embroidery_objects") == 2);
    CHECK(is_error(call(server, "project_restore", {{"name", "zzz"}})));
}

TEST_CASE("mcp project_satin_compare: trois strategies isolees, session intacte", "[mcp][pr]") {
    Server server;
    call(server, "project_new", {{"width_mm", 100}, {"height_mm", 40}});
    call(server, "project_apply", {{"operations", json::array({shape(0, 0, 60, 6, "bande")})}});
    const auto before = text_of(call(server, "project_inspect"));
    const auto id = before.at("groups").at("tatami").begin().value()[0].get<std::uint64_t>();

    const auto r = call(server, "project_satin_compare", {{"objects", json::array({id})}});
    REQUIRE_FALSE(is_error(r));
    const auto doc = text_of(r);
    REQUIRE(doc.at("objects").size() == 1);
    const auto& trials = doc.at("objects")[0].at("trials");
    REQUIRE(trials.size() == 3);
    for (const auto& t : trials) {
        CAPTURE(t.dump());
        const auto status = t.at("status").get<std::string>();
        CHECK((status == "ok" || status == "no_points" || status == "refused"));
        CHECK(t.contains("rejected_because"));
    }
    CHECK(doc.at("note").get<std::string>().find("aucun choix automatique") != std::string::npos);
    // La session n'a pas été modifiée : l'objet est toujours du tatami.
    CHECK(text_of(call(server, "project_inspect")) == before);

    CHECK(is_error(call(server, "project_satin_compare", {{"objects", json::array({424242})}})));
    CHECK(is_error(call(server, "project_satin_compare", {{"objects", json::array()}})));
    CHECK(is_error(call(server, "project_satin_compare",
                        {{"objects", json::array({id})}, {"strategies", json::array({"zzz"})}})));
}

TEST_CASE("mcp doctor: auto-test de bout en bout en memoire", "[mcp][pr]") {
    bool ok = false;
    const auto report = json::parse(openstitch::mcp::self_test_json(ok));
    CHECK(ok);
    CHECK(report.at("schema") == "openstitch.mcp-doctor/1");
    CHECK(report.at("version") == openstitch::mcp::kServerVersion);
    REQUIRE(report.at("checks").size() >= 7);
    for (const auto& check : report.at("checks")) {
        CAPTURE(check.dump());
        CHECK(check.at("ok") == true);
    }
}
