// SPDX-License-Identifier: Apache-2.0
#include <cmath>
#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "hoop_store.hpp"
#include "openstitch/project_ops/factory.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
DocumentSession newSession() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}

fs::path tempDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("openstitch_hoops_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

UserHoop hoop(const std::string& name, double w = 150.0, double h = 120.0) {
    UserHoop u;
    u.name = name;
    u.widthMm = w;
    u.heightMm = h;
    u.marginMm = 4.0;
    return u;
}
} // namespace

TEST_CASE("a user hoop is validated before it is stored", "[desktop_v2][hoops]") {
    CHECK(validateUserHoop(hoop("Cadre atelier")).empty());
    CHECK_FALSE(validateUserHoop(hoop("")).empty());
    CHECK_FALSE(validateUserHoop(hoop("   ")).empty());
    CHECK_FALSE(validateUserHoop(hoop(std::string(41, 'x'))).empty());
    CHECK_FALSE(validateUserHoop(hoop("a;b")).empty());
    CHECK_FALSE(validateUserHoop(hoop("a\nb")).empty());
    CHECK_FALSE(validateUserHoop(hoop("ok", 5.0, 100.0)).empty());
    CHECK_FALSE(validateUserHoop(hoop("ok", 100.0, 5000.0)).empty());
    CHECK_FALSE(validateUserHoop(hoop("ok", std::nan(""), 100.0)).empty());
    auto margin = hoop("ok", 100.0, 100.0);
    margin.marginMm = 60.0; // >= moitié du plus petit côté
    CHECK_FALSE(validateUserHoop(margin).empty());
    margin.marginMm = -1.0;
    CHECK_FALSE(validateUserHoop(margin).empty());
    auto rounded = hoop("ok", 100.0, 60.0);
    rounded.shape = UserHoopShape::RoundedRectangle;
    rounded.cornerRadiusMm = 20.0;
    CHECK(validateUserHoop(rounded).empty());
    rounded.cornerRadiusMm = 40.0; // > moitié de 60
    CHECK_FALSE(validateUserHoop(rounded).empty());
}

TEST_CASE("adding replaces a hoop of the same name regardless of case and keeps the order",
          "[desktop_v2][hoops]") {
    std::vector<UserHoop> list;
    REQUIRE(addUserHoop(list, hoop("Alpha")).ok);
    REQUIRE(addUserHoop(list, hoop("Beta")).ok);
    REQUIRE(addUserHoop(list, hoop("ALPHA", 200.0, 100.0)).ok); // remplace, ne duplique pas
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "ALPHA");
    CHECK(list[0].widthMm == Catch::Approx(200.0));
    CHECK(list[1].name == "Beta");
    CHECK_FALSE(addUserHoop(list, hoop("")).ok);
    CHECK(list.size() == 2);
    CHECK(findUserHoop(list, "beta") != nullptr);
    CHECK(findUserHoop(list, "nope") == nullptr);
    CHECK(removeUserHoop(list, "BETA"));
    CHECK_FALSE(removeUserHoop(list, "BETA"));
    CHECK(list.size() == 1);
}

TEST_CASE("the list is capped at fifty hoops", "[desktop_v2][hoops]") {
    std::vector<UserHoop> list;
    for (std::size_t i = 0; i < kMaxUserHoops; ++i) {
        REQUIRE(addUserHoop(list, hoop("c" + std::to_string(i))).ok);
    }
    CHECK_FALSE(addUserHoop(list, hoop("one-too-many")).ok);
    CHECK(list.size() == kMaxUserHoops);
    CHECK(addUserHoop(list, hoop("c3", 99.0, 99.0)).ok); // remplacer reste permis
}

TEST_CASE("hoops round-trip through text and a corrupt line never hides the others",
          "[desktop_v2][hoops]") {
    std::vector<UserHoop> list;
    REQUIRE(addUserHoop(list, hoop("Rond", 150.0, 150.0)).ok);
    list.back().shape = UserHoopShape::Ellipse;
    REQUIRE(addUserHoop(list, hoop("Arrondi", 180.0, 130.0)).ok);
    list.back().shape = UserHoopShape::RoundedRectangle;
    list.back().cornerRadiusMm = 12.5;
    const auto text = userHoopsToText(list);
    CHECK(text == userHoopsToText(list)); // déterministe
    const auto back = userHoopsFromText(text);
    REQUIRE(back.size() == 2);
    CHECK(back[0].shape == UserHoopShape::Ellipse);
    CHECK(back[1].shape == UserHoopShape::RoundedRectangle);
    CHECK(back[1].cornerRadiusMm == Catch::Approx(12.5));

    const auto mixed = userHoopsFromText("garbage\nA;rect;100;100;0;0\nB;hexagon;1;1;0;0\r\n"
                                         "C;rect;abc;100;0;0\nD;rect;120;90;0;3\n;;;;;\n");
    REQUIRE(mixed.size() == 2); // seules A et D sont valides
    CHECK(mixed[0].name == "A");
    CHECK(mixed[1].name == "D");
    CHECK(userHoopsFromText("").empty());
}

TEST_CASE("hoops are saved atomically and a missing file gives an empty list",
          "[desktop_v2][hoops]") {
    const auto dir = tempDir("save");
    const auto file = dir / "sub" / "hoops.txt";
    CHECK(loadUserHoops(file).empty());
    std::vector<UserHoop> list;
    REQUIRE(addUserHoop(list, hoop("Atelier")).ok);
    REQUIRE(saveUserHoops(file, list));
    CHECK_FALSE(fs::exists(fs::path(file).concat(".tmp")));
    const auto back = loadUserHoops(file);
    REQUIRE(back.size() == 1);
    CHECK(back[0].name == "Atelier");
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "Atel"; // fichier tronqué
    }
    CHECK(loadUserHoops(file).empty());
    fs::remove_all(dir);
}

TEST_CASE("a user hoop is applied to the project in one undo step and can be captured back",
          "[desktop_v2][hoops]") {
    auto s = newSession();
    auto u = hoop("Mon cadre", 140.0, 110.0);
    u.shape = UserHoopShape::RoundedRectangle;
    u.cornerRadiusMm = 10.0;
    const auto depth = s.undoNames().size();
    REQUIRE(applyUserHoop(s, u).ok);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(s.project().canvas.width.value == 140'000);
    CHECK(s.project().canvas.margin.value == 4'000);
    CHECK(s.project().canvas.shape == document::HoopShape::RoundedRectangle);
    CHECK_FALSE(applyUserHoop(s, u).ok); // déjà celui-là : aucune trace de plus
    CHECK(s.undoNames().size() == depth + 1);
    auto bad = u;
    bad.widthMm = 3.0;
    CHECK_FALSE(applyUserHoop(s, bad).ok);

    const auto captured = userHoopFromProject(s.project(), "Capturé");
    CHECK(captured.widthMm == Catch::Approx(140.0));
    CHECK(captured.cornerRadiusMm == Catch::Approx(10.0));
    CHECK(captured.marginMm == Catch::Approx(4.0));
    CHECK(captured.shape == UserHoopShape::RoundedRectangle);
    CHECK(validateUserHoop(captured).empty());

    REQUIRE(s.undo());
    CHECK(s.project().canvas.width.value == 100'000);
}
