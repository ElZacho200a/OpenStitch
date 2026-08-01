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

// ---------------------------------------------------------------------------
// effective_sequence : point d'entree unique de production (Lot 8.1)
// ---------------------------------------------------------------------------

namespace {

// Projet minimal : un carre vectoriel de 10 mm et un objet de contour
// (RunningStitchParams par defaut) -- meme forme que make_project() dans
// test_generate.cpp (non partageable, anonyme a la TU), pour exercer
// effective_sequence via le VRAI generateur plutot qu'une sequence
// synthetique.
document::Project make_running_square_project() {
    document::Project project;
    geometry::Path square;
    square.closed = true;
    const std::int32_t s = 10'000;
    square.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
    };
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{square, {}});
    project.vector_objects.push_back(vec);

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    project.embroidery_objects.push_back(emb);
    return project;
}

}  // namespace

TEST_CASE("effective_sequence : identique au brut quand l'objet est Clean") {
    const auto project = make_running_square_project();
    const auto raw = generate_sequence(project);
    const auto effective = effective_sequence(project);
    REQUIRE(raw.has_value());
    REQUIRE(effective.has_value());
    CHECK(effective->commands == raw->commands);
}

TEST_CASE("effective_sequence : applique les retouches quand l'objet est ManuallyEdited") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;

    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, oid);
    REQUIRE(slice.size() > 2);
    REQUIRE(slice[1].type == CmdType::Stitch);
    REQUIRE(slice[1].pass == Pass::TopStitch);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.moved_to = Vec2um{Micrometers{123}, Micrometers{456}};
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice);

    const auto effective = effective_sequence(project);
    REQUIRE(effective.has_value());
    REQUIRE(effective->commands.size() == raw->commands.size());  // aucun Trim insere ici
    CHECK(effective->commands[1].pos == (Vec2um{Micrometers{123}, Micrometers{456}}));
    CHECK(effective->commands[1].pass == Pass::Manual);
    // Les points non cibles restent ceux du brut.
    CHECK(effective->commands[2].pos == raw->commands[2].pos);
    CHECK(effective->commands.front() == raw->commands.front());  // Jump initial inchange
}

TEST_CASE("effective_sequence : propage l'erreur de generate_sequence sans plantage") {
    auto project = make_running_square_project();
    project.embroidery_objects[0].visible = false;  // aucun objet visible -> erreur
    const auto effective = effective_sequence(project);
    REQUIRE_FALSE(effective.has_value());
    CHECK(effective.error().category == ErrorCategory::UserInput);
}

TEST_CASE("effective_sequence : deterministe sur deux appels independants") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, oid);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.trim_after = true;
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice);

    const auto e1 = effective_sequence(project);
    const auto e2 = effective_sequence(project);
    REQUIRE(e1.has_value());
    REQUIRE(e2.has_value());
    CHECK(e1->commands == e2->commands);
}

// ---------------------------------------------------------------------------
// is_movable_point (Lot 8.2 : predicat expose pour le placement des poignees)
// ---------------------------------------------------------------------------

TEST_CASE("is_movable_point : vrai pour un Stitch en passe TopStitch") {
    const ObjectId o{1};
    CHECK(is_movable_point(mk(0, 0, CmdType::Stitch, o, Pass::TopStitch)));
}

TEST_CASE("is_movable_point : faux pour un Jump en passe TopStitch") {
    const ObjectId o{1};
    CHECK_FALSE(is_movable_point(mk(0, 0, CmdType::Jump, o, Pass::TopStitch)));
}

TEST_CASE("is_movable_point : faux pour un Stitch hors passe TopStitch") {
    const ObjectId o{1};
    CHECK_FALSE(is_movable_point(mk(0, 0, CmdType::Stitch, o, Pass::Underlay)));
    CHECK_FALSE(is_movable_point(mk(0, 0, CmdType::Stitch, o, Pass::Travel)));
    CHECK_FALSE(is_movable_point(mk(0, 0, CmdType::Stitch, o, Pass::Lock)));
}

// ---------------------------------------------------------------------------
// edit_view (Lot 8.2 : bloc de construction unique pour le mode edition UI)
// ---------------------------------------------------------------------------

TEST_CASE("edit_view : Clean, vue brute et fingerprint coherents avec generate_sequence") {
    const auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;

    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto expectedSlice = raw_slice(*raw, oid);

    const auto view = edit_view(project, oid);
    REQUIRE(view.has_value());
    CHECK(view->state == ObjectEditState::Clean);
    CHECK(view->raw == expectedSlice);
    CHECK(view->point_count == static_cast<std::uint32_t>(expectedSlice.size()));
    CHECK(view->fingerprint == fingerprint(expectedSlice));
}

TEST_CASE("edit_view : ManuallyEdited une fois l'objet retouche") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, oid);
    REQUIRE(slice.size() > 1);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.moved_to = Vec2um{Micrometers{111}, Micrometers{222}};
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice);

    const auto view = edit_view(project, oid);
    REQUIRE(view.has_value());
    CHECK(view->state == ObjectEditState::ManuallyEdited);
}

TEST_CASE("edit_view : Dirty quand l'empreinte stockee ne correspond plus") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, oid);

    document::StitchOverride ov;
    ov.base_index = 0;
    ov.trim_after = true;
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice) + 1;  // corrompue

    const auto view = edit_view(project, oid);
    REQUIRE(view.has_value());
    CHECK(view->state == ObjectEditState::Dirty);
    // La vue brute reste fidele malgre l'etat Dirty (rien n'est masque a l'UI).
    CHECK(view->raw == slice);
}

