// SPDX-License-Identifier: Apache-2.0
// Poches sans fil d'un bloc : detectees depuis les points cousus, pas depuis les contours.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/stitch_analysis/uncovered_pockets.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

struct Block {
    document::Project project;
    stitch::StitchSequence sequence;
    std::vector<ObjectId> vectors;
};

// Rectangle [x0,x1]x[y0,y1] : vecteur + tatami ; rangees tous les 400 um retirees de 200 um du bord
// (comme le retrait de contour), sauf les rangees dont l'ordonnee tombe dans `skipY` (vide force).
void add_block_rect(Block& b, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                    std::int32_t skipLo = 0, std::int32_t skipHi = -1) {
    document::VectorObject vec;
    vec.id = b.project.object_ids.next();
    geometry::PathSet set;
    for (const auto& p : {um(x0, y0), um(x1, y0), um(x1, y1), um(x0, y1)}) {
        set.outer.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    vec.paths.push_back(set);
    document::EmbroideryObject emb;
    emb.id = b.project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    b.vectors.push_back(vec.id);
    b.project.vector_objects.push_back(vec);
    b.project.embroidery_objects.push_back(emb);
    bool left = true;
    for (std::int32_t y = y0 + 200; y <= y1 - 200; y += 400) {
        if (y >= skipLo && y <= skipHi) {
            continue;
        }
        const std::int32_t a = left ? x0 + 200 : x1 - 200;
        const std::int32_t c = left ? x1 - 200 : x0 + 200;
        b.sequence.commands.push_back(
            {um(a, y), CommandType::Stitch, emb.id, StitchPass::TopStitch});
        b.sequence.commands.push_back(
            {um(c, y), CommandType::Stitch, emb.id, StitchPass::TopStitch});
        left = !left;
    }
}

double total_mm2(const std::vector<geometry::PathSet>& sets) {
    double a = 0.0;
    for (const auto& s : sets) {
        a += geometry::path_set_area_um2(s);
    }
    return a / 1e6;
}

} // namespace

TEST_CASE("pockets : un bloc entierement couvert n'a aucune poche") {
    Block b;
    add_block_rect(b, 0, 0, 10'000, 10'000);
    const auto pockets = find_uncovered_pockets(b.project, b.sequence, b.vectors);
    REQUIRE(pockets.has_value());
    CHECK(pockets->empty());
}

TEST_CASE("pockets : rangees manquantes au milieu d'une forme = une poche") {
    Block b;
    add_block_rect(b, 0, 0, 10'000, 10'000, 3'500, 6'500); // bande vide de ~3 mm
    const auto pockets = find_uncovered_pockets(b.project, b.sequence, b.vectors);
    REQUIRE(pockets.has_value());
    REQUIRE(pockets->size() == 1);
    const double area = total_mm2(*pockets);
    CHECK(area > 25.0);
    CHECK(area < 60.0);
}

TEST_CASE("pockets : une fente entre deux formes est trouvee si elle est fermable") {
    Block b;
    add_block_rect(b, 0, 0, 4'600, 10'000);
    add_block_rect(b, 5'400, 0, 10'000, 10'000); // fente de 0,8 mm
    const auto found = find_uncovered_pockets(b.project, b.sequence, b.vectors);
    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    CHECK(total_mm2(*found) > 5.0);
    CHECK(total_mm2(*found) < 25.0);

    UncoveredPocketOptions noClosing;
    noClosing.closing_gap = Micrometers{0};
    const auto ignored = find_uncovered_pockets(b.project, b.sequence, b.vectors, noClosing);
    REQUIRE(ignored.has_value());
    CHECK(ignored->empty()); // la fente est hors de l'enveloppe : ce n'est pas un trou du bloc
}

TEST_CASE("pockets : un fil d'un objet hors bloc couvre la poche") {
    Block b;
    add_block_rect(b, 0, 0, 10'000, 10'000, 3'500, 6'500);
    // Un second objet (hors bloc) coud la bande manquante.
    document::EmbroideryObject other;
    other.id = b.project.object_ids.next();
    other.params = document::TatamiParams{};
    b.project.embroidery_objects.push_back(other);
    for (std::int32_t y = 3'500; y <= 6'500; y += 300) {
        b.sequence.commands.push_back(
            {um(0, y), CommandType::Stitch, other.id, StitchPass::TopStitch});
        b.sequence.commands.push_back(
            {um(10'000, y), CommandType::Stitch, other.id, StitchPass::TopStitch});
    }
    const auto pockets = find_uncovered_pockets(b.project, b.sequence, b.vectors);
    REQUIRE(pockets.has_value());
    CHECK(pockets->empty());
}

TEST_CASE("pockets : sous-couche et deplacements ne comptent pas, resultat deterministe") {
    Block b;
    add_block_rect(b, 0, 0, 10'000, 10'000, 3'500, 6'500);
    for (std::int32_t y = 3'500; y <= 6'500; y += 300) {
        b.sequence.commands.push_back({um(0, y), CommandType::Stitch,
                                       b.project.embroidery_objects[0].id, StitchPass::Underlay});
        b.sequence.commands.push_back({um(10'000, y), CommandType::Stitch,
                                       b.project.embroidery_objects[0].id, StitchPass::Underlay});
    }
    const auto first = find_uncovered_pockets(b.project, b.sequence, b.vectors);
    const auto second = find_uncovered_pockets(b.project, b.sequence, b.vectors);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->size() == 1);
    CHECK(*first == *second);
}

TEST_CASE("pockets : un bloc sans surface est refuse") {
    Block b;
    const auto pockets = find_uncovered_pockets(b.project, b.sequence, {ObjectId{99}});
    CHECK_FALSE(pockets.has_value());
}
