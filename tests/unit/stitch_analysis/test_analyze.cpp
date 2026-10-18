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

} // namespace

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

TEST_CASE("point quasi nul detecte : avertissement par point sous 0,1 mm") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50, 0), CommandType::Stitch, ObjectId{1}}, // 0,05 mm < 0,1 mm
        {um(3'050, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto f = analyze(seq);
    CHECK(has_category(f, "point-court"));
    CHECK(count_category(f, "point-court") == 1);
    CHECK_FALSE(has_category(f, "points-courts"));
    for (const auto& x : f) {
        if (x.category == "point-court") {
            CHECK(x.severity == Severity::Warning);
        }
    }
}

TEST_CASE("frontiere du point quasi nul : 99 um avertit, 100 um informe") {
    stitch::StitchSequence below;
    below.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(99, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'099, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto fb = analyze(below);
    CHECK(count_category(fb, "point-court") == 1);
    CHECK_FALSE(has_category(fb, "points-courts"));

    stitch::StitchSequence atLimit;
    atLimit.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(100, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'100, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto fa = analyze(atLimit);
    CHECK_FALSE(has_category(fa, "point-court"));
    CHECK(count_category(fa, "points-courts") == 1);
}

TEST_CASE("points courts de 0,1 a 0,5 mm : un constat d'information par objet") {
    stitch::StitchSequence seq;
    seq.commands = {
        // Objet 1 : deux points courts (0,2 mm puis 0,1 mm), puis un point normal.
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(200, 0), CommandType::Stitch, ObjectId{1}},
        {um(300, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'300, 0), CommandType::Stitch, ObjectId{1}},
        // Objet 2 : un point court (0,3 mm).
        {um(20'000, 0), CommandType::Jump, ObjectId{2}},
        {um(20'000, 0), CommandType::Stitch, ObjectId{2}},
        {um(20'300, 0), CommandType::Stitch, ObjectId{2}},
        {um(23'300, 0), CommandType::Stitch, ObjectId{2}},
    };
    const auto f = analyze(seq);
    CHECK_FALSE(has_category(f, "point-court"));
    std::vector<Finding> groups;
    for (const auto& x : f) {
        if (x.category == "points-courts") {
            groups.push_back(x);
        }
    }
    REQUIRE(groups.size() == 2);
    // Ordre d'identifiant d'objet, gravité « information », premier point fautif localisé.
    CHECK(groups[0].severity == Severity::Info);
    CHECK(groups[0].object == ObjectId{1});
    CHECK(groups[0].location == um(200, 0));
    CHECK(groups[0].message.find("2 point(s)") != std::string::npos);
    CHECK(groups[0].message.find("le plus court : 0,1 mm") != std::string::npos);
    CHECK(groups[1].severity == Severity::Info);
    CHECK(groups[1].object == ObjectId{2});
    CHECK(groups[1].location == um(20'300, 0));
    CHECK(groups[1].message.find("1 point(s)") != std::string::npos);
}

TEST_CASE("point de 0,5 mm ou plus : aucun constat de point court") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(500, 0), CommandType::Stitch, ObjectId{1}}, // exactement 0,5 mm : pas trop court
        {um(3'500, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto f = analyze(seq);
    CHECK_FALSE(has_category(f, "point-court"));
    CHECK_FALSE(has_category(f, "points-courts"));
}

TEST_CASE("point trop long detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'000, 0), CommandType::Stitch, ObjectId{1}}, // 9 mm > 7 mm
    };
    CHECK(has_category(analyze(seq), "point-long"));
}

TEST_CASE("saut trop long detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}}, // 50 mm > 30 mm
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

TEST_CASE("saut long sans coupe dans le deplacement : un constat par saut") {
    stitch::StitchSequence one;
    one.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}}, // 50 mm, aucune coupe
        {um(50'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK(count_category(analyze(one), "saut-long") == 1);

    stitch::StitchSequence two;
    two.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(40'000, 0), CommandType::Jump, ObjectId{1}},
        {um(80'000, 0), CommandType::Jump, ObjectId{1}}, // deux sauts de 40 mm, aucune coupe
        {um(80'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK(count_category(analyze(two), "saut-long") == 2);
}

TEST_CASE("saut long precede d'une coupe : pas de constat (message faux sinon)") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(0, 0), CommandType::Trim, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}}, // 50 mm mais coupe avant le saut
        {um(50'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK_FALSE(has_category(analyze(seq), "saut-long"));
}

TEST_CASE("saut long suivi d'une coupe avant la reprise : pas de constat") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}},
        {um(50'000, 0), CommandType::Trim, ObjectId{1}}, // coupe apres le saut, meme deplacement
        {um(50'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK_FALSE(has_category(analyze(seq), "saut-long"));
}

TEST_CASE("deux sauts longs et une seule coupe dans le deplacement : aucun constat") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(40'000, 0), CommandType::Jump, ObjectId{1}},
        {um(40'000, 0), CommandType::Trim, ObjectId{1}},
        {um(80'000, 0), CommandType::Jump, ObjectId{1}},
        {um(80'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK_FALSE(has_category(analyze(seq), "saut-long"));
}

TEST_CASE("une coupe d'un deplacement precedent n'excuse pas un saut long sans coupe") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(0, 0), CommandType::Trim, ObjectId{1}},
        {um(1'000, 0), CommandType::Jump, ObjectId{1}}, // premier deplacement : court, coupe
        {um(1'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(60'000, 0), CommandType::Jump, ObjectId{1}}, // second deplacement : 59 mm, aucune coupe
        {um(60'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK(count_category(analyze(seq), "saut-long") == 1);
}

TEST_CASE("saut long en fin de sequence : signale sans coupe, silencieux avec coupe") {
    stitch::StitchSequence bare;
    bare.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}}, // aucun point cousu apres
    };
    CHECK(count_category(analyze(bare), "saut-long") == 1);

    stitch::StitchSequence trimmed;
    trimmed.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(0, 0), CommandType::Trim, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}},
    };
    CHECK_FALSE(has_category(analyze(trimmed), "saut-long"));
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
        {um(51'400, 4'227), CommandType::Jump, ObjectId{1}},   // ~51,4 mm, cf. export debug
        {um(51'400, 4'227), CommandType::Stitch, ObjectId{1}}, // atterrissage : distance nulle
        {um(52'400, 4'227), CommandType::Stitch, ObjectId{1}}, // reprise normale (1 mm)
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
        {um(60'000, 0), CommandType::Stitch, ObjectId{1}}, // hors d'un cadre 100x100 centre
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
    // 200 points quasi nuls d'affilee (0,05 mm : sous le seuil d'avertissement par point).
    for (int i = 0; i < 200; ++i) {
        seq.commands.push_back({um(i * 50, 0), CommandType::Stitch, ObjectId{1}});
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

TEST_CASE("messages d'analyse : virgule decimale francaise et indice de correction") {
    CHECK(format_mm_fr(3'500.0) == "3,5");
    CHECK(format_mm_fr(51'400.0) == "51,4");
    CHECK(format_mm_fr(200.0) == "0,2");
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'500, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto f = analyze(seq);
    REQUIRE(f.size() == 1);
    CHECK(f[0].message.find("9,5 mm") != std::string::npos);
    CHECK(f[0].message.find("9.5") == std::string::npos);
    CHECK_FALSE(f[0].hint.empty());
}

TEST_CASE("plafond par categorie : le nombre masque est rapporte") {
    stitch::StitchSequence seq;
    for (int i = 0; i < 20; ++i) {
        seq.commands.push_back({um(i * 10'000, 0), CommandType::Stitch, ObjectId{1}});
    }
    AnalysisOptions opts;
    opts.max_findings_per_category = 5;
    const auto r = analyze_detailed(seq, opts);
    CHECK(count_category(r.findings, "point-long") == 5);
    REQUIRE(r.suppressed.count("point-long") == 1);
    CHECK(r.suppressed.at("point-long") == 14);
}

namespace {

// Remplit le rectangle [x0 ; x1] x [y0 ; y1] (µm) de rangées en aller-retour
// espacées de `spacing`, pour l'objet `id` et la passe `pass`, puis lève
// l'aiguille (saut) pour que deux blocs ne se rejoignent pas.
void add_block(stitch::StitchSequence& seq, std::uint64_t id, stitch::StitchPass pass,
               std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
               std::int32_t spacing) {
    bool forward = true;
    for (std::int32_t y = y0; y <= y1; y += spacing) {
        const std::int32_t from = forward ? x0 : x1;
        const std::int32_t to = forward ? x1 : x0;
        seq.commands.push_back({um(from, y), CommandType::Stitch, ObjectId{id}, pass});
        seq.commands.push_back({um(to, y), CommandType::Stitch, ObjectId{id}, pass});
        forward = !forward;
    }
    seq.commands.push_back({um(x0, y0), CommandType::Jump, ObjectId{id}, pass});
}

// Un objet « normal » : sous-couche espacée de 2 mm + dessus à 0,4 mm.
void add_object(stitch::StitchSequence& seq, std::uint64_t id, std::int32_t x0, std::int32_t y0,
                std::int32_t x1, std::int32_t y1) {
    add_block(seq, id, stitch::StitchPass::Underlay, x0, y0, x1, y1, 2'000);
    add_block(seq, id, stitch::StitchPass::TopStitch, x0, y0, x1, y1, 400);
}

} // namespace

TEST_CASE("couches : un objet seul (sous-couche + dessus) n'est pas signale") {
    stitch::StitchSequence seq;
    add_object(seq, 1, 0, 0, 20'000, 20'000);
    CHECK_FALSE(has_category(analyze(seq), "couches-superposees"));
}

TEST_CASE("couches : deux objets superposes restent sous le seuil par defaut") {
    // 2 remplissages denses + leurs sous-couches ~ 2,4 couches équivalentes < 3.
    stitch::StitchSequence seq;
    add_object(seq, 1, 0, 0, 20'000, 20'000);
    add_object(seq, 2, 5'000, 5'000, 25'000, 25'000);
    CHECK_FALSE(has_category(analyze(seq), "couches-superposees"));
}

TEST_CASE("couches : trois objets superposes sont signales une fois") {
    stitch::StitchSequence seq;
    add_object(seq, 1, 0, 0, 20'000, 20'000);
    add_object(seq, 2, 5'000, 5'000, 25'000, 25'000);
    add_object(seq, 3, 5'000, 5'000, 20'000, 20'000); // 3 couches sur 15 x 15 mm
    const auto f = analyze(seq);
    REQUIRE(count_category(f, "couches-superposees") == 1);
    const auto hit = std::find_if(
        f.begin(), f.end(), [](const Finding& x) { return x.category == "couches-superposees"; });
    CHECK(hit->severity == Severity::Warning);
    CHECK(hit->message.find("couches de remplissage dense") != std::string::npos);
    // Le point signalé est dans la zone de recouvrement.
    CHECK(hit->location.x.value >= 5'000);
    CHECK(hit->location.x.value <= 20'000);
    CHECK(hit->location.y.value >= 5'000);
    CHECK(hit->location.y.value <= 20'000);
    CHECK((hit->object == ObjectId{1} || hit->object == ObjectId{2} || hit->object == ObjectId{3}));
}

TEST_CASE("couches : deux objets voisins ou a peine empietants ne sont pas signales") {
    // Mesuré au développement : un recouvrement de 1 mm entre deux remplissages
    // (pratique courante contre les interstices) ne doit pas déclencher la règle.
    for (const std::int32_t overlap : {-300, 0, 1'000}) {
        stitch::StitchSequence seq;
        add_object(seq, 1, 0, 0, 10'000, 20'000);
        add_object(seq, 2, 10'000 - overlap, 0, 20'000 - overlap, 20'000);
        INFO("recouvrement " << overlap << " um");
        CHECK_FALSE(has_category(analyze(seq), "couches-superposees"));
    }
}

TEST_CASE("couches : le seuil est reglable et la regle desactivable") {
    stitch::StitchSequence seq;
    add_object(seq, 1, 0, 0, 20'000, 20'000);
    add_object(seq, 2, 0, 0, 20'000, 20'000);
    CHECK_FALSE(has_category(analyze(seq), "couches-superposees")); // ~2,4 < 3
    AnalysisOptions strict;
    strict.max_layer_thickness = 2.0;
    CHECK(has_category(analyze(seq, strict), "couches-superposees"));

    add_object(seq, 3, 0, 0, 20'000, 20'000);
    CHECK(has_category(analyze(seq), "couches-superposees"));
    AnalysisOptions off;
    off.max_layer_thickness = 0.0;
    CHECK_FALSE(has_category(analyze(seq, off), "couches-superposees"));
}

TEST_CASE("couches : les points d'arret et les deplacements ne comptent pas") {
    stitch::StitchSequence seq;
    add_object(seq, 1, 0, 0, 20'000, 20'000);
    add_block(seq, 2, stitch::StitchPass::Lock, 0, 0, 20'000, 20'000, 400);
    add_block(seq, 3, stitch::StitchPass::Travel, 0, 0, 20'000, 20'000, 400);
    add_block(seq, 4, stitch::StitchPass::Lock, 0, 0, 20'000, 20'000, 400);
    CHECK_FALSE(has_category(analyze(seq), "couches-superposees"));
}

TEST_CASE("couches : meme sequence, meme resultat") {
    stitch::StitchSequence seq;
    add_object(seq, 1, 0, 0, 20'000, 20'000);
    add_object(seq, 2, 5'000, 5'000, 25'000, 25'000);
    add_object(seq, 3, 5'000, 5'000, 20'000, 20'000);
    const auto a = analyze(seq);
    const auto b = analyze(seq);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].category == b[i].category);
        CHECK(a[i].message == b[i].message);
        CHECK(a[i].location == b[i].location);
    }
}
