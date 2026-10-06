// SPDX-License-Identifier: Apache-2.0
// AD-04 : le design importé est une donnée source du document, pas une
// exception de l'UI. Ces tests couvrent le type lui-même (égalité, valeur
// par défaut de `Project::imported_design`) -- le round-trip `.osp` est
// couvert par tests/unit/project_io/test_machine_file.cpp.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/document/project.hpp"

using namespace openstitch;

TEST_CASE("Project::imported_design : absent par defaut") {
    document::Project project;
    CHECK_FALSE(project.imported_design.has_value());
}

TEST_CASE("ImportedDesign : egalite de valeur") {
    document::ImportedDesign a;
    a.source_format = "dst";
    a.sequence.commands = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, stitch::CommandType::Stitch, ObjectId{}},
    };
    stitch::ColorBlock block;
    block.rgb = {1, 2, 3};
    block.start = 0;
    block.end = 1;
    a.color_blocks = {block};

    document::ImportedDesign b = a;
    CHECK(a == b);

    b.source_format = "autre";
    CHECK_FALSE(a == b);
}
