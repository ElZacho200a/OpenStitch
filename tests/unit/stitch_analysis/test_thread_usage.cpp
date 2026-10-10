// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/thread_usage.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

constexpr std::array<std::uint8_t, 3> kRed{200, 16, 46};
constexpr std::array<std::uint8_t, 3> kBlue{0, 114, 206};

// Trois objets : 1 rouge, 2 bleu, 3 rouge. Chacun : 3 piqûres espacées de 1 mm.
document::Project three_objects() {
    document::Project p;
    const std::array<std::uint8_t, 3> colors[3] = {kRed, kBlue, kRed};
    for (int i = 0; i < 3; ++i) {
        document::EmbroideryObject e;
        e.id = ObjectId{static_cast<std::uint64_t>(i + 1)};
        e.name = "o" + std::to_string(i + 1);
        e.rgb = colors[i];
        p.embroidery_objects.push_back(e);
    }
    return p;
}

stitch::StitchSequence sequence_for(const document::Project& p) {
    stitch::StitchSequence s;
    bool first = true;
    for (const auto& e : p.embroidery_objects) {
        if (!first) {
            s.commands.push_back({um(0, 0), CommandType::ColorChange, e.id});
        }
        first = false;
        for (int k = 0; k < 3; ++k) {
            s.commands.push_back({um(k * 1000, 0), CommandType::Stitch, e.id});
        }
    }
    s.commands.push_back({um(0, 0), CommandType::End, ObjectId{}});
    return s;
}

} // namespace

TEST_CASE("thread_usage groups objects by free colour, in first-use order") {
    const auto p = three_objects();
    const auto usage = thread_usage(p, sequence_for(p));
    REQUIRE(usage.size() == 2);
    CHECK(usage[0].identity.rgb == kRed);
    CHECK(usage[0].objects == std::vector<ObjectId>{ObjectId{1}, ObjectId{3}});
    CHECK(usage[0].stitch_count == 6);
    CHECK(usage[0].length_mm == 4.0); // 2 mm par objet
    CHECK(usage[0].color_blocks == 2);
    CHECK(usage[1].identity.rgb == kBlue);
    CHECK(usage[1].color_blocks == 1);
    CHECK(thread_label(usage[0]) == "#C8102E");
}

TEST_CASE(
    "thread_usage separates equal RGB with different threads, resolves names, estimates time") {
    auto p = three_objects();
    p.embroidery_objects[0].thread = thread_palette::ThreadKey{"generic", "G06"};
    const auto lib = thread_palette::ThreadLibrary::with_builtin();
    ThreadUsageOptions opt;
    opt.stitches_per_minute = 60.0; // 1 piqure par seconde
    opt.color_change_seconds = 10.0;
    opt.trim_seconds = 1.0;
    const auto usage = thread_usage(p, sequence_for(p), &lib, opt);
    REQUIRE(usage.size() == 3);
    CHECK(usage[0].identity.key == thread_palette::ThreadKey{"generic", "G06"});
    CHECK(usage[0].brand == "Générique");
    CHECK(usage[0].name == "Rouge");
    CHECK(thread_label(usage[0]).find("G06") != std::string::npos);
    CHECK(usage[0].objects.size() == 1);
    CHECK(usage[2].identity.rgb == kRed); // objet 3 : couleur libre
    // Fil 0 : 3 piqures, premier bloc du design -> pas de changement.
    CHECK(usage[0].estimated_seconds == 3.0);
    // Fil 1 (bleu) : 3 piqures + 1 changement.
    CHECK(usage[1].estimated_seconds == 13.0);
}

TEST_CASE("thread_usage with an unknown library entry keeps the RGB, only the name is missing") {
    auto p = three_objects();
    p.embroidery_objects[1].thread = thread_palette::ThreadKey{"user_gone", "X9"};
    const auto lib = thread_palette::ThreadLibrary::with_builtin();
    const auto usage = thread_usage(p, sequence_for(p), &lib);
    REQUIRE(usage.size() >= 2);
    CHECK(usage[1].identity.rgb == kBlue);
    CHECK(usage[1].code == "X9");
    CHECK(usage[1].name.empty());
}

