// SPDX-License-Identifier: Apache-2.0
// Bases de texte (HP-TXT-005), edition individuelle d'une lettre (HP-TXT-008) et
// connecteurs entre lettres (HP-TXT-014) portes par document::TextObject.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>

#include "openstitch/project_io/project_io.hpp"
#include "test_support.hpp"

using namespace lettering_test;
namespace fs = std::filesystem;

namespace {

struct Box {
    double x0{1e18}, y0{1e18}, x1{-1e18}, y1{-1e18};
    [[nodiscard]] double cx() const { return (x0 + x1) / 2.0; }
    [[nodiscard]] double cy() const { return (y0 + y1) / 2.0; }
};

Box box_of(const lettering::PlacedGlyph& g) {
    Box b;
    for (const auto& set : g.shapes) {
        for (const auto& n : set.outer.nodes) {
            b.x0 = std::min(b.x0, static_cast<double>(n.pos.x.value));
            b.x1 = std::max(b.x1, static_cast<double>(n.pos.x.value));
            b.y0 = std::min(b.y0, static_cast<double>(n.pos.y.value));
            b.y1 = std::max(b.y1, static_cast<double>(n.pos.y.value));
        }
    }
    return b;
}

lettering::TextLayout layout_of(const document::TextObject& t) {
    auto layout = lettering::layout_text(vera_bold(), t, lettering::layout_options_for(t));
    REQUIRE(layout.has_value());
    return std::move(*layout);
}

lettering::BuildOptions fast() {
    lettering::BuildOptions o;
    o.verify_satin = false;
    return o;
}

} // namespace

