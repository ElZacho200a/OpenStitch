// SPDX-License-Identifier: Apache-2.0
// HP-PERF-002 : cache de regeneration par objet. La sequence doit rester identique, octet pour
// octet, avec ou sans cache ; seul l'objet modifie est regenere.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

void set_threads(const char* value) {
#ifdef _WIN32
    _putenv_s("OPENSTITCH_THREADS", value);
#else
    setenv("OPENSTITCH_THREADS", value, 1);
#endif
}

geometry::Path square_at(std::int32_t x, std::int32_t size) {
    geometry::Path p;
    p.closed = true;
    const auto node = [](std::int32_t px, std::int32_t py) {
        return geometry::PathNode{
            Vec2um{Micrometers{px}, Micrometers{py}}, geometry::NodeType::Corner, {}, {}};
    };
    p.nodes = {node(x, 0), node(x + size, 0), node(x + size, size), node(x, size)};
    return p;
}

// Quatre carres : contour, tatami, remplissage directionnel, tatami.
document::Project make_project() {
    document::Project project;
    for (int i = 0; i < 4; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.paths.push_back(geometry::PathSet{square_at(i * 15'000, 10'000), {}});
        project.vector_objects.push_back(vec);

        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "objet " + std::to_string(i);
        emb.source_vector = vec.id;
        emb.rgb = {static_cast<std::uint8_t>(40 * i), 30, 200};
        if (i == 1 || i == 3) {
            emb.params = document::TatamiParams{};
        } else if (i == 2) {
            emb.params = document::DirectionalFillParams{};
        }
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

stitch::StitchSequence uncached(const document::Project& project) {
    set_chunk_cache_enabled(false);
    const auto seq = generate_sequence(project);
    set_chunk_cache_enabled(true);
    REQUIRE(seq.has_value());
    return *seq;
}

} // namespace

TEST_CASE("chunk cache : sequence identique avec et sans cache") {
    const document::Project project = make_project();
    const auto reference = uncached(project);

    clear_chunk_cache();
    const auto cold = generate_sequence(project);
    const auto warm = generate_sequence(project);
    REQUIRE(cold.has_value());
    REQUIRE(warm.has_value());
    CHECK(cold->commands == reference.commands);
    CHECK(warm->commands == reference.commands);

    const ChunkCacheStats stats = chunk_cache_stats();
    CHECK(stats.entries == 4);
    CHECK(stats.misses == 4);
    CHECK(stats.hits == 4);
}

TEST_CASE("chunk cache : modifier un objet ne regenere que lui") {
    document::Project project = make_project();
    clear_chunk_cache();
    REQUIRE(generate_sequence(project).has_value());

    auto& tatami = std::get<document::TatamiParams>(project.embroidery_objects[1].params);
    tatami.row_spacing = Micrometers{600};

    const auto edited = generate_sequence(project);
    REQUIRE(edited.has_value());
    CHECK(edited->commands == uncached(project).commands);

    const ChunkCacheStats stats = chunk_cache_stats();
    CHECK(stats.misses == 4 + 1); // 4 du premier calcul + l'objet modifie
    CHECK(stats.hits == 3);       // les trois autres servis du cache
}

TEST_CASE("chunk cache : une geometrie modifiee invalide l'entree") {
    document::Project project = make_project();
    clear_chunk_cache();
    REQUIRE(generate_sequence(project).has_value());

    project.vector_objects[2].paths[0].outer.nodes[2].pos.x = Micrometers{45'000};
    const auto edited = generate_sequence(project);
    REQUIRE(edited.has_value());
    CHECK(edited->commands == uncached(project).commands);
    CHECK(chunk_cache_stats().misses == 5);
}

TEST_CASE(
    "chunk cache : un identifiant reutilise avec un autre contenu n'est jamais servi perime") {
    const document::Project a = make_project();
    document::Project b = make_project(); // memes identifiants, contenu different
    std::get<document::TatamiParams>(b.embroidery_objects[3].params).angle = Angle{0.5};
    b.vector_objects[0].paths[0].outer.nodes[1].pos.x = Micrometers{12'345};

    clear_chunk_cache();
    REQUIRE(generate_sequence(a).has_value());
    const auto fromB = generate_sequence(b);
    REQUIRE(fromB.has_value());
    CHECK(fromB->commands == uncached(b).commands);
    const auto backToA = generate_sequence(a);
    REQUIRE(backToA.has_value());
    CHECK(backToA->commands == uncached(a).commands);
}

TEST_CASE("chunk cache : meme resultat avec un seul fil et avec plusieurs") {
    const document::Project project = make_project();
    set_threads("1");
    clear_chunk_cache();
    const auto one = generate_sequence(project);
    set_threads("8");
    clear_chunk_cache();
    const auto many = generate_sequence(project);
    const auto manyWarm = generate_sequence(project);
    set_threads("");
    REQUIRE(one.has_value());
    REQUIRE(many.has_value());
    CHECK(one->commands == many->commands);
    CHECK(manyWarm->commands == many->commands);
}

TEST_CASE("chunk cache : effective_sequence suit une retouche de parametres") {
    document::Project project = make_project();
    clear_chunk_cache();
    const auto before = effective_sequence(project);
    REQUIRE(before.has_value());

    std::get<document::DirectionalFillParams>(project.embroidery_objects[2].params).row_spacing =
        Micrometers{700};
    const auto after = effective_sequence(project);
    REQUIRE(after.has_value());
    CHECK(after->commands != before->commands);

    set_chunk_cache_enabled(false);
    const auto reference = effective_sequence(project);
    set_chunk_cache_enabled(true);
    REQUIRE(reference.has_value());
    CHECK(after->commands == reference->commands);
}