TEST_CASE("thread_usage is deterministic and csv has a fixed header") {
    const auto p = three_objects();
    const auto a = thread_usage_csv(thread_usage(p, sequence_for(p)));
    const auto b = thread_usage_csv(thread_usage(p, sequence_for(p)));
    CHECK(a == b);
    CHECK(a.starts_with("ordre;marque;nuancier;reference;nom;couleur;objets;points;longueur_mm;"
                        "duree_s\n"));
    CHECK(a.find("1;;;;;#C8102E;2;6;4.0;") != std::string::npos);
}

TEST_CASE("objects_using_thread selects by key or by free colour") {
    auto p = three_objects();
    p.embroidery_objects[0].thread = thread_palette::ThreadKey{"generic", "G06"};
    CHECK(objects_using_thread(p, {thread_palette::ThreadKey{"generic", "G06"}, kRed}) ==
          std::vector<ObjectId>{ObjectId{1}});
    CHECK(objects_using_thread(p, {std::nullopt, kRed}) == std::vector<ObjectId>{ObjectId{3}});
}

TEST_CASE("color_film lists blocks in sewing order") {
    const auto p = three_objects();
    const auto film = color_film(p, sequence_for(p));
    REQUIRE(film.size() == 3);
    CHECK(film[0].objects == std::vector<ObjectId>{ObjectId{1}});
    CHECK(film[1].identity.rgb == kBlue);
    CHECK(film[2].stitch_count == 3);
    CHECK(color_change_count(film) == 2);
}

TEST_CASE("reorder_film_blocks moves a block and honours locked objects") {
    auto p = three_objects();
    auto film = color_film(p, sequence_for(p));
    const auto moved = reorder_film_blocks(p, film, 2, 0); // objet 3 en tête
    REQUIRE(moved.has_value());
    CHECK(*moved == std::vector<ObjectId>{ObjectId{3}, ObjectId{1}, ObjectId{2}});
    CHECK_FALSE(reorder_film_blocks(p, film, 3, 0).has_value());

    p.embroidery_objects[1].locked = true; // objet 2 figé en 2e position
    film = color_film(p, sequence_for(p));
    CHECK(film[1].locked);
    const auto lockedMove = reorder_film_blocks(p, film, 2, 0);
    REQUIRE(lockedMove.has_value());
    CHECK(*lockedMove == std::vector<ObjectId>{ObjectId{3}, ObjectId{2}, ObjectId{1}});
}

TEST_CASE("merge_same_thread_blocks groups equal threads and cuts colour changes") {
    const auto p = three_objects();
    const auto film = color_film(p, sequence_for(p));
    const auto order = merge_same_thread_blocks(p, film);
    CHECK(order == std::vector<ObjectId>{ObjectId{1}, ObjectId{3}, ObjectId{2}});

    // Appliquer l'ordre et recompter les changements.
    document::Project merged = p;
    std::vector<document::EmbroideryObject> next;
    for (const ObjectId id : order) {
        next.push_back(*p.findEmbroidery(id));
    }
    merged.embroidery_objects = next;
    const auto filmAfter = color_film(merged, sequence_for(merged));
    // Les deux rouges deviennent consécutifs : un seul changement réel reste.
    CHECK(color_change_count(filmAfter) == 1);
    CHECK(color_change_count(film) == 2);
}

TEST_CASE("merge_same_thread_blocks leaves hidden objects in place") {
    auto p = three_objects();
    p.embroidery_objects[1].visible = false;
    stitch::StitchSequence s; // objet 2 sans point : absent du film
    s.commands = {{um(0, 0), CommandType::Stitch, ObjectId{1}},
                  {um(1000, 0), CommandType::Stitch, ObjectId{1}},
                  {um(0, 0), CommandType::ColorChange, ObjectId{3}},
                  {um(0, 0), CommandType::Stitch, ObjectId{3}},
                  {um(1000, 0), CommandType::Stitch, ObjectId{3}},
                  {um(0, 0), CommandType::End, ObjectId{}}};
    const auto film = color_film(p, s);
    REQUIRE(film.size() == 2);
    const auto order = merge_same_thread_blocks(p, film);
    CHECK(order == std::vector<ObjectId>{ObjectId{1}, ObjectId{2}, ObjectId{3}});
}
