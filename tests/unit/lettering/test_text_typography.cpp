// SPDX-License-Identifier: Apache-2.0
// Crenage manuel par paire et ligatures portes par document::TextObject (HP-TXT-007/012) :
// des champs vides redonnent EXACTEMENT les points d'avant, des champs remplis survivent a
// l'aller-retour .osp et redonnent les memes points.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "test_support.hpp"

using namespace lettering_test;
namespace fs = std::filesystem;

namespace {

// Meme conversion que apps/desktop `layoutOptionsFor` (champs du texte -> options de mise en page).
lettering::LayoutOptions options_from(const document::TextObject& t) {
    lettering::LayoutOptions options;
    for (const auto& p : t.kerning_pairs) {
        options.kerning_overrides.push_back({p.left, p.right, p.adjust});
    }
    options.ligatures = t.ligatures;
    return options;
}

document::Project project_for(const document::TextObject& t, const lettering::BuildOptions& o) {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    document::TextObject text = t;
    text.id = project.object_ids.next();
    auto built = lettering::build_text_objects(vera_bold(), text, project.object_ids, o);
    REQUIRE(built.has_value());
    project.text_objects.push_back(text);
    project.vector_objects = std::move(built->vectors);
    project.embroidery_objects = std::move(built->embroideries);
    return project;
}

stitch::StitchSequence sequence_of(const document::Project& project) {
    auto seq = stitch_generation::effective_sequence(project);
    REQUIRE(seq.has_value());
    return std::move(*seq);
}

lettering::BuildOptions fast() {
    lettering::BuildOptions o;
    o.verify_satin = false;
    return o;
}

} // namespace

TEST_CASE("typography fields left empty give exactly the previous stitches") {
    const auto t = make_text("AVOID fi", 10'000);
    REQUIRE(t.kerning_pairs.empty());
    REQUIRE_FALSE(t.ligatures);

    auto legacy = fast(); // aucune option de mise en page : ce que faisait l'application avant
    auto mapped = fast();
    mapped.layout = options_from(t);

    const auto before = sequence_of(project_for(t, legacy));
    const auto after = sequence_of(project_for(t, mapped));
    REQUIRE(before.commands.size() > 100);
    CHECK(before.commands == after.commands);
}

TEST_CASE("a manual pair changes the stitches and stays deterministic") {
    auto t = make_text("AV", 12'000);
    t.kerning = false;
    const auto plain = sequence_of(project_for(t, fast()));

    t.kerning_pairs = {{U'A', U'V', Micrometers{-1'500}}};
    auto o = fast();
    o.layout = options_from(t);
    const auto tight1 = sequence_of(project_for(t, o));
    const auto tight2 = sequence_of(project_for(t, o));
    CHECK(tight1.commands == tight2.commands);
    CHECK_FALSE(tight1.commands == plain.commands);
}

TEST_CASE("typography survives the osp round trip and rebuilds the same stitches") {
    auto t = make_text("AVOID", 12'000);
    t.kerning_pairs = {{U'A', U'V', Micrometers{-900}}, {U'O', U'I', Micrometers{300}}};
    t.ligatures = true;
    auto o = fast();
    o.layout = options_from(t);
    const auto project = project_for(t, o);
    const auto expected = sequence_of(project);

    const auto path =
        fs::temp_directory_path() /
        ("openstitch_typo_build_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".osp");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->text_objects.size() == 1);
    const auto& back = loaded->text_objects[0];
    CHECK(back.kerning_pairs == t.kerning_pairs);
    CHECK(back.ligatures);

    // Le texte relu redonne les memes lettres, donc les memes points.
    auto o2 = fast();
    o2.layout = options_from(back);
    const auto rebuilt = sequence_of(project_for(back, o2));
    CHECK(rebuilt.commands == expected.commands);
}

TEST_CASE("an existing text without typography is written without new keys") {
    const auto t = make_text("Hello", 10'000);
    const auto project = project_for(t, fast());
    const auto path =
        fs::temp_directory_path() /
        ("openstitch_typo_legacy_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".osp");
    REQUIRE(project_io::save_project(path, project).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersion);
    CHECK(loaded->text_objects[0].kerning_pairs.empty());
    CHECK_FALSE(loaded->text_objects[0].ligatures);
}
