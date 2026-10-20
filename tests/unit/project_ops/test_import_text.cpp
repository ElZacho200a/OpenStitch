// SPDX-License-Identifier: Apache-2.0
// import_svg, lettrage (add_text / set_text / remove_text) et restauration du contour brut
// d'une region d'un projet reellement numerise (auto_satin).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/project_ops/auto_satin_service.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
namespace po = openstitch::project_ops;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path temp_dir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("openstitch_import_text_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string font_path() {
    return (fs::path(OPENSTITCH_FONTS_DIR) / "VeraBd.ttf").string();
}

std::string escaped(const std::string& s) {
    return json(s).dump();
}

document::Project empty_project() {
    auto created = po::create_empty_project({});
    REQUIRE(created);
    return std::move(*created);
}

double area_mm2(const document::VectorObject& v) {
    double um2 = 0.0;
    for (const auto& set : v.paths) {
        um2 += geometry::path_set_area_um2(set);
    }
    return um2 / 1e6;
}

} // namespace

TEST_CASE("import_svg creates editable objects from a real svg file", "[project_ops][import]") {
    const auto dir = temp_dir("svg");
    const fs::path svg = dir / "dessin.svg";
    {
        std::ofstream out(svg);
        out << R"(<svg xmlns="http://www.w3.org/2000/svg" width="60mm" height="40mm" viewBox="0 0 60 40">)"
               R"(<rect x="5" y="5" width="20" height="12"/>)"
               R"(<circle cx="40" cy="20" r="8"/>)"
               R"(<path d="M10 25 L30 25 L20 36 Z"/>)"
               R"(<text x="1" y="1">non rendu</text></svg>)";
    }
    auto project = empty_project();
    const auto report = po::apply_operation(
        project, R"({"op":"import_svg","as":"s","file":)" + escaped(svg.string()) +
                     R"(,"stitch_type":"tatami","rgb":"#336699"})");
    INFO(report.to_json());
    REQUIRE(report.ok);
    CHECK(project.embroidery_objects.size() == 3);
    CHECK(project.vector_objects.size() == 3);
    CHECK(project.embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{0x33, 0x66, 0x99});
    CHECK(project.embroidery_objects[0].is_tatami());
    CHECK(area_mm2(project.vector_objects[0]) > 200.0);
    // Le <text> n'est pas rendu : il est signale, pas ignore en silence.
    CHECK(report.outcomes[0].result_json.find("text") != std::string::npos);
    CHECK(stitch_generation::effective_sequence(project));

    // Mode auto : meme classification que l'auto-numerisation.
    auto other = empty_project();
    REQUIRE(
        po::apply_operation(other, R"({"op":"import_svg","file":)" + escaped(svg.string()) + "}")
            .ok);
    CHECK(other.embroidery_objects.size() == 3);
}

TEST_CASE("import_svg refuses missing, empty and invalid input with named codes",
          "[project_ops][import]") {
    const auto dir = temp_dir("svg_err");
    auto project = empty_project();
    const auto missing =
        po::apply_operation(project, R"({"op":"import_svg","file":"/pas/de/fichier.svg"})");
    CHECK_FALSE(missing.ok);
    CHECK(missing.to_json().find("E_IO") != std::string::npos);

    const fs::path empty = dir / "vide.svg";
    {
        std::ofstream out(empty);
        out << R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"></svg>)";
    }
    const auto none = po::apply_operation(project, R"({"op":"import_svg","file":)" +
                                                       escaped(empty.string()) + "}");
    CHECK_FALSE(none.ok);
    CHECK(none.to_json().find("E_REFUSED") != std::string::npos);

    const fs::path broken = dir / "casse.svg";
    {
        std::ofstream out(broken);
        out << "<svg><path d=";
    }
    CHECK_FALSE(po::apply_operation(project, R"({"op":"import_svg","file":)" +
                                                 escaped(broken.string()) + "}")
                    .ok);
    const auto badType =
        po::apply_operation(project, R"({"op":"import_svg","stitch_type":"satin","file":)" +
                                         escaped(empty.string()) + "}");
    CHECK(badType.to_json().find("E_OUT_OF_RANGE") != std::string::npos);
    CHECK(project.embroidery_objects.empty());
}

TEST_CASE("add_text builds editable letters and set_text regenerates them", "[project_ops][text]") {
    auto project = empty_project();
    const auto report = po::apply_batch(
        project,
        R"({"schema":"openstitch.batch/1","operations":[)"
        R"({"op":"add_text","as":"t","text":"Hello","font_file":)" +
            escaped(font_path()) +
            R"(,"cap_height_mm":14,"fill":"auto","rgb":"#204080"},)"
            R"({"op":"set_text","text_object":"$t","text":"Hello World","align":"center"}]})");
    INFO(report.to_json());
    REQUIRE(report.ok);
    REQUIRE(project.text_objects.size() == 1);
    CHECK(project.text_objects[0].text == "Hello World");
    CHECK(project.text_objects[0].align == document::TextAlign::Center);
    CHECK(project.embroidery_objects.size() >= 10); // une lettre = un objet, espace exclu
    for (const auto& v : project.vector_objects) {
        CHECK(v.text_owner == project.text_objects[0].id);
    }
    CHECK(stitch_generation::effective_sequence(project));

    const auto text = po::inspect_project_json(project);
    REQUIRE(text);
    const json doc = json::parse(*text);
    REQUIRE(doc["text_objects"].size() == 1);
    CHECK(doc["text_objects"][0]["text"] == "Hello World");
    CHECK(doc["text_objects"][0]["letter_vector_ids"].size() == project.vector_objects.size());

    const auto removed =
        po::apply_operation(project, R"({"op":"remove_text","text_object":)" +
                                         std::to_string(project.text_objects[0].id.value) + "}");
    REQUIRE(removed.ok);
    CHECK(project.text_objects.empty());
    CHECK(project.embroidery_objects.empty());
    CHECK(project.vector_objects.empty());
}

TEST_CASE("set_text keeps the visible position of moved letters", "[project_ops][text]") {
    auto project = empty_project();
    REQUIRE(po::apply_operation(project, R"({"op":"add_text","text":"AB","font_file":)" +
                                             escaped(font_path()) + "}")
                .ok);
    const auto textId = project.text_objects[0].id.value;
    const auto firstEmb = project.embroidery_objects[0].id.value;
    REQUIRE(po::apply_operation(project, R"({"op":"translate","object":)" +
                                             std::to_string(firstEmb) + R"(,"dx_mm":20,"dy_mm":5})")
                .ok);
    // Toutes les lettres ne bougent pas : on deplace le texte entier lettre par lettre.
    for (std::size_t i = 1; i < project.embroidery_objects.size(); ++i) {
        REQUIRE(po::apply_operation(project,
                                    R"({"op":"translate","object":)" +
                                        std::to_string(project.embroidery_objects[i].id.value) +
                                        R"(,"dx_mm":20,"dy_mm":5})")
                    .ok);
    }
    REQUIRE(po::apply_operation(project, R"({"op":"set_text","text_object":)" +
                                             std::to_string(textId) + R"(,"text":"AB"})")
                .ok);
    const auto text = po::inspect_project_json(project);
    const json doc = json::parse(*text);
    // origine d'edition = origine memorisee + deplacement visible (20 ; 5)
    CHECK(doc["text_objects"][0]["origin_mm"][0].get<double>() > 19.0);
    CHECK(doc["text_objects"][0]["origin_mm"][1].get<double>() > 4.0);
}

TEST_CASE("text operations refuse bad fonts and bad arguments", "[project_ops][text]") {
    auto project = empty_project();
    const auto noFont = po::apply_operation(project, R"({"op":"add_text","text":"A"})");
    CHECK_FALSE(noFont.ok); // font_file est obligatoire
    const auto absent = po::apply_operation(
        project, R"({"op":"add_text","text":"A","font_file":"/pas/de/police.ttf"})");
    CHECK(absent.to_json().find("E_IO") != std::string::npos);
    const fs::path fake = temp_dir("font") / "fausse.ttf";
    {
        std::ofstream out(fake, std::ios::binary);
        out << "ceci n'est pas une police";
    }
    const auto notFont = po::apply_operation(
        project, R"({"op":"add_text","text":"A","font_file":)" + escaped(fake.string()) + "}");
    CHECK(notFont.to_json().find("E_REFUSED") != std::string::npos);
    const auto tooBig = po::apply_operation(
        project, R"({"op":"add_text","text":"A","cap_height_mm":9999,"font_file":)" +
                     escaped(font_path()) + "}");
    CHECK(tooBig.to_json().find("E_OUT_OF_RANGE") != std::string::npos);
    const auto unknown =
        po::apply_operation(project, R"({"op":"set_text","text_object":999,"text":"x"})");
    CHECK(unknown.to_json().find("E_UNKNOWN_OBJECT") != std::string::npos);
    CHECK(project.text_objects.empty());
}

TEST_CASE("auto_satin keeps an edited contour instead of restoring the raw region one",
          "[project_ops][pristine]") {
    auto project = empty_project();
    const auto digitized = po::digitize_image_into(
        project, fs::path(OPENSTITCH_FIXTURES_DIR) / "cli" / "logo_small.png", {});
    REQUIRE(digitized);
    REQUIRE(project.segmentation.has_value());
    // Copies : l'application d'une operation remplace le contenu du projet.
    const ObjectId embId = project.embroidery_objects.front().id;
    const ObjectId vectorId = project.embroidery_objects.front().source_vector;
    REQUIRE(project.findObject(vectorId)->source_region.has_value());

    // Contour courant identique a la region : rien a restaurer.
    const auto plain = po::plan_auto_satin(project, vectorId, {.use_pristine_contour = true});
    REQUIRE(plain);
    CHECK_FALSE(plain->restored_contour.has_value());

    // L'utilisateur agrandit la forme de 50 % (retouche, fusion...) : ce n'est PAS le recouvrement
    // des tatamis voisins, le contour est voulu et ne doit jamais redevenir celui de la region
    // d'origine (defaut : apres fusion de deux zones, le satin revenait a la forme premiere).
    const std::string id = std::to_string(embId.value);
    REQUIRE(
        po::apply_operation(project, R"({"op":"scale","object":)" + id + R"(,"factor":1.5})").ok);
    const double enlarged = area_mm2(*project.findObject(vectorId));
    const auto plan = po::plan_auto_satin(project, vectorId, {.compact_axis = true});
    REQUIRE(plan);
    CHECK_FALSE(plan->restored_contour.has_value());

    // Via l'operation : si la conversion aboutit, la forme retouchee est conservee.
    const auto report = po::apply_operation(
        project, R"({"op":"auto_satin","compact_axis":true,"object":)" + id + "}");
    INFO(report.to_json());
    CHECK(area_mm2(*project.findObject(vectorId)) == enlarged);
}
