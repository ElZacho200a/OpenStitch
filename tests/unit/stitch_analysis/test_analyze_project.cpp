// SPDX-License-Identifier: Apache-2.0
//
// Analyse unifiée : `analyze_project` (options + cadre du canevas + séquence + texte + réglages)
// et les corrections proposées (`Finding::fix`).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/text_rules.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Contour carré (point de course) de côté 2*half centré en (cx, cy).
ObjectId add_square(document::Project& project, std::int32_t cx, std::int32_t cy, std::int32_t half,
                    document::StitchParams params) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "carre";
    geometry::PathSet set;
    for (const auto& p : {um(cx - half, cy - half), um(cx + half, cy - half),
                          um(cx + half, cy + half), um(cx - half, cy + half)}) {
        set.outer.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    vec.paths.push_back(set);
    document::EmbroideryObject obj;
    obj.id = project.object_ids.next();
    obj.name = "objet";
    obj.source_vector = vec.id;
    obj.params = std::move(params);
    project.vector_objects.push_back(vec);
    project.embroidery_objects.push_back(obj);
    return obj.id;
}

std::vector<Finding> of_category(const std::vector<Finding>& all, const std::string& category) {
    std::vector<Finding> out;
    for (const auto& f : all) {
        if (f.category == category) {
            out.push_back(f);
        }
    }
    return out;
}

stitch::StitchSequence effective(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    REQUIRE(sequence.has_value());
    return *sequence;
}

bool same_fix(const Fix& a, const Fix& b) {
    return a.kind == b.kind && a.delta == b.delta && a.value == b.value && a.text == b.text &&
           a.label == b.label;
}

bool same(const Finding& a, const Finding& b) {
    return a.severity == b.severity && a.category == b.category && a.message == b.message &&
           a.location == b.location && a.object == b.object && a.hint == b.hint &&
           same_fix(a.fix, b.fix);
}

document::Project text_project(std::int32_t cap_um) {
    document::Project project;
    document::TextObject text;
    text.id = project.object_ids.next();
    text.cap_height = Micrometers{cap_um};
    text.origin = um(1'000, 2'000);
    project.text_objects.push_back(text);
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.text_owner = text.id;
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::AutoSatinParams{};
    project.embroidery_objects.push_back(emb);
    return project;
}

} // namespace

