// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

using CmdType = stitch::CommandType;
using Pass = stitch::StitchPass;

stitch::StitchCommand mk(std::int32_t x, std::int32_t y, CmdType type, ObjectId source,
                         Pass pass = Pass::TopStitch) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, type, source, pass};
}

}  // namespace

// ---------------------------------------------------------------------------
// fingerprint
// ---------------------------------------------------------------------------

TEST_CASE("fingerprint : stable pour une vue brute identique") {
    const ObjectId o{1};
    const std::vector<stitch::StitchCommand> raw = {
        mk(0, 0, CmdType::Stitch, o),
        mk(1'000, 0, CmdType::Stitch, o),
        mk(1'000, 1'000, CmdType::Stitch, o),
    };
    const auto copy = raw;
    CHECK(fingerprint(raw) == fingerprint(copy));
}

TEST_CASE("fingerprint : sensible a la position d'un seul point") {
    const ObjectId o{1};
    std::vector<stitch::StitchCommand> a = {mk(0, 0, CmdType::Stitch, o), mk(1'000, 0, CmdType::Stitch, o)};
    auto b = a;
    b[1].pos.x = Micrometers{1'001};
    CHECK(fingerprint(a) != fingerprint(b));
}

TEST_CASE("fingerprint : sensible au type de commande") {
    const ObjectId o{1};
    std::vector<stitch::StitchCommand> a = {mk(0, 0, CmdType::Stitch, o), mk(1'000, 0, CmdType::Stitch, o)};
    auto b = a;
    b[1].type = CmdType::Jump;
    CHECK(fingerprint(a) != fingerprint(b));
}

TEST_CASE("fingerprint : sensible a la passe") {
    const ObjectId o{1};
    std::vector<stitch::StitchCommand> a = {mk(0, 0, CmdType::Stitch, o), mk(1'000, 0, CmdType::Stitch, o)};
    auto b = a;
    b[1].pass = Pass::Underlay;
    CHECK(fingerprint(a) != fingerprint(b));
}

TEST_CASE("fingerprint : sensible a l'ordre des points") {
    const ObjectId o{1};
    const auto p0 = mk(0, 0, CmdType::Stitch, o);
    const auto p1 = mk(1'000, 500, CmdType::Stitch, o);
    const std::vector<stitch::StitchCommand> a = {p0, p1};
    const std::vector<stitch::StitchCommand> b = {p1, p0};
    CHECK(fingerprint(a) != fingerprint(b));
}

// ---------------------------------------------------------------------------
// raw_slice
// ---------------------------------------------------------------------------

TEST_CASE("raw_slice : sous-sequence contigue d'un objet") {
    const ObjectId a{1};
    const ObjectId b{2};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, a, Pass::Travel),
        mk(0, 0, CmdType::Stitch, a, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, a, Pass::TopStitch),
        mk(1'000, 0, CmdType::Jump, b, Pass::Travel),
        mk(1'000, 0, CmdType::Stitch, b, Pass::TopStitch),
    };
    const auto ra = raw_slice(seq, a);
    REQUIRE(ra.size() == 3);
    CHECK(ra[0] == seq.commands[0]);
    CHECK(ra[1] == seq.commands[1]);
    CHECK(ra[2] == seq.commands[2]);

    const auto rb = raw_slice(seq, b);
    REQUIRE(rb.size() == 2);
}

TEST_CASE("raw_slice : reconstitue un objet meme entrelace avec un autre") {
    // Construction synthetique : le generateur actuel garde chaque objet
    // contigu (verifie dans le test d'integration satin ci-dessous), mais
    // raw_slice doit rester correcte independamment de cette propriete du
    // generateur -- c'est un simple filtre par source, insensible a l'ordre
    // relatif des objets.
    const ObjectId a{1};
    const ObjectId b{2};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Stitch, a),
        mk(10, 0, CmdType::Stitch, b),
        mk(20, 0, CmdType::Stitch, a),
        mk(30, 0, CmdType::Stitch, b),
        mk(40, 0, CmdType::Stitch, a),
    };
    const auto ra = raw_slice(seq, a);
    REQUIRE(ra.size() == 3);
    CHECK(ra[0].pos.x.value == 0);
    CHECK(ra[1].pos.x.value == 20);
    CHECK(ra[2].pos.x.value == 40);

    const auto rb = raw_slice(seq, b);
    REQUIRE(rb.size() == 2);
    CHECK(rb[0].pos.x.value == 10);
    CHECK(rb[1].pos.x.value == 30);
}

namespace {
// Deux colonnes satin adjacentes, memes couleur/source (satin ignore
// source_vector), pour verifier l'isolation par `raw_slice` a travers un
// routage reel (routing.cpp), y compris si un trajet cache (Travel) relie
// les deux colonnes.
document::SatinParams straight_column(std::int32_t x0, std::int32_t x1) {
    document::SatinParams sp;
    sp.rail_a.closed = false;
    sp.rail_a.nodes = {{Vec2um{Micrometers{x0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
                       {Vec2um{Micrometers{x1}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
    sp.rail_b.closed = false;
    sp.rail_b.nodes = {{Vec2um{Micrometers{x0}, Micrometers{3'000}}, geometry::NodeType::Corner, {}, {}},
                       {Vec2um{Micrometers{x1}, Micrometers{3'000}}, geometry::NodeType::Corner, {}, {}}};
    sp.rungs = {{Vec2um{Micrometers{x0}, Micrometers{0}}, Vec2um{Micrometers{x0}, Micrometers{3'000}}},
                {Vec2um{Micrometers{x1}, Micrometers{0}}, Vec2um{Micrometers{x1}, Micrometers{3'000}}}};
    return sp;
}
}  // namespace

TEST_CASE("raw_slice : isole chaque colonne satin routee, meme avec un trajet cache") {
    document::Project project;
    document::EmbroideryObject a;
    a.id = project.object_ids.next();
    a.params = straight_column(0, 10'000);
    document::EmbroideryObject b;
    b.id = project.object_ids.next();
    b.params = straight_column(14'000, 24'000);  // ecart 4mm < underpath_max (8mm)
    project.embroidery_objects = {a, b};

    const auto seq = generate_sequence(project);
    REQUIRE(seq.has_value());

    const auto ra = raw_slice(*seq, a.id);
    const auto rb = raw_slice(*seq, b.id);
    CHECK_FALSE(ra.empty());
    CHECK_FALSE(rb.empty());
    // Toutes les commandes sauf `End` (source nulle) appartiennent a l'une
    // des deux colonnes -- aucune commande orpheline, aucun chevauchement
    // (raw_slice filtre par egalite stricte de `source`).
    CHECK(ra.size() + rb.size() == seq->commands.size() - 1);
}

// ---------------------------------------------------------------------------
// apply_manual_overrides
// ---------------------------------------------------------------------------

namespace {

// Sequence a 4 commandes pour un seul objet : Jump (Travel) puis 3 Stitch
// (TopStitch) aux index bruts 1, 2, 3.
stitch::StitchSequence make_single_object_sequence(ObjectId oid) {
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, oid, Pass::Travel),
        mk(0, 0, CmdType::Stitch, oid, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oid, Pass::TopStitch),
        mk(1'000, 1'000, CmdType::Stitch, oid, Pass::TopStitch),
    };
    return seq;
}

// Place l'objet en etat ManuallyEdited : capture le fingerprint/compteur de la
// vue brute ACTUELLE avant d'ajouter les overrides fournis (transition 1, §1).
void make_manually_edited(document::Project& project, document::EmbroideryObject& obj,
                          const stitch::StitchSequence& seq,
                          std::vector<document::StitchOverride> overrides) {
    const auto raw = raw_slice(seq, obj.id);
    obj.edited_point_count = static_cast<std::uint32_t>(raw.size());
    obj.edited_fingerprint = fingerprint(raw);
    obj.overrides = std::move(overrides);
    project.embroidery_objects.push_back(obj);
}

}  // namespace

TEST_CASE("apply_manual_overrides : deplace un point Stitch cible") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);

    document::StitchOverride ov;
    ov.base_index = 2;  // (1000, 0)
    ov.moved_to = Vec2um{Micrometers{1'500}, Micrometers{500}};
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[2].pos == (Vec2um{Micrometers{1'500}, Micrometers{500}}));
    CHECK(seq.commands[2].pass == Pass::Manual);
    CHECK(seq.commands[2].type == CmdType::Stitch);
    // Points non cibles : inchanges.
    CHECK(seq.commands[1].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));
    CHECK(seq.commands[3].pos == (Vec2um{Micrometers{1'000}, Micrometers{1'000}}));
}

TEST_CASE("apply_manual_overrides : convertit Stitch en Jump") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.forced_type = document::StitchPointType::Jump;
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[1].type == CmdType::Jump);
    CHECK(seq.commands[1].pass == Pass::Manual);
    CHECK(seq.commands[1].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));  // position inchangee
}

TEST_CASE("apply_manual_overrides : insere un Trim exactement apres le point cible") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);
    const std::size_t sizeBefore = seq.commands.size();

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.trim_after = true;
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    REQUIRE(seq.commands.size() == sizeBefore + 1);
    CHECK(seq.commands[1].type == CmdType::Stitch);  // point cible inchange
    CHECK(seq.commands[2].type == CmdType::Trim);    // Trim juste apres
    CHECK(seq.commands[2].pos == seq.commands[1].pos);
    CHECK(seq.commands[2].pass == Pass::Manual);
    CHECK(seq.commands[2].source == oid);
}

TEST_CASE("apply_manual_overrides : trim apres un deplacement utilise la position finale") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);

    document::StitchOverride ov;
    ov.base_index = 3;
    ov.moved_to = Vec2um{Micrometers{2'000}, Micrometers{2'000}};
    ov.trim_after = true;
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    REQUIRE(seq.commands.size() == make_single_object_sequence(oid).commands.size() + 1);
    CHECK(seq.commands[3].pos == (Vec2um{Micrometers{2'000}, Micrometers{2'000}}));
    CHECK(seq.commands[4].type == CmdType::Trim);
    CHECK(seq.commands[4].pos == (Vec2um{Micrometers{2'000}, Micrometers{2'000}}));
}

TEST_CASE("apply_manual_overrides : plusieurs overrides, une insertion de Trim ne decale pas les autres index") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);

    document::StitchOverride moveOv;
    moveOv.base_index = 1;
    moveOv.moved_to = Vec2um{Micrometers{-500}, Micrometers{-500}};
    document::StitchOverride trimOv;
    trimOv.base_index = 3;
    trimOv.trim_after = true;
    make_manually_edited(project, obj, seq, {moveOv, trimOv});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[1].pos == (Vec2um{Micrometers{-500}, Micrometers{-500}}));
    CHECK(seq.commands[1].pass == Pass::Manual);
    REQUIRE(seq.commands.size() == 5);
    CHECK(seq.commands[4].type == CmdType::Trim);
}

TEST_CASE("apply_manual_overrides : Dirty par empreinte -- sequence non patchee") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.moved_to = Vec2um{Micrometers{9'999}, Micrometers{9'999}};
    make_manually_edited(project, obj, seq, {ov});
    project.embroidery_objects[0].edited_fingerprint += 1;  // corrompt l'empreinte

    const auto original = seq.commands;
    const auto dirty = apply_manual_overrides(seq, project);
    REQUIRE(dirty.size() == 1);
    CHECK(dirty[0] == oid);
    CHECK(seq.commands == original);  // aucune retouche appliquee, aucune perte
}

TEST_CASE("apply_manual_overrides : Dirty par compteur de points, meme si l'empreinte semble correcte") {
    const ObjectId oid{1};
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    auto seq = make_single_object_sequence(oid);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.trim_after = true;
    make_manually_edited(project, obj, seq, {ov});
    // Empreinte laissee correcte, mais compteur volontairement faux : verifie
    // que le controle de longueur est independant du hachage (cf. cadrage §1).
    project.embroidery_objects[0].edited_point_count += 1;

    const auto original = seq.commands;
    const auto dirty = apply_manual_overrides(seq, project);
    REQUIRE(dirty.size() == 1);
    CHECK(dirty[0] == oid);
    CHECK(seq.commands == original);
}

TEST_CASE("apply_manual_overrides : isolation entre deux objets retouches") {
    const ObjectId oidA{1};
    const ObjectId oidB{2};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Stitch, oidA, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oidA, Pass::TopStitch),
        mk(2'000, 0, CmdType::Stitch, oidB, Pass::TopStitch),
        mk(3'000, 0, CmdType::Stitch, oidB, Pass::TopStitch),
    };

    document::Project project;
    document::EmbroideryObject a;
    a.id = oidA;
    document::StitchOverride ovA;
    ovA.base_index = 0;
    ovA.moved_to = Vec2um{Micrometers{111}, Micrometers{111}};
    make_manually_edited(project, a, seq, {ovA});

    document::EmbroideryObject b;
    b.id = oidB;
    document::StitchOverride ovB;
    ovB.base_index = 1;  // deuxieme entree de B (indice brut 3 dans seq)
    ovB.moved_to = Vec2um{Micrometers{222}, Micrometers{222}};
    make_manually_edited(project, b, seq, {ovB});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[0].pos == (Vec2um{Micrometers{111}, Micrometers{111}}));
    CHECK(seq.commands[1].pos == (Vec2um{Micrometers{1'000}, Micrometers{0}}));  // A non cible : intact
    CHECK(seq.commands[2].pos == (Vec2um{Micrometers{2'000}, Micrometers{0}}));  // B non cible : intact
    CHECK(seq.commands[3].pos == (Vec2um{Micrometers{222}, Micrometers{222}}));
}

TEST_CASE("apply_manual_overrides : cible hors TopStitch (Underlay) refusee") {
    const ObjectId oid{1};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Stitch, oid, Pass::Underlay),  // base_index 0 : ineligible
        mk(1'000, 0, CmdType::Stitch, oid, Pass::TopStitch),
    };
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 0;
    ov.moved_to = Vec2um{Micrometers{9'999}, Micrometers{9'999}};
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());  // toujours ManuallyEdited : l'empreinte/compteur correspondent
    CHECK(seq.commands[0].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));  // override ignore
    CHECK(seq.commands[0].pass == Pass::Underlay);
}

TEST_CASE("apply_manual_overrides : deplacer une cible Jump en passe TopStitch refuse") {
    // `moved_to` reste reserve aux points Stitch (cadrage Lot 8 SS2.1) : un
    // saut n'a pas de position a "deplacer" independamment de ses voisins de
    // trajet. Cas de bord qui n'est pas produit par le generateur actuel
    // (une entree TopStitch y est toujours un Stitch), mais l'eligibilite de
    // `moved_to` doit rester stricte sur (pass ET type), pas seulement la
    // passe.
    const ObjectId oid{1};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, oid, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oid, Pass::TopStitch),
    };
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 0;
    ov.moved_to = Vec2um{Micrometers{9'999}, Micrometers{9'999}};
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[0].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));  // override ignore
}

TEST_CASE("apply_manual_overrides : convertit Jump en Stitch (direction inverse)") {
    // Regression : `is_overridable_entry` n'acceptait auparavant qu'une cible
    // de type Stitch, rendant Jump->Stitch impossible alors que le cadrage
    // (SS2.2) et le rapport de Lot 8.0 annoncent explicitement une transition
    // bidirectionnelle Stitch<->Jump.
    const ObjectId oid{1};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, oid, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oid, Pass::TopStitch),
    };
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 0;
    ov.forced_type = document::StitchPointType::Stitch;
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[0].type == CmdType::Stitch);
    CHECK(seq.commands[0].pass == Pass::Manual);
    CHECK(seq.commands[0].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));  // position inchangee
}

TEST_CASE("apply_manual_overrides : trim apres une cible Jump accepte") {
    // `trim_after` n'est pas restreint aux points Stitch (cadrage SS2.3) : un
    // Trim est une commande machine independante de la geometrie du point.
    const ObjectId oid{1};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, oid, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oid, Pass::TopStitch),
    };
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 0;
    ov.trim_after = true;
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    REQUIRE(seq.commands.size() == 3);
    CHECK(seq.commands[0].type == CmdType::Jump);  // point cible inchange
    CHECK(seq.commands[1].type == CmdType::Trim);
    CHECK(seq.commands[1].pos == seq.commands[0].pos);
}

TEST_CASE("apply_manual_overrides : override combine sur cible Jump -- moved_to ignore, forced_type applique") {
    // Validation par champ (cadrage : "un override combinant plusieurs champs
    // doit valider chaque champ separement, pas rejeter tout le bloc selon
    // une regle unique trop restrictive") : sur une cible Jump, `moved_to`
    // est invalide (reserve a Stitch) mais `forced_type` (Jump->Stitch) et
    // `trim_after` restent valides dans le meme override.
    const ObjectId oid{1};
    stitch::StitchSequence seq;
    seq.commands = {
        mk(0, 0, CmdType::Jump, oid, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oid, Pass::TopStitch),
    };
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 0;
    ov.moved_to = Vec2um{Micrometers{9'999}, Micrometers{9'999}};  // invalide sur cible Jump
    ov.forced_type = document::StitchPointType::Stitch;             // valide : Jump->Stitch
    ov.trim_after = true;                                           // valide sur cible Jump/Stitch
    make_manually_edited(project, obj, seq, {ov});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands[0].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));  // moved_to ignore
    CHECK(seq.commands[0].type == CmdType::Stitch);                          // forced_type applique
    CHECK(seq.commands[0].pass == Pass::Manual);
    REQUIRE(seq.commands.size() == 3);
    CHECK(seq.commands[1].type == CmdType::Trim);  // trim_after applique
    CHECK(seq.commands[1].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));
}

TEST_CASE("apply_manual_overrides : index invalide ignore sans plantage") {
    const ObjectId oid{1};
    auto seq = make_single_object_sequence(oid);
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 999;  // hors bornes de raw_slice (taille 4)
    ov.moved_to = Vec2um{Micrometers{1}, Micrometers{1}};
    make_manually_edited(project, obj, seq, {ov});

    const auto original = seq.commands;
    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands == original);
}

TEST_CASE("apply_manual_overrides : override vide -- aucun effet") {
    const ObjectId oid{1};
    auto seq = make_single_object_sequence(oid);
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    document::StitchOverride ov;
    ov.base_index = 1;  // moved_to/forced_type nullopt, trim_after false
    make_manually_edited(project, obj, seq, {ov});

    const auto original = seq.commands;
    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands == original);
}

TEST_CASE("apply_manual_overrides : doublon d'overrides -- la derniere entree du vecteur l'emporte en bloc") {
    const ObjectId oid{1};
    auto seq = make_single_object_sequence(oid);
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;

    document::StitchOverride first;
    first.base_index = 1;
    first.moved_to = Vec2um{Micrometers{5'000}, Micrometers{5'000}};
    document::StitchOverride second;
    second.base_index = 1;
    second.forced_type = document::StitchPointType::Jump;  // moved_to nullopt ici
    make_manually_edited(project, obj, seq, {first, second});

    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    // La derniere entree l'emporte EN BLOC (pas de fusion champ a champ) :
    // le deplacement de `first` est perdu, seule la conversion de `second`
    // s'applique.
    CHECK(seq.commands[1].type == CmdType::Jump);
    CHECK(seq.commands[1].pos == (Vec2um{Micrometers{0}, Micrometers{0}}));
}

TEST_CASE("apply_manual_overrides : n'a aucun effet observable si overrides est vide") {
    const ObjectId oid{1};
    auto seq = make_single_object_sequence(oid);
    document::Project project;
    document::EmbroideryObject obj;
    obj.id = oid;
    project.embroidery_objects.push_back(obj);  // overrides vide (Clean)

    const auto original = seq.commands;
    const auto dirty = apply_manual_overrides(seq, project);
    CHECK(dirty.empty());
    CHECK(seq.commands == original);
}

TEST_CASE("apply_manual_overrides : deterministe sur deux copies independantes") {
    const ObjectId oidA{1};
    const ObjectId oidB{2};
    stitch::StitchSequence seqTemplate;
    seqTemplate.commands = {
        mk(0, 0, CmdType::Stitch, oidA, Pass::TopStitch),
        mk(1'000, 0, CmdType::Stitch, oidA, Pass::TopStitch),
        mk(2'000, 0, CmdType::Stitch, oidB, Pass::TopStitch),
    };

    document::Project project;
    document::EmbroideryObject a;
    a.id = oidA;
    document::StitchOverride ovA;
    ovA.base_index = 1;
    ovA.trim_after = true;
    make_manually_edited(project, a, seqTemplate, {ovA});

    document::EmbroideryObject b;
    b.id = oidB;
    // Objet B volontairement Dirty (empreinte fausse).
    document::StitchOverride ovB;
    ovB.base_index = 0;
    ovB.moved_to = Vec2um{Micrometers{1}, Micrometers{1}};
    make_manually_edited(project, b, seqTemplate, {ovB});
    project.embroidery_objects[1].edited_fingerprint += 1;

    auto seq1 = seqTemplate;
    auto seq2 = seqTemplate;
    const auto dirty1 = apply_manual_overrides(seq1, project);
    const auto dirty2 = apply_manual_overrides(seq2, project);

    CHECK(seq1.commands == seq2.commands);
    REQUIRE(dirty1.size() == 1);
    REQUIRE(dirty2.size() == 1);
    CHECK(dirty1[0] == oidB);
    CHECK(dirty1 == dirty2);
}

// ---------------------------------------------------------------------------
// classify_edit_state
// ---------------------------------------------------------------------------

TEST_CASE("classify_edit_state : Clean quand overrides est vide") {
    document::EmbroideryObject obj;
    obj.id = ObjectId{1};
    const std::vector<stitch::StitchCommand> raw = {mk(0, 0, CmdType::Stitch, obj.id)};
    CHECK(classify_edit_state(obj, raw) == ObjectEditState::Clean);
}

TEST_CASE("classify_edit_state : ManuallyEdited quand empreinte et compteur correspondent") {
    document::EmbroideryObject obj;
    obj.id = ObjectId{1};
    const std::vector<stitch::StitchCommand> raw = {mk(0, 0, CmdType::Stitch, obj.id),
                                                     mk(1'000, 0, CmdType::Stitch, obj.id)};
    obj.edited_point_count = static_cast<std::uint32_t>(raw.size());
    obj.edited_fingerprint = fingerprint(raw);
    obj.overrides.push_back(document::StitchOverride{});
    CHECK(classify_edit_state(obj, raw) == ObjectEditState::ManuallyEdited);
}

TEST_CASE("classify_edit_state : Dirty quand la vue brute a change") {
    document::EmbroideryObject obj;
    obj.id = ObjectId{1};
    const std::vector<stitch::StitchCommand> raw = {mk(0, 0, CmdType::Stitch, obj.id)};
    obj.edited_point_count = static_cast<std::uint32_t>(raw.size());
    obj.edited_fingerprint = fingerprint(raw);
    obj.overrides.push_back(document::StitchOverride{});

    const std::vector<stitch::StitchCommand> changed = {mk(1, 0, CmdType::Stitch, obj.id)};
    CHECK(classify_edit_state(obj, changed) == ObjectEditState::Dirty);
}