TEST_CASE("edit_view : Clean pour un objet absent du projet, sans plantage") {
    const auto project = make_running_square_project();
    const auto view = edit_view(project, ObjectId{999'999});
    REQUIRE(view.has_value());
    CHECK(view->state == ObjectEditState::Clean);
    CHECK(view->raw.empty());
    CHECK(view->point_count == 0);
}

TEST_CASE("edit_view : propage l'erreur de generate_sequence sans plantage") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    project.embroidery_objects[0].visible = false;  // aucun objet visible -> erreur
    const auto view = edit_view(project, oid);
    REQUIRE_FALSE(view.has_value());
    CHECK(view.error().category == ErrorCategory::UserInput);
}

// ---------------------------------------------------------------------------
// classify_all_edit_states (Lot 8.2 : indicateurs par objet)
// ---------------------------------------------------------------------------

TEST_CASE("classify_all_edit_states : vide et sans appel a generate_sequence si aucun objet retouche") {
    auto project = make_running_square_project();
    // Rendrait generate_sequence en erreur si jamais invoque -- prouve le
    // court-circuit "aucun objet retouche" avant tout calcul de sequence.
    project.embroidery_objects[0].visible = false;

    const auto states = classify_all_edit_states(project);
    REQUIRE(states.has_value());
    CHECK(states->empty());
}

TEST_CASE("classify_all_edit_states : objets Clean absents, ManuallyEdited/Dirty presents") {
    auto project = make_running_square_project();
    const ObjectId cleanId = project.object_ids.next();
    {
        // Deuxieme objet, jamais retouche : doit rester absent du resultat.
        document::EmbroideryObject clean;
        clean.id = cleanId;
        clean.source_vector = project.vector_objects[0].id;
        project.embroidery_objects.push_back(clean);
    }
    const ObjectId editedId = project.embroidery_objects[0].id;

    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, editedId);
    REQUIRE(slice.size() > 1);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.moved_to = Vec2um{Micrometers{50}, Micrometers{60}};
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice) + 1;  // Dirty

    const auto states = classify_all_edit_states(project);
    REQUIRE(states.has_value());
    REQUIRE(states->size() == 1);  // l'objet Clean n'apparait pas
    CHECK((*states)[0].first == editedId);
    CHECK((*states)[0].second == ObjectEditState::Dirty);
}

// ---------------------------------------------------------------------------
// refresh_context (Lot 8.2, revue corrective point 1 : un seul
// generate_sequence pour effective/edit_states/target_view a la fois)
// ---------------------------------------------------------------------------

TEST_CASE("refresh_context : effective identique a effective_sequence, sans cible") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, oid);

    document::StitchOverride ov;
    ov.base_index = 1;
    ov.moved_to = Vec2um{Micrometers{123}, Micrometers{456}};
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice);

    const auto expected = effective_sequence(project);
    REQUIRE(expected.has_value());

    const auto ctx = refresh_context(project, std::nullopt);
    REQUIRE(ctx.has_value());
    CHECK(ctx->effective.commands == expected->commands);
    CHECK_FALSE(ctx->target_view.has_value());  // aucune cible demandee
    REQUIRE(ctx->edit_states.size() == 1);
    CHECK(ctx->edit_states[0].first == oid);
    CHECK(ctx->edit_states[0].second == ObjectEditState::ManuallyEdited);
}

TEST_CASE("refresh_context : target_view identique a edit_view pour la meme cible") {
    const auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;

    const auto expectedView = edit_view(project, oid);
    REQUIRE(expectedView.has_value());

    const auto ctx = refresh_context(project, oid);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->target_view.has_value());
    CHECK(ctx->target_view->state == expectedView->state);
    CHECK(ctx->target_view->raw == expectedView->raw);
    CHECK(ctx->target_view->fingerprint == expectedView->fingerprint);
    CHECK(ctx->target_view->point_count == expectedView->point_count);
}

TEST_CASE("refresh_context : target_view present et Dirty quand l'empreinte ne correspond plus") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    const auto raw = generate_sequence(project);
    REQUIRE(raw.has_value());
    const auto slice = raw_slice(*raw, oid);

    document::StitchOverride ov;
    ov.base_index = 0;
    ov.trim_after = true;
    project.embroidery_objects[0].overrides = {ov};
    project.embroidery_objects[0].edited_point_count = static_cast<std::uint32_t>(slice.size());
    project.embroidery_objects[0].edited_fingerprint = fingerprint(slice) + 1;  // corrompue

    const auto ctx = refresh_context(project, oid);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->target_view.has_value());
    CHECK(ctx->target_view->state == ObjectEditState::Dirty);  // jamais filtre ici : a l'appelant de le faire
    // effective conserve la sequence brute pour l'objet Dirty (jamais reappliquee).
    CHECK(raw_slice(ctx->effective, oid) == slice);
}

TEST_CASE("refresh_context : propage l'erreur de generate_sequence sans plantage") {
    auto project = make_running_square_project();
    const ObjectId oid = project.embroidery_objects[0].id;
    project.embroidery_objects[0].visible = false;  // aucun objet visible -> erreur
    const auto ctx = refresh_context(project, oid);
    REQUIRE_FALSE(ctx.has_value());
    CHECK(ctx.error().category == ErrorCategory::UserInput);
}
