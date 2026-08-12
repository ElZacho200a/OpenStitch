// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/stitch_analysis/analyze.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

bool has_category(const std::vector<Finding>& f, const std::string& cat) {
    return std::any_of(f.begin(), f.end(), [&](const Finding& x) { return x.category == cat; });
}

std::size_t count_category(const std::vector<Finding>& f, const std::string& cat) {
    return static_cast<std::size_t>(
        std::count_if(f.begin(), f.end(), [&](const Finding& x) { return x.category == cat; }));
}

}  // namespace

TEST_CASE("sequence vide -> une erreur 'vide'") {
    const auto f = analyze({});
    REQUIRE(f.size() == 1);
    CHECK(f[0].severity == Severity::Error);
    CHECK(f[0].category == "vide");
}

TEST_CASE("motif correct -> aucun probleme") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(3'000, 0), CommandType::Stitch, ObjectId{}},
        {um(6'000, 0), CommandType::Stitch, ObjectId{}},
        {um(6'000, 0), CommandType::End, ObjectId{}},
    };
    CHECK(analyze(seq).empty());
}

TEST_CASE("point trop court detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(200, 0), CommandType::Stitch, ObjectId{1}},  // 0,2 mm < 0,5 mm
        {um(3'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto f = analyze(seq);
    CHECK(has_category(f, "point-court"));
    CHECK(count_category(f, "point-court") == 1);
}

TEST_CASE("point trop long detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'000, 0), CommandType::Stitch, ObjectId{1}},  // 9 mm > 7 mm
    };
    CHECK(has_category(analyze(seq), "point-long"));
}

TEST_CASE("saut trop long detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}},  // 50 mm > 30 mm
        {um(50'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto f = analyze(seq);
    CHECK(has_category(f, "saut-long"));
    // Défaut trouvé en usage réel (export debug utilisateur, objet satin
    // "GISTRE") : la couture qui reprend juste après un saut (ici à distance
    // réelle nulle du point d'atterrissage) se comparait encore à la DERNIÈRE
    // couture D'AVANT le saut -- un « point-long » fantôme signalant la
    // distance du saut lui-même comme si le fil n'avait jamais été levé.
    // `hasPrevStitch` doit être réinitialisé par le saut (§ analyze.cpp).
    CHECK_FALSE(has_category(f, "point-long"));
}

TEST_CASE("saut suivi d'une reprise a distance nulle -> aucun point-court ni point-long fantome") {
    // Motif réel : `emit_polyline` saute vers le premier point d'une passe
    // PUIS coud immédiatement à cette même position (point de « pinning »).
    // Sans réinitialisation de `prevStitch` au saut, cette couture à distance
    // nulle se comparait à la dernière couture d'avant le saut, aussi loin
    // soit-elle -- ni un point trop court (distance nulle < min) ni un point
    // trop long (distance du saut > max) ne doivent apparaître ici.
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(0, 3'000), CommandType::Stitch, ObjectId{1}},
        {um(51'400, 4'227), CommandType::Jump, ObjectId{1}},  // ~51,4 mm, cf. export debug
        {um(51'400, 4'227), CommandType::Stitch, ObjectId{1}},  // atterrissage : distance nulle
        {um(52'400, 4'227), CommandType::Stitch, ObjectId{1}},  // reprise normale (1 mm)
    };
    const auto f = analyze(seq);
    CHECK(has_category(f, "saut-long"));
    CHECK_FALSE(has_category(f, "point-long"));
    CHECK_FALSE(has_category(f, "point-court"));
}

TEST_CASE("point hors cadre detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(60'000, 0), CommandType::Stitch, ObjectId{1}},  // hors d'un cadre 100x100 centre
    };
    AnalysisOptions opts;
    opts.hoop = stitch::BoundsUm{um(-50'000, -50'000), um(50'000, 50'000)};
    const auto f = analyze(seq, opts);
    REQUIRE(has_category(f, "hors-cadre"));
    // Les erreurs sont triees en tete.
    CHECK(f.front().severity == Severity::Error);
}

TEST_CASE("plafond par categorie respecte") {
    stitch::StitchSequence seq;
    // 200 points tres courts d'affilee.
    for (int i = 0; i < 200; ++i) {
        seq.commands.push_back({um(i * 100, 0), CommandType::Stitch, ObjectId{1}});
    }
    AnalysisOptions opts;
    opts.max_findings_per_category = 10;
    CHECK(count_category(analyze(seq, opts), "point-court") == 10);
}

TEST_CASE("deterministe") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'100, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto a = analyze(seq);
    const auto b = analyze(seq);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].category == b[i].category);
    }
}
