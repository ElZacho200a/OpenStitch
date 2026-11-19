// SPDX-License-Identifier: Apache-2.0
// HP-PERF-002 : cache de regeneration par objet. La sequence doit rester identique, octet pour
// octet, avec ou sans cache ; seul l'objet modifie est regenere.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <stop_token>
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

TEST_CASE("chunk cache : fragments immuables reutilises avec jointures automatiques") {
    document::Project project = make_project();
    project.finishing.auto_join = true;
    const auto reference = uncached(project);

    clear_chunk_cache();
    const auto cold = generate_sequence(project);
    const auto warm = generate_sequence(project);
    REQUIRE(cold.has_value());
    REQUIRE(warm.has_value());
    CHECK(cold->commands == reference.commands);
    CHECK(warm->commands == reference.commands);
    CHECK(chunk_cache_stats().hits == 4);

    // Les voisins sont lus directement depuis les fragments en cache, même si
    // l'objet du milieu a été modifié et doit, lui, être regénéré.
    std::get<document::TatamiParams>(project.embroidery_objects[1].params).row_spacing =
        Micrometers{550};
    const auto edited = generate_sequence(project);
    REQUIRE(edited.has_value());
    CHECK(edited->commands == uncached(project).commands);
    CHECK(chunk_cache_stats().hits == 7);
    CHECK(chunk_cache_stats().misses == 5);

    const auto cachedFinal = effective_sequence(project);
    REQUIRE(cachedFinal.has_value());
    set_chunk_cache_enabled(false);
    const auto uncachedFinal = effective_sequence(project);
    set_chunk_cache_enabled(true);
    REQUIRE(uncachedFinal.has_value());
    CHECK(cachedFinal->commands == uncachedFinal->commands);
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

namespace {

geometry::PathNode corner(std::int32_t x, std::int32_t y) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, {}, {}};
}