TEST_CASE("a line base gives exactly the legacy layout") {
    auto t = make_text("HELLO", 12'000);
    const auto before = layout_of(t);
    t.base.kind = document::TextBaseKind::Line;
    t.base.radius = Micrometers{20'000}; // ignore pour une ligne
    const auto after = layout_of(t);
    REQUIRE(before.glyphs.size() == after.glyphs.size());
    for (std::size_t i = 0; i < before.glyphs.size(); ++i) {
        CHECK(before.glyphs[i].pen == after.glyphs[i].pen);
    }
}

TEST_CASE("arc base: letters lie on the circle and turn with the tangent") {
    auto t = make_text("ABCDE", 10'000);
    t.base.kind = document::TextBaseKind::Arc;
    t.base.radius = Micrometers{60'000};
    t.align = document::TextAlign::Center;
    const auto layout = layout_of(t);
    REQUIRE(layout.glyphs.size() == 5);
    // Centre du cercle sous l'origine (convexe) : distance centre -> milieu de lettre
    // = rayon + mi-hauteur de lettre (a quelques % pres).
    double first_angle = 0.0;
    double last_angle = 0.0;
    for (std::size_t i = 0; i < layout.glyphs.size(); ++i) {
        const Box b = box_of(layout.glyphs[i]);
        const double dist = std::hypot(b.cx(), b.cy() + 60'000.0);
        CHECK(dist > 60'000.0);
        CHECK(dist < 60'000.0 + 14'000.0);
        const double angle = std::atan2(b.cx(), b.cy() + 60'000.0);
        if (i == 0) {
            first_angle = angle;
        }
        last_angle = angle;
    }
    CHECK(first_angle < 0.0); // texte centre : demi-tour gauche puis droite
    CHECK(last_angle > 0.0);
}

TEST_CASE("arc base inside puts the text below with tops toward the centre") {
    auto t = make_text("ABC", 10'000);
    t.base.kind = document::TextBaseKind::Arc;
    t.base.radius = Micrometers{60'000};
    t.base.inside = true;
    t.align = document::TextAlign::Center;
    const auto layout = layout_of(t);
    REQUIRE(layout.glyphs.size() == 3);
    const Box b = box_of(layout.glyphs[1]);
    // Centre du cercle au-dessus de l'origine : les lettres sont a l'interieur du cercle.
    const double dist = std::hypot(b.cx(), b.cy() - 60'000.0);
    CHECK(dist < 60'000.0);
    CHECK(dist > 60'000.0 - 14'000.0);
}

TEST_CASE("circle base spreads the letters over the whole circumference") {
    auto t = make_text("ABCDEFGH", 8'000);
    t.base.kind = document::TextBaseKind::Circle;
    t.base.radius = Micrometers{40'000};
    const auto layout = layout_of(t);
    REQUIRE(layout.glyphs.size() == 8);
    const Box last = box_of(layout.glyphs.back());
    const Box first = box_of(layout.glyphs.front());
    const double a0 = std::atan2(first.cx(), first.cy() + 40'000.0);
    const double a1 = std::atan2(last.cx(), last.cy() + 40'000.0);
    // La derniere lettre est proche de la premiere en faisant le tour : le texte laisse un
    // emplacement vide entre sa fin et son debut (ecart d'environ deux pas).
    double gap = std::fabs(a0 - a1);
    gap = std::min(gap, 2.0 * std::numbers::pi - gap);
    CHECK(gap < 2.0 * std::numbers::pi / 8.0 * 2.5);
    CHECK(gap > 0.1);
    CHECK(layout.warnings.empty());
}

TEST_CASE("circle base warns when the text is longer than the circle") {
    auto t = make_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10'000);
    t.base.kind = document::TextBaseKind::Circle;
    t.base.radius = Micrometers{10'000};
    const auto layout = layout_of(t);
    bool warned = false;
    for (const auto& w : layout.warnings) {
        warned = warned || w.code == "texte-trop-long-cercle";
    }
    CHECK(warned);
}

TEST_CASE("path base follows a vertical polyline upward") {
    auto t = make_text("III", 8'000);
    t.base.kind = document::TextBaseKind::Path;
    t.base.path = {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{100'000}}};
    const auto layout = layout_of(t);
    REQUIRE(layout.glyphs.size() == 3);
    const Box a = box_of(layout.glyphs[0]);
    const Box b = box_of(layout.glyphs[2]);
    CHECK(b.cy() > a.cy() + 5'000.0); // les lettres montent
    // Lettres couchees : plus larges en Y qu'en X.
    CHECK((a.y1 - a.y0) < (a.x1 - a.x0) + 1.0);
    CHECK(box_of(layout.glyphs[0]).cx() < 0.0); // sommets a gauche du chemin (normale)
}

TEST_CASE("an unusable path falls back to the straight line") {
    auto line = make_text("AB", 10'000);
    auto path = line;
    path.base.kind = document::TextBaseKind::Path;
    path.base.path = {{Micrometers{0}, Micrometers{0}}};
    const auto a = layout_of(line);
    const auto b = layout_of(path);
    REQUIRE(a.glyphs.size() == b.glyphs.size());
    CHECK(a.glyphs[1].pen == b.glyphs[1].pen);
}

TEST_CASE("curved layouts are deterministic") {
    auto t = make_text("Hello World", 9'000);
    t.base.kind = document::TextBaseKind::Arc;
    t.base.radius = Micrometers{50'000};
    const auto a = layout_of(t);
    const auto b = layout_of(t);
    REQUIRE(a.glyphs.size() == b.glyphs.size());
    for (std::size_t i = 0; i < a.glyphs.size(); ++i) {
        CHECK(a.glyphs[i].shapes.size() == b.glyphs[i].shapes.size());
        CHECK(box_of(a.glyphs[i]).x0 == box_of(b.glyphs[i]).x0);
    }
}

TEST_CASE("a glyph edit moves, scales and recolours only that letter") {
    auto t = make_text("ABC", 12'000);
    const auto base = layout_of(t);
    t.glyph_edits.push_back({1, Micrometers{500}, Micrometers{-700}, Angle{0.0}, 2.0,
                             std::array<std::uint8_t, 3>{255, 0, 0}});
    const auto edited = layout_of(t);
    REQUIRE(edited.glyphs.size() == 3);
    CHECK(box_of(edited.glyphs[0]).x0 == box_of(base.glyphs[0]).x0);
    CHECK(box_of(edited.glyphs[2]).x0 == box_of(base.glyphs[2]).x0);
    const Box b0 = box_of(base.glyphs[1]);
    const Box b1 = box_of(edited.glyphs[1]);
    CHECK(std::fabs((b1.x1 - b1.x0) - 2.0 * (b0.x1 - b0.x0)) < 4.0);
    CHECK(std::fabs(b1.cx() - (b0.cx() + 500.0)) < 3.0);
    CHECK(std::fabs(b1.cy() - (b0.cy() - 700.0)) < 3.0);
    REQUIRE(edited.glyphs[1].rgb.has_value());
    CHECK((*edited.glyphs[1].rgb)[0] == 255);
    CHECK_FALSE(edited.glyphs[0].rgb.has_value());
}

TEST_CASE("a glyph rotation by 90 degrees swaps the letter box") {
    auto t = make_text("L", 12'000);
    const Box b0 = box_of(layout_of(t).glyphs[0]);
    t.glyph_edits.push_back(
        {0, Micrometers{0}, Micrometers{0}, Angle{std::numbers::pi / 2.0}, 1.0, std::nullopt});
    const Box b1 = box_of(layout_of(t).glyphs[0]);
    CHECK(std::fabs((b1.x1 - b1.x0) - (b0.y1 - b0.y0)) < 4.0);
    CHECK(std::fabs((b1.y1 - b1.y0) - (b0.x1 - b0.x0)) < 4.0);
}

TEST_CASE("a glyph edit colours the derived objects of that letter") {
    auto t = make_text("AB", 12'000);
    t.glyph_edits.push_back({1, Micrometers{0}, Micrometers{0}, Angle{0.0}, 1.0,
                             std::array<std::uint8_t, 3>{10, 20, 30}});
    IdGenerator<ObjectId> ids;
    auto built = lettering::build_text_objects(vera_bold(), t, ids, fast());
    REQUIRE(built.has_value());
    REQUIRE(built->embroideries.size() >= 2);
    CHECK(built->embroideries.front().rgb == t.rgb);
    CHECK(built->embroideries.back().rgb == (std::array<std::uint8_t, 3>{10, 20, 30}));
    CHECK(built->vectors.back().rgb == (std::array<std::uint8_t, 3>{10, 20, 30}));
}

TEST_CASE("connectors: letters and words get their own mode on the last object of a letter") {
    auto t = make_text("AB CD", 12'000);
    t.letter_connector = document::ConnectorMode::NeverTrim;
    t.word_connector = document::ConnectorMode::AlwaysTrim;
    IdGenerator<ObjectId> ids;
    auto built = lettering::build_text_objects(vera_bold(), t, ids, fast());
    REQUIRE(built.has_value());
    REQUIRE(built->layout.glyphs.size() == 4);
    int never = 0;
    int always = 0;
    for (const auto& e : built->embroideries) {
        never += e.connector == document::ConnectorMode::NeverTrim ? 1 : 0;
        always += e.connector == document::ConnectorMode::AlwaysTrim ? 1 : 0;
    }
    CHECK(never == 2);  // A -> B et C -> D
    CHECK(always == 1); // B -> C (changement de mot)
    CHECK(built->embroideries.back().connector == document::ConnectorMode::Inherit);
}

TEST_CASE("default connectors leave every object untouched") {
    auto t = make_text("AB CD", 12'000);
    IdGenerator<ObjectId> ids;
    auto built = lettering::build_text_objects(vera_bold(), t, ids, fast());
    REQUIRE(built.has_value());
    for (const auto& e : built->embroideries) {
        CHECK(e.connector == document::ConnectorMode::Inherit);
    }
}

TEST_CASE("bases, glyph edits and connectors survive the osp round trip") {
    auto t = make_text("Arc", 12'000);
    t.base.kind = document::TextBaseKind::Path;
    t.base.radius = Micrometers{33'000};
    t.base.inside = true;
    t.base.path = {{Micrometers{0}, Micrometers{0}}, {Micrometers{20'000}, Micrometers{5'000}}};
    t.glyph_edits.push_back({2, Micrometers{100}, Micrometers{-200}, Angle{0.25}, 1.5,
                             std::array<std::uint8_t, 3>{1, 2, 3}});
    t.letter_connector = document::ConnectorMode::AlwaysTrim;
    t.word_connector = document::ConnectorMode::NeverTrim;

    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    t.id = project.object_ids.next();
    project.text_objects.push_back(t);

    const auto path =
        fs::temp_directory_path() /
        ("openstitch_bases_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".osp");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->text_objects.size() == 1);
    CHECK(loaded->text_objects[0] == t);
}

TEST_CASE("a text without the new fields is written without new keys") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    auto t = make_text("Hi", 10'000);
    t.id = project.object_ids.next();
    project.text_objects.push_back(t);
    const auto path =
        fs::temp_directory_path() /
        ("openstitch_bases_legacy_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".osp");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->text_objects[0].base.kind == document::TextBaseKind::Line);
    CHECK(loaded->text_objects[0].glyph_edits.empty());
    CHECK(loaded->text_objects[0].letter_connector == document::ConnectorMode::Inherit);
}
