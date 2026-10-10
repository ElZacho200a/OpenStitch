// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/text_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "test_support.hpp"

using namespace lettering_test;
namespace cmd = openstitch::commands;
namespace fs = std::filesystem;

namespace {

// Crée (ou remplace) le texte dans le projet via la pile, comme le fait le desktop.
void set_text(document::Project& project, cmd::UndoStack& stack, document::TextObject t,
              const char* label = "Texte") {
    auto built = lettering::build_text_objects(vera_bold(), t, project.object_ids);
    REQUIRE(built.has_value());
    stack.execute(
        std::make_unique<cmd::SetTextObjectCommand>(std::move(t), std::move(built->vectors),
                                                    std::move(built->embroideries), label),
        project);
}

document::TextObject new_text(document::Project& project, const std::string& s) {
    auto t = make_text(s, 10'000);
    t.id = project.object_ids.next();
    return t;
}

} // namespace

TEST_CASE("create, edit and undo a text in single steps") {
    document::Project project;
    cmd::UndoStack stack;
    stack.setMergeWindow(std::chrono::milliseconds{0});

    // Un objet vectoriel étranger doit survivre à tout.
    document::VectorObject other;
    other.id = project.object_ids.next();
    project.vector_objects.push_back(other);

    auto t = new_text(project, "ABC");
    set_text(project, stack, t);
    REQUIRE(project.text_objects.size() == 1);
    CHECK(project.vector_objects.size() == 1 + 3);
    CHECK(project.embroidery_objects.size() == 3);
    const auto firstEmb = project.embroidery_objects.front().id;

    // Édition du texte : les lettres sont remplacées, en une seule annulation.
    t.text = "ABCDE";
    t.rgb = {200, 0, 0};
    set_text(project, stack, t, "Modifier le texte");
    CHECK(project.text_objects.size() == 1);
    CHECK(project.text_objects[0].text == "ABCDE");
    CHECK(project.embroidery_objects.size() == 5);
    CHECK(project.embroidery_objects.front().id != firstEmb);
    CHECK(project.embroidery_objects.front().rgb == t.rgb);
    CHECK(stack.undoName() == "Modifier le texte");

    REQUIRE(stack.undo(project));
    CHECK(project.text_objects[0].text == "ABC");
    CHECK(project.embroidery_objects.size() == 3);
    CHECK(project.embroidery_objects.front().id == firstEmb);
    CHECK(project.vector_objects.size() == 4);

    REQUIRE(stack.undo(project));
    CHECK(project.text_objects.empty());
    CHECK(project.embroidery_objects.empty());
    REQUIRE(project.vector_objects.size() == 1);
    CHECK(project.vector_objects[0].id == other.id);

    REQUIRE(stack.redo(project));
    REQUIRE(stack.redo(project));
    CHECK(project.text_objects[0].text == "ABCDE");
    CHECK(project.embroidery_objects.size() == 5);
}

TEST_CASE("editing keeps the stitching order position of the letters") {
    document::Project project;
    cmd::UndoStack stack;
    stack.setMergeWindow(std::chrono::milliseconds{0});

    auto t = new_text(project, "AB");
    set_text(project, stack, t);
    // Un autre objet de broderie ajouté APRES le texte.
    document::EmbroideryObject tail;
    tail.id = project.object_ids.next();
    project.embroidery_objects.push_back(tail);

    t.text = "ABCD";
    set_text(project, stack, t);
    REQUIRE(project.embroidery_objects.size() == 5);
    CHECK(project.embroidery_objects.back().id == tail.id);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(project.embroidery_objects[i].id != tail.id);
    }
    REQUIRE(stack.undo(project));
    REQUIRE(project.embroidery_objects.size() == 3);
    CHECK(project.embroidery_objects.back().id == tail.id);
}

