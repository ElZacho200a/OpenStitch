// SPDX-License-Identifier: Apache-2.0
//
// HP-HOOP-003 : verifications selon le profil machine, et "hors cadre" sur la forme reelle.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "openstitch/document/hoop_library.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/machine_checks.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchCommand cmd(CommandType type, Vec2um pos) {
    stitch::StitchCommand c;
    c.type = type;
    c.pos = pos;
    return c;
}

// Une ligne de points de x0 a x1 (pas 2 mm) a y donne.
stitch::StitchSequence line(std::int32_t x0, std::int32_t x1, std::int32_t y) {
    stitch::StitchSequence seq;
    for (std::int32_t x = x0; x <= x1; x += 2'000) {
        seq.commands.push_back(cmd(CommandType::Stitch, um(x, y)));
    }
    seq.commands.push_back(cmd(CommandType::End, um(x1, y)));
    return seq;
}

bool has(const std::vector<Finding>& all, const char* category) {
    return std::any_of(all.begin(), all.end(),
                       [&](const Finding& f) { return f.category == category; });
}

} // namespace

TEST_CASE("machine checks: no machine means no finding") {
    document::Project project;
    CHECK(check_machine(project, line(-50'000, 50'000, 0), "dst").empty());
}

TEST_CASE("machine checks: unknown profile and incompatible format") {
    document::Project project;
    project.machine_id = "no-such-machine";
    CHECK(has(check_machine(project, line(0, 10'000, 0)), "machine-inconnue"));

    project.machine_id = "generic-single-130x180";
    CHECK(!has(check_machine(project, line(0, 10'000, 0), "pes"), "machine-format"));
    CHECK(has(check_machine(project, line(0, 10'000, 0), "pec"), "machine-format"));
    CHECK(!has(check_machine(project, line(0, 10'000, 0)), "machine-format")); // non verifie
}

TEST_CASE("machine checks: design larger than the field is an error, rotation allowed") {
    document::Project project;
    project.machine_id = "generic-single-130x180";
    // 170 mm de large sur 0 de haut : tient tourne de 90 degres (180 mm de cote).
    const auto ok = check_machine(project, line(-85'000, 85'000, 0));
    CHECK(!has(ok, "machine-champ"));
    // 200 mm : ne tient dans aucune orientation.
    const auto big = check_machine(project, line(-100'000, 100'000, 0));
    REQUIRE(has(big, "machine-champ"));
    CHECK(std::any_of(big.begin(), big.end(), [](const Finding& f) {
        return f.category == "machine-champ" && f.severity == Severity::Error;
    }));
}

TEST_CASE("machine checks: no hoop of the machine holds the design") {
    document::Project project;
    project.machine_id = "generic-single-100";
    project.canvas.margin = Micrometers{5'000};
    // 95 mm de large : dans le champ (100) mais plus grand que la zone brodable (90 mm).
    CHECK(has(check_machine(project, line(-47'500, 47'500, 0)), "machine-cadre"));
    CHECK(!has(check_machine(project, line(-40'000, 40'000, 0)), "machine-cadre"));
}

TEST_CASE("machine checks: round hoop shape is honoured by hors-cadre") {
    document::Project project;
    project.canvas = document::canvas_from_hoop(*document::find_hoop("round-100"));
    stitch::StitchSequence seq;
    seq.commands.push_back(cmd(CommandType::Stitch, um(0, 0)));
    seq.commands.push_back(cmd(CommandType::Stitch, um(40'000, 40'000))); // boite oui, disque non
    seq.commands.push_back(cmd(CommandType::End, um(40'000, 40'000)));
    const auto report = analyze_project(project, seq);
    const auto it = std::find_if(report.findings.begin(), report.findings.end(),
                                 [](const Finding& f) { return f.category == "hors-cadre"; });
    REQUIRE(it != report.findings.end());

    project.canvas.shape = document::HoopShape::Rectangle;
    const auto rect = analyze_project(project, seq);
    CHECK(std::none_of(rect.findings.begin(), rect.findings.end(),
                       [](const Finding& f) { return f.category == "hors-cadre"; }));
}

TEST_CASE("machine checks: long stitches and trims without a cutter") {
    document::Project project;
    project.machine_id = "generic-single-100"; // 7 mm max, pas de coupe-fil
    stitch::StitchSequence seq;
    seq.commands.push_back(cmd(CommandType::Stitch, um(0, 0)));
    seq.commands.push_back(cmd(CommandType::Stitch, um(9'000, 0)));
    seq.commands.push_back(cmd(CommandType::Trim, um(9'000, 0)));
    seq.commands.push_back(cmd(CommandType::End, um(9'000, 0)));
    const auto findings = check_machine(project, seq);
    CHECK(has(findings, "machine-point-long"));
    CHECK(has(findings, "machine-coupe"));

    project.machine_id = "generic-multi-6"; // 12 mm, coupe-fil
    const auto fine = check_machine(project, seq);
    CHECK(!has(fine, "machine-point-long"));
    CHECK(!has(fine, "machine-coupe"));
}