// Colonne satin droite de x0 a x0+len, a la hauteur y0, deux barreaux (donc « routable »).
document::SatinParams column(std::int32_t x0, std::int32_t y0, std::int32_t len = 12'000) {
    document::SatinParams sp;
    sp.rail_a.closed = false;
    sp.rail_a.nodes = {corner(x0, y0), corner(x0 + len, y0)};
    sp.rail_b.closed = false;
    sp.rail_b.nodes = {corner(x0, y0 + 4'000), corner(x0 + len, y0 + 4'000)};
    sp.rungs = {{Vec2um{Micrometers{x0}, Micrometers{y0}},
                 Vec2um{Micrometers{x0}, Micrometers{y0 + 4'000}}},
                {Vec2um{Micrometers{x0 + len}, Micrometers{y0}},
                 Vec2um{Micrometers{x0 + len}, Micrometers{y0 + 4'000}}}};
    return sp;
}

// Indices : 0 tatami, 1-3 groupe de trois colonnes routees (meme couleur et meme source),
// 4 tatami, 5 colonne routee seule, 6 satin legacy sans barreaux, 7 auto-satin.
document::Project make_satin_project() {
    document::Project project;
    const auto addVector = [&](std::int32_t x) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.paths.push_back(geometry::PathSet{square_at(x, 10'000), {}});
        project.vector_objects.push_back(vec);
        return vec.id;
    };
    const auto addObject = [&](document::StitchParams params, ObjectId source, std::uint8_t r) {
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "objet " + std::to_string(project.embroidery_objects.size());
        emb.source_vector = source;
        emb.rgb = {r, 30, 200};
        emb.params = std::move(params);
        project.embroidery_objects.push_back(std::move(emb));
    };
    const ObjectId v0 = addVector(0);
    const ObjectId v1 = addVector(40'000);
    const ObjectId v2 = addVector(80'000);
    addObject(document::TatamiParams{}, v0, 10);
    addObject(column(0, 20'000), ObjectId{}, 50);
    addObject(column(14'000, 22'000), ObjectId{}, 50);
    addObject(column(28'000, 20'000), ObjectId{}, 50);
    addObject(document::TatamiParams{}, v1, 90);
    addObject(column(40'000, 40'000), ObjectId{}, 130);
    document::SatinParams legacy = column(60'000, 40'000);
    legacy.rungs.clear();
    addObject(legacy, ObjectId{}, 170);
    addObject(document::AutoSatinParams{}, v2, 210);
    return project;
}

} // namespace

TEST_CASE("chunk cache : satins routes et auto-satin identiques avec et sans cache") {
    for (const bool join : {false, true}) {
        document::Project project = make_satin_project();
        project.finishing.auto_join = join;
        const auto reference = uncached(project);

        clear_chunk_cache();
        const auto cold = generate_sequence(project);
        const auto warm = generate_sequence(project);
        REQUIRE(cold.has_value());
        REQUIRE(warm.has_value());
        CHECK(cold->commands == reference.commands);
        CHECK(warm->commands == reference.commands);
        // Deuxieme passe : tous les objets (independants et unites routees) sont servis.
        const ChunkCacheStats stats = chunk_cache_stats();
        CHECK(stats.hits == stats.misses);
        // 2 tatamis + groupe de 3 colonnes + colonne seule + satin legacy + auto-satin.
        CHECK(stats.entries == 6);

        const auto effectiveCached = effective_sequence(project);
        set_chunk_cache_enabled(false);
        const auto effectiveRef = effective_sequence(project);
        set_chunk_cache_enabled(true);
        REQUIRE(effectiveCached.has_value());
        REQUIRE(effectiveRef.has_value());
        CHECK(effectiveCached->commands == effectiveRef->commands);
    }
}

TEST_CASE("chunk cache : une modification en amont change le contexte des satins routes") {
    document::Project project = make_satin_project();
    project.finishing.auto_join = true;
    clear_chunk_cache();
    REQUIRE(generate_sequence(project).has_value());

    // Deplacer le tatami 0 change la derniere commande vue par le groupe de colonnes : la cle
    // (derniere commande) differe, l'entree ne doit jamais etre servie perimee.
    project.vector_objects[0].paths.front() = geometry::PathSet{square_at(-3'000, 7'000), {}};
    const auto after = generate_sequence(project);
    REQUIRE(after.has_value());
    CHECK(after->commands == uncached(project).commands);

    // Modifier une colonne du groupe, puis un parametre de l'auto-satin, puis l'ordre.
    std::get<document::SatinParams>(project.embroidery_objects[2].params).density =
        Micrometers{450};
    CHECK(generate_sequence(project)->commands == uncached(project).commands);
    std::get<document::AutoSatinParams>(project.embroidery_objects[7].params).spacing =
        Micrometers{500};
    CHECK(generate_sequence(project)->commands == uncached(project).commands);
    std::swap(project.embroidery_objects[4], project.embroidery_objects[5]);
    CHECK(generate_sequence(project)->commands == uncached(project).commands);
    // Geometrie source de l'auto-satin.
    project.vector_objects[2].paths.front() = geometry::PathSet{square_at(80'000, 12'000), {}};
    CHECK(generate_sequence(project)->commands == uncached(project).commands);
}

TEST_CASE("chunk cache : satins routes identiques avec un seul fil et avec plusieurs") {
    for (const bool join : {false, true}) {
        document::Project project = make_satin_project();
        project.finishing.auto_join = join;
        set_threads("1");
        clear_chunk_cache();
        const auto one = generate_sequence(project);
        const auto oneWarm = generate_sequence(project);
        set_threads("8");
        clear_chunk_cache();
        const auto many = generate_sequence(project);
        const auto manyWarm = generate_sequence(project);
        set_threads("");
        REQUIRE(one.has_value());
        REQUIRE(many.has_value());
        CHECK(one->commands == many->commands);
        CHECK(oneWarm->commands == many->commands);
        CHECK(manyWarm->commands == many->commands);
        CHECK(many->commands == uncached(project).commands);
    }
}

TEST_CASE("task control : effective_sequence annulable identique sans annulation") {
    const document::Project project = make_project();
    const auto reference = effective_sequence(project);
    REQUIRE(reference.has_value());

    std::atomic<std::size_t> reports{0};
    std::atomic<std::size_t> lastTotal{0};
    TaskContext task;
    task.progress = [&](std::size_t, std::size_t total) {
        reports.fetch_add(1);
        lastTotal.store(total);
    };
    const auto withTask = effective_sequence(project, task);
    REQUIRE(withTask.has_value());
    CHECK(withTask->commands == reference->commands);
    CHECK(reports.load() == 4);
    CHECK(lastTotal.load() == 4);
}

TEST_CASE("task control : une annulation renvoie Cancelled et ne pollue pas le cache") {
    const document::Project project = make_project();
    clear_chunk_cache();

    std::stop_source source;
    source.request_stop();
    TaskContext task;
    task.stop = source.get_token();
    const auto cancelled = effective_sequence(project, task);
    REQUIRE_FALSE(cancelled.has_value());
    CHECK(cancelled.error().category == ErrorCategory::Cancelled);
    CHECK(chunk_cache_stats().entries == 0);

    // Le calcul suivant, non annule, reste correct.
    const auto reference = uncached(project);
    const auto after = generate_sequence(project);
    REQUIRE(after.has_value());
    CHECK(after->commands == reference.commands);
}
