// SPDX-License-Identifier: Apache-2.0
// HP-FMT-002..005 : analyse pré-export propre au format cible + inscription au registre.
#include <catch2/catch_test_macros.hpp>

#include "machine_test_helpers.hpp"
#include "openstitch/formats/export_limits.hpp"
#include "openstitch/formats/format_registry.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using namespace machine_test;

namespace {
bool has(const std::vector<ExportIssue>& issues, ExportIssueSeverity severity,
         const std::string& needle) {
    for (const auto& i : issues) {
        if (i.severity == severity && i.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("registre : pes, jef, exp inscrits en lecture et ecriture, par id et par extension") {
    for (const char* id : {"pes", "jef", "exp"}) {
        const auto* f = find_format(id);
        REQUIRE(f != nullptr);
        CHECK(f->can_read);
        CHECK(f->can_write);
        CHECK(f->encode != nullptr);
        CHECK(f->decode != nullptr);
        CHECK(f->encode_ex != nullptr);
        CHECK(f->decode_ex != nullptr);
        CHECK(find_format_for_extension(id) == f);
    }
    CHECK(find_format_for_extension("PES") == find_format("pes"));
    CHECK(find_format("pes")->carries_colors);
    CHECK(find_format("jef")->carries_colors);
    CHECK_FALSE(find_format("exp")->carries_colors);
    CHECK_FALSE(find_format("dst")->carries_colors);
    CHECK(find_format("pes")->default_constraints.max_colors == 255);
}

TEST_CASE("registre : encode/decode simples fonctionnent pour chaque nouveau format") {
    const auto seq = rich_short();
    for (const char* id : {"pes", "jef", "exp"}) {
        const auto* f = find_format(id);
        REQUIRE(f != nullptr);
        const auto bytes = f->encode(seq);
        REQUIRE(bytes.has_value());
        const auto decoded = f->decode(*bytes);
        REQUIRE(decoded.has_value());
        CHECK_FALSE(decoded->commands.empty());
        const auto viaEx = f->decode_ex(*bytes);
        REQUIRE(viaEx.has_value());
        CHECK(viaEx->sequence.commands == decoded->commands);
    }
}

TEST_CASE("export_limits : trop de couleurs pour PES = erreur ; EXP/DST = information") {
    StitchSequence many;
    many.commands.push_back({um(0, 0), CommandType::Stitch, ObjectId{}});
    for (int i = 0; i < 300; ++i) {
        many.commands.push_back({um(i * 100, 0), CommandType::Stitch, ObjectId{}});
        many.commands.push_back({um(i * 100, 0), CommandType::ColorChange, ObjectId{}});
    }
    many.commands.push_back({um(30'000, 0), CommandType::End, ObjectId{}});

    const auto pes = check_export_limits(many, *find_format("pes"));
    CHECK(has(pes, ExportIssueSeverity::Error, "255"));
    const auto jef = check_export_limits(many, *find_format("jef"));
    CHECK_FALSE(has(jef, ExportIssueSeverity::Error, "blocs"));
    const auto exp = check_export_limits(many, *find_format("exp"));
    CHECK(has(exp, ExportIssueSeverity::Info, "ne conserve pas les couleurs"));
}

TEST_CASE("export_limits : etendue hors plage PES, deplacements decoupes, cadres connus") {
    const auto huge = make({
        {um(0, 0), CommandType::Stitch},
        {um(4'000'000, 0), CommandType::Stitch},
        {um(4'000'000, 0), CommandType::End},
    });
    CHECK(has(check_export_limits(huge, *find_format("pes")), ExportIssueSeverity::Error,
              "plage codable"));

    const auto far = rich_long_jump();
    CHECK(
        has(check_export_limits(far, *find_format("jef")), ExportIssueSeverity::Info, "découpés"));

    const auto wide = make({
        {um(0, 0), CommandType::Stitch},
        {um(250'000, 0), CommandType::Stitch},
        {um(250'000, 250'000), CommandType::Stitch},
        {um(250'000, 250'000), CommandType::End},
    });
    CHECK(has(check_export_limits(wide, *find_format("jef")), ExportIssueSeverity::Warning,
              "Janome"));
    CHECK(has(check_export_limits(wide, *find_format("pes")), ExportIssueSeverity::Warning,
              "Brother"));
    CHECK_FALSE(has(check_export_limits(rich_short(), *find_format("pes")),
                    ExportIssueSeverity::Warning, "Brother"));
}