TEST_CASE("removing a text removes its letters and undo restores them exactly") {
    document::Project project;
    cmd::UndoStack stack;
    stack.setMergeWindow(std::chrono::milliseconds{0});
    auto t = new_text(project, "XYZ");
    set_text(project, stack, t);
    const auto before = project;

    stack.execute(std::make_unique<cmd::RemoveTextObjectCommand>(t.id), project);
    CHECK(project.text_objects.empty());
    CHECK(project.vector_objects.empty());
    CHECK(project.embroidery_objects.empty());

    REQUIRE(stack.undo(project));
    CHECK(project.text_objects == before.text_objects);
    REQUIRE(project.vector_objects.size() == before.vector_objects.size());
    for (std::size_t i = 0; i < before.vector_objects.size(); ++i) {
        CHECK(project.vector_objects[i].id == before.vector_objects[i].id);
        CHECK(project.vector_objects[i].paths == before.vector_objects[i].paths);
    }
    CHECK(project.embroidery_objects.size() == before.embroidery_objects.size());
    // Supprimer un texte inconnu est sans effet et sans plantage.
    cmd::RemoveTextObjectCommand ghost{ObjectId{999}};
    ghost.apply(project);
    ghost.revert(project);
    CHECK(project.text_objects.size() == 1);
}

TEST_CASE("moving the letters is detected so editing keeps the visible position") {
    document::Project project;
    cmd::UndoStack stack;
    auto t = new_text(project, "Mot");
    t.origin = {Micrometers{5'000}, Micrometers{-3'000}};
    set_text(project, stack, t);

    CHECK(lettering::text_displacement(project, t, vera_bold()) == Vec2um{});
    const Vec2um delta{Micrometers{12'340}, Micrometers{-4'560}};
    for (const auto& v : project.vector_objects) {
        cmd::TranslateVectorObjectCommand move(v.id, delta);
        move.apply(project);
    }
    CHECK(lettering::text_displacement(project, t, vera_bold()) == delta);

    // Texte sans lettres (toutes supprimées) : pas de déplacement défini.
    project.vector_objects.clear();
    CHECK_FALSE(lettering::text_displacement(project, t, vera_bold()).has_value());
}

TEST_CASE("text objects survive an osp round trip and stay editable") {
    document::Project project;
    cmd::UndoStack stack;
    auto t = new_text(project, "Hello\nMonde");
    t.font.family = "Bitstream Vera Sans";
    t.font.file = "C:/fonts/Vera.ttf";
    t.font.builtin = "vera-sans";
    t.align = document::TextAlign::Center;
    t.letter_spacing = Micrometers{-300};
    t.word_spacing = Micrometers{500};
    t.line_spacing = 1.25;
    t.kerning = false;
    t.origin = {Micrometers{12'345}, Micrometers{-6'789}};
    t.rotation = Angle{0.25};
    t.rgb = {10, 20, 30};
    t.fill = document::TextFill::Satin;
    t.max_satin_width = Micrometers{5'500};
    t.density = Micrometers{450};
    t.justify_width = Micrometers{90'000};
    set_text(project, stack, t);

    const fs::path path = fs::temp_directory_path() / "openstitch_lettering_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    project_io::LoadInfo info;
    auto loaded = project_io::load_project(path, &info);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersion);
    CHECK_FALSE(info.migrated);
    // Intention du texte identique, bit pour bit.
    REQUIRE(loaded->text_objects.size() == 1);
    CHECK(loaded->text_objects[0] == t);
    // Lettres matérialisées : mêmes ids, même propriétaire, mêmes paramètres.
    REQUIRE(loaded->vector_objects.size() == project.vector_objects.size());
    REQUIRE(loaded->embroidery_objects.size() == project.embroidery_objects.size());
    for (std::size_t i = 0; i < project.vector_objects.size(); ++i) {
        CHECK(loaded->vector_objects[i].text_owner == t.id);
        CHECK(loaded->vector_objects[i].paths == project.vector_objects[i].paths);
        CHECK(loaded->embroidery_objects[i].params == project.embroidery_objects[i].params);
    }
    // Le texte reste ÉDITABLE après rechargement : la commande retrouve ses lettres.
    cmd::UndoStack stack2;
    auto t2 = loaded->text_objects[0];
    t2.text = "Salut";
    auto built = lettering::build_text_objects(vera_bold(), t2, loaded->object_ids);
    REQUIRE(built.has_value());
    stack2.execute(std::make_unique<cmd::SetTextObjectCommand>(
                       t2, std::move(built->vectors), std::move(built->embroideries), "Texte"),
                   *loaded);
    CHECK(loaded->text_objects.size() == 1);
    CHECK(loaded->embroidery_objects.size() == 5);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("a project without text still writes no textObjects key and loads clean") {
    document::Project project;
    const fs::path path = fs::temp_directory_path() / "openstitch_lettering_none.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->text_objects.empty());
    std::error_code ec;
    fs::remove(path, ec);
}