TEST_CASE("analyze_project: hors-cadre regroupe par objet, delta minimal en um entiers") {
    document::Project project; // canevas 100 x 100 mm
    const stitch::BoundsUm hoop = canvas_hoop(project.canvas);
    stitch::StitchSequence seq;
    // Objet 7 : deux points a droite (x max 5 mm au-dela), un dedans. Objet 3 : en bas a gauche.
    // Points sans objet (source 0) : un constat global sans correction.
    const std::int32_t right = hoop.max.x.value;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{7}},
        {um(right + 5'000, 0), CommandType::Stitch, ObjectId{7}},
        {um(right + 2'000, 1'000), CommandType::Stitch, ObjectId{7}},
        {um(hoop.min.x.value - 400, hoop.min.y.value - 1'234), CommandType::Stitch, ObjectId{3}},
        {um(0, 2'000), CommandType::Stitch, ObjectId{3}},
        {um(right + 100, 0), CommandType::Stitch, ObjectId{}},
    };
    const AnalysisReport report = analyze_project(project, seq);
    const auto out = of_category(report.findings, "hors-cadre");
    REQUIRE(out.size() == 3);
    // Ordre d'identifiant croissant : global (0), puis 3, puis 7.
    CHECK(out[0].object.value == 0);
    CHECK(out[0].fix.kind == FixKind::None);
    CHECK(out[1].object.value == 3);
    CHECK(out[2].object.value == 7);
    for (const auto& f : out) {
        CHECK(f.severity == Severity::Error);
    }
    // Objet 3 : 1 point fautif, boite [min.x-400, 0] x [min.y-1234, 2000] -> delta (+400, +1234).
    CHECK(out[1].message == "1 point(s) hors du cadre de broderie.");
    CHECK(out[1].location == um(hoop.min.x.value - 400, hoop.min.y.value - 1'234));
    CHECK(out[1].fix.kind == FixKind::MoveIntoHoop);
    CHECK(out[1].fix.delta == um(400, 1'234));
    CHECK(out[1].fix.label == "Ramener dans le cadre");
    // Objet 7 : 2 points fautifs, premier = (right+5000, 0) ; delta (-5000, 0).
    CHECK(out[2].message == "2 point(s) hors du cadre de broderie.");
    CHECK(out[2].location == um(right + 5'000, 0));
    CHECK(out[2].fix.kind == FixKind::MoveIntoHoop);
    CHECK(out[2].fix.delta == um(-5'000, 0));
    // Les erreurs restent en tete de liste.
    CHECK(report.findings.front().severity == Severity::Error);
}

TEST_CASE("analyze_project: objet plus grand que le cadre, pas de correction") {
    document::Project project;
    const stitch::BoundsUm hoop = canvas_hoop(project.canvas);
    const std::int32_t wide = hoop.max.x.value - hoop.min.x.value + 1'000;
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{2}},
        {um(wide, 0), CommandType::Stitch, ObjectId{2}},
    };
    const auto out = of_category(analyze_project(project, seq).findings, "hors-cadre");
    REQUIRE(out.size() == 1);
    CHECK(out[0].fix.kind == FixKind::None);
    CHECK(out[0].hint == "Agrandissez le canevas.");
}

TEST_CASE("analyze_project: le plafond par categorie s'applique aux objets hors cadre") {
    document::Project project;
    const stitch::BoundsUm hoop = canvas_hoop(project.canvas);
    stitch::StitchSequence seq;
    for (std::uint64_t id = 1; id <= 60; ++id) {
        seq.commands.push_back(
            {um(hoop.max.x.value + 1'000, 0), CommandType::Stitch, ObjectId{id}});
    }
    const AnalysisReport report = analyze_project(project, seq);
    CHECK(of_category(report.findings, "hors-cadre").size() == 50);
    REQUIRE(report.suppressed.count("hors-cadre") == 1);
    CHECK(report.suppressed.at("hors-cadre") == 10);
}

TEST_CASE("analyze_project: le deplacement propose fait rentrer l'objet dans le cadre") {
    document::Project project;
    const stitch::BoundsUm hoop = canvas_hoop(project.canvas);
    const ObjectId id =
        add_square(project, hoop.max.x.value - 2'000, 0, 6'000, document::RunningStitchParams{});
    const auto before =
        of_category(analyze_project(project, effective(project)).findings, "hors-cadre");
    REQUIRE(before.size() == 1);
    REQUIRE(before[0].object == id);
    REQUIRE(before[0].fix.kind == FixKind::MoveIntoHoop);

    // Applique la translation au vecteur source (le bureau passe par une ICommand).
    for (auto& set : project.vector_objects.front().paths) {
        for (auto& node : set.outer.nodes) {
            node.pos = node.pos + before[0].fix.delta;
        }
    }
    const auto after =
        of_category(analyze_project(project, effective(project)).findings, "hors-cadre");
    CHECK(after.empty());
}

TEST_CASE("analyze_project: texte trop petit propose d'agrandir a 5 mm, et cela resout") {
    for (const std::int32_t cap : {2'500, 4'000}) {
        document::Project project = text_project(cap);
        const auto found = of_category(analyze_project(project, {}).findings, "texte-trop-petit");
        REQUIRE(found.size() == 1);
        CHECK(found[0].fix.kind == FixKind::EnlargeText);
        CHECK(found[0].fix.text == project.text_objects[0].id);
        CHECK(found[0].fix.value == document::kTextSatinMinCapHeight);
        CHECK(found[0].fix.value.value == 5'000);
        CHECK(found[0].fix.label == "Agrandir à 5 mm");

        project.text_objects[0].cap_height = found[0].fix.value;
        CHECK(of_category(analyze_project(project, {}).findings, "texte-trop-petit").empty());
    }
}

TEST_CASE("analyze_project: trait-trop-fin n'a pas de correction") {
    document::Project project = text_project(12'000);
    project.text_objects[0].fill = document::TextFill::Satin;
    project.embroidery_objects[0].params = document::RunningStitchParams{};
    const auto found = of_category(analyze_project(project, {}).findings, "trait-trop-fin");
    REQUIRE(found.size() == 1);
    CHECK(found[0].fix.kind == FixKind::None);
}

TEST_CASE("analyze_project: reglage hors limites propose la borne de la plage, et cela resout") {
    document::Project project;
    document::TatamiParams longFill;
    longFill.stitch_length = Micrometers{9'000};
    document::RunningStitchParams shortRun;
    shortRun.stitch_length = Micrometers{200};
    add_square(project, 0, 0, 5'000, longFill);
    add_square(project, 0, 0, 5'000, shortRun);

    const auto found = check_stitch_limits(project);
    REQUIRE(found.size() == 2);
    CHECK(found[0].fix.kind == FixKind::ClampStitchLength);
    CHECK(found[0].fix.value.value == 7'000);
    CHECK(found[0].fix.label == "Ramener à 7,0 mm");
    CHECK(found[1].fix.kind == FixKind::ClampStitchLength);
    CHECK(found[1].fix.value.value == 500);
    CHECK(found[1].fix.label == "Ramener à 0,5 mm");

    std::get<document::TatamiParams>(project.embroidery_objects[0].params).stitch_length =
        found[0].fix.value;
    std::get<document::RunningStitchParams>(project.embroidery_objects[1].params).stitch_length =
        found[1].fix.value;
    CHECK(check_stitch_limits(project).empty());
}

TEST_CASE("analyze_project: point-long propose le decoupage et cela resout") {
    document::Project project; // finitions activees, decoupage desactive
    document::RunningStitchParams run;
    run.stitch_length = Micrometers{9'000};
    add_square(project, 0, 0, 18'000, run);

    const AnalysisReport report = analyze_project(project, effective(project));
    const auto found = of_category(report.findings, "point-long");
    REQUIRE_FALSE(found.empty());
    for (const auto& f : found) {
        CHECK(f.fix.kind == FixKind::SplitLongStitches);
        CHECK(f.fix.label == "Découper les points longs");
    }

    project.finishing.split_long_stitches = true;
    const AnalysisReport after = analyze_project(project, effective(project));
    CHECK(of_category(after.findings, "point-long").empty());
}

TEST_CASE("analyze_project: point-long sans correction si le decoupage est deja actif") {
    document::Project project;
    project.finishing.split_long_stitches = true;
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(20'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto found = of_category(analyze_project(project, seq).findings, "point-long");
    REQUIRE(found.size() == 1);
    CHECK(found[0].fix.kind == FixKind::None);
}

TEST_CASE("analyze_project: point-long non decoupable (apres une coupe), pas de correction") {
    document::Project project;
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(1'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(1'000, 0), CommandType::Trim, ObjectId{1}},
        {um(20'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto found = of_category(analyze_project(project, seq).findings, "point-long");
    REQUIRE(found.size() == 1);
    CHECK(found[0].fix.kind == FixKind::None);
}

TEST_CASE("analyze_project: saut-sans-coupe propose les finitions et cela resout") {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    add_square(project, -30'000, 0, 5'000, document::RunningStitchParams{});
    add_square(project, 30'000, 0, 5'000, document::RunningStitchParams{});

    const AnalysisReport report = analyze_project(project, effective(project));
    const auto found = of_category(report.findings, "saut-sans-coupe");
    REQUIRE_FALSE(found.empty());
    for (const auto& f : found) {
        CHECK(f.fix.kind == FixKind::EnableFinishing);
        CHECK(f.fix.label == "Activer les finitions (coupes et points d'arrêt)");
    }

    project.finishing.enabled = true;
    const AnalysisReport after = analyze_project(project, effective(project));
    CHECK(of_category(after.findings, "saut-sans-coupe").empty());
}

TEST_CASE("analyze_project: pas de correction finitions si elles ne resolvent pas le constat") {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    const ObjectId first = add_square(project, -30'000, 0, 5'000, document::RunningStitchParams{});
    add_square(project, 30'000, 0, 5'000, document::RunningStitchParams{});
    // Connecteur « ne jamais couper » : activer les finitions laisserait le saut sans coupe.
    project.findEmbroidery(first)->connector = document::ConnectorMode::NeverTrim;

    const AnalysisReport report = analyze_project(project, effective(project));
    const auto found = of_category(report.findings, "saut-sans-coupe");
    REQUIRE_FALSE(found.empty());
    for (const auto& f : found) {
        CHECK(f.fix.kind == FixKind::None);
    }
}

TEST_CASE(
    "analyze_project: meme liste que l'assemblage manuel, dans l'ordre sequence-texte-reglages") {
    document::Project project = text_project(4'000);
    document::TatamiParams longFill;
    longFill.stitch_length = Micrometers{9'000};
    add_square(project, 0, 0, 5'000, longFill);
    add_square(project, 40'000, 0, 5'000, document::RunningStitchParams{}); // dans le cadre

    const stitch::StitchSequence seq = effective(project);
    AnalysisOptions options = options_from_project(project);
    options.hoop = canvas_hoop(project.canvas);
    AnalysisReport manual = analyze_detailed(seq, options);
    REQUIRE(of_category(manual.findings, "hors-cadre").empty());
    for (auto& f : analyze_text_objects(project)) {
        manual.findings.push_back(std::move(f));
    }
    for (auto& f : check_stitch_limits(project)) {
        manual.findings.push_back(std::move(f));
    }

    const AnalysisReport unified = analyze_project(project, seq);
    REQUIRE(unified.findings.size() == manual.findings.size());
    for (std::size_t i = 0; i < unified.findings.size(); ++i) {
        INFO(i);
        // Hors corrections (propres a analyze_project), les constats sont identiques.
        Finding a = unified.findings[i];
        Finding b = manual.findings[i];
        a.fix = {};
        b.fix = {};
        CHECK(same(a, b));
    }
    CHECK(unified.suppressed == manual.suppressed);
    CHECK_FALSE(of_category(unified.findings, "texte-trop-petit").empty());
    CHECK_FALSE(of_category(unified.findings, "parametre-hors-limites").empty());
    // Ordre : sequence, puis texte, puis reglages.
    const auto pos = [&](const char* category) {
        return std::find_if(unified.findings.begin(), unified.findings.end(),
                            [&](const Finding& f) { return f.category == category; }) -
               unified.findings.begin();
    };
    CHECK(pos("texte-trop-petit") < pos("parametre-hors-limites"));
}

TEST_CASE("analyze_project: deterministe") {
    document::Project project = text_project(2'000);
    add_square(project, 60'000, 0, 8'000, document::RunningStitchParams{});
    add_square(project, -60'000, 0, 8'000, document::RunningStitchParams{});
    const stitch::StitchSequence seq = effective(project);
    const AnalysisReport a = analyze_project(project, seq);
    const AnalysisReport b = analyze_project(project, seq);
    REQUIRE(a.findings.size() == b.findings.size());
    for (std::size_t i = 0; i < a.findings.size(); ++i) {
        CHECK(same(a.findings[i], b.findings[i]));
    }
    CHECK(a.suppressed == b.suppressed);
}

TEST_CASE("analyze_project: sequence vide, constat vide puis texte") {
    document::Project project = text_project(2'000);
    const AnalysisReport report = analyze_project(project, {});
    REQUIRE(report.findings.size() >= 2);
    CHECK(report.findings[0].category == "vide");
    CHECK(report.findings[1].category == "texte-trop-petit");
}
