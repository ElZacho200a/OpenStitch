// SPDX-License-Identifier: Apache-2.0
// Tests adverses de CompositeCommand (revue indépendante du lot L5) : exception à
// chaque position, grand N, imbrication, ré-application après annulation, doublons
// d'ids dans une composite de suppressions, bornes de la pile d'annulation.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

// Empreinte textuelle exhaustive des objets du projet (ids, noms, nœuds, ordre).
std::string snapshot(const document::Project& project) {
    std::ostringstream out;
    for (const auto& v : project.vector_objects) {
        out << "V" << v.id.value << ':' << v.name << ':' << v.visible << '[';
        for (const auto& set : v.paths) {
            for (const auto& n : set.outer.nodes) {
                out << n.pos.x.value << ',' << n.pos.y.value << ';';
            }
            out << '|';
        }
        out << "]\n";
    }
    for (const auto& e : project.embroidery_objects) {
        out << "E" << e.id.value << ':' << e.source_vector.value << '\n';
    }
    return out.str();
}

document::VectorObject make_object(std::uint32_t id, int x) {
    document::VectorObject v;
    v.id = ObjectId{id};
    v.name = "obj" + std::to_string(id);
    geometry::Path p;
    p.closed = true;
    for (const auto& [dx, dy] : {std::pair{0, 0}, std::pair{1000, 0}, std::pair{0, 1000}}) {
        p.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{x + dx}, Micrometers{dy}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    }
    v.paths.push_back(geometry::PathSet{p, {}});
    return v;
}

document::EmbroideryObject make_emb(std::uint32_t id, std::uint32_t source) {
    document::EmbroideryObject e;
    e.id = ObjectId{id};
    e.name = "emb" + std::to_string(id);
    e.source_vector = ObjectId{source};
    return e;
}

class ThrowOnApply final : public ICommand {
public:
    void apply(document::Project&) override { throw std::runtime_error("boom"); }
    void revert(document::Project&) override {}
    [[nodiscard]] std::string name() const override { return "Throw"; }
};

// Compte les apply/revert ; revert lève au n-ième appel si demandé.
class Counting final : public ICommand {
public:
    Counting(int& applies, int& reverts) : applies_(applies), reverts_(reverts) {}
    void apply(document::Project&) override { ++applies_; }
    void revert(document::Project&) override { ++reverts_; }
    [[nodiscard]] std::string name() const override { return "Counting"; }

private:
    int& applies_;
    int& reverts_;
};

} // namespace

TEST_CASE("adv composite throwing at every position restores the exact document") {
    constexpr int n = 12;
    for (int k = 0; k <= n; ++k) {
        document::Project project;
        project.vector_objects.push_back(make_object(500, 0));
        project.embroidery_objects.push_back(make_emb(501, 500));
        const std::string before = snapshot(project);

        auto composite = std::make_unique<CompositeCommand>("Throwing");
        for (int i = 0; i < n; ++i) {
            if (i == k) {
                composite->add(std::make_unique<ThrowOnApply>());
            }
            composite->add(std::make_unique<AddVectorObjectCommand>(
                make_object(static_cast<std::uint32_t>(i + 1), i * 10'000)));
            composite->add(std::make_unique<TranslateVectorObjectCommand>(
                ObjectId{static_cast<std::uint32_t>(i + 1)},
                Vec2um{Micrometers{7}, Micrometers{9}}));
        }
        if (k == n) {
            composite->add(std::make_unique<ThrowOnApply>());
        }
        UndoStack stack;
        INFO("throw position k = " << k);
        REQUIRE_THROWS_AS(stack.execute(std::move(composite), project), std::runtime_error);
        CHECK(snapshot(project) == before);
        CHECK_FALSE(stack.canUndo());
        CHECK_FALSE(stack.canRedo());
    }
}

TEST_CASE("adv composite delete with duplicated ids and interleaved embroideries round-trips") {
    document::Project project;
    for (std::uint32_t i = 1; i <= 4; ++i) {
        project.vector_objects.push_back(make_object(i, static_cast<int>(i) * 5000));
    }
    // Broderies entrelacées : A1 B1 A2 B2 C1 (sources 1,2,1,2,3), objet 4 sans broderie.
    project.embroidery_objects = {make_emb(11, 1), make_emb(12, 2), make_emb(13, 1),
                                  make_emb(14, 2), make_emb(15, 3)};
    const std::string before = snapshot(project);

    auto composite = std::make_unique<CompositeCommand>("Supprimer");
    for (const std::uint32_t id : {1u, 2u, 1u, 3u, 99u, 2u}) { // doublons + id inexistant
        composite->add(std::make_unique<RemoveVectorObjectCommand>(ObjectId{id}));
    }
    UndoStack stack;
    stack.execute(std::move(composite), project);
    CHECK(project.vector_objects.size() == 1);
    CHECK(project.embroidery_objects.empty());
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(stack.undo(project));
        CHECK(snapshot(project) == before);
        REQUIRE(stack.redo(project));
        CHECK(project.vector_objects.size() == 1);
    }
}

TEST_CASE("adv composite re-apply after revert is idempotent over many cycles") {
    document::Project project;
    project.vector_objects.push_back(make_object(1, 0));
    auto composite = std::make_unique<CompositeCommand>("Cycle");
    for (int i = 0; i < 20; ++i) {
        composite->add(std::make_unique<TranslateVectorObjectCommand>(
            ObjectId{1}, Vec2um{Micrometers{i + 1}, Micrometers{-(i + 1)}}));
    }
    const std::string before = snapshot(project);
    composite->apply(project);
    const std::string applied = snapshot(project);
    CHECK(applied != before);
    for (int c = 0; c < 50; ++c) {
        composite->revert(project);
        REQUIRE(snapshot(project) == before);
        composite->apply(project);
        REQUIRE(snapshot(project) == applied);
    }
}

TEST_CASE("adv composite huge N applies and reverts every sub-command exactly once") {
    constexpr int n = 100'000;
    int applies = 0;
    int reverts = 0;
    document::Project project;
    auto composite = std::make_unique<CompositeCommand>("Enorme");
    for (int i = 0; i < n; ++i) {
        composite->add(std::make_unique<Counting>(applies, reverts));
    }
    CHECK(composite->size() == static_cast<std::size_t>(n));
    UndoStack stack;
    stack.execute(std::move(composite), project);
    CHECK(applies == n);
    CHECK(stack.undo(project));
    CHECK(reverts == n);
    CHECK(stack.redo(project));
    CHECK(applies == 2 * n);
}

TEST_CASE("adv composite huge N of object additions restores the document") {
    constexpr int n = 50'000;
    document::Project project;
    project.vector_objects.push_back(make_object(1, 0));
    const std::string before = snapshot(project);
    auto composite = std::make_unique<CompositeCommand>("Ajouts");
    for (int i = 0; i < n; ++i) {
        composite->add(std::make_unique<AddVectorObjectCommand>(
            make_object(static_cast<std::uint32_t>(i + 2), i)));
    }
    composite->apply(project);
    CHECK(project.vector_objects.size() == static_cast<std::size_t>(n) + 1);
    composite->revert(project);
    CHECK(snapshot(project) == before);
}

TEST_CASE("adv composite nested depth keeps apply and revert symmetric") {
    constexpr int depth = 2000;
    int applies = 0;
    int reverts = 0;
    document::Project project;
    std::unique_ptr<ICommand> current = std::make_unique<Counting>(applies, reverts);
    for (int d = 0; d < depth; ++d) {
        auto wrapper = std::make_unique<CompositeCommand>("niveau " + std::to_string(d));
        wrapper->add(std::make_unique<Counting>(applies, reverts));
        wrapper->add(std::move(current));
        current = std::move(wrapper);
    }
    current->apply(project);
    CHECK(applies == depth + 1);
    current->revert(project);
    CHECK(reverts == depth + 1);
    CHECK(current->name() == "niveau " + std::to_string(depth - 1));
}

TEST_CASE("adv composite nested failure at the deepest level unwinds every level") {
    document::Project project;
    project.vector_objects.push_back(make_object(1, 0));
    const std::string before = snapshot(project);
    constexpr int depth = 50;
    std::unique_ptr<ICommand> current = std::make_unique<ThrowOnApply>();
    for (int d = 0; d < depth; ++d) {
        auto wrapper = std::make_unique<CompositeCommand>("w");
        wrapper->add(std::make_unique<TranslateVectorObjectCommand>(
            ObjectId{1}, Vec2um{Micrometers{3}, Micrometers{4}}));
        wrapper->add(std::move(current));
        current = std::move(wrapper);
    }
    CHECK_THROWS_AS(current->apply(project), std::runtime_error);
    CHECK(snapshot(project) == before);
}

TEST_CASE("adv composite null sub-commands are dropped and name is reported verbatim") {
    std::vector<std::unique_ptr<ICommand>> cmds;
    cmds.push_back(nullptr);
    cmds.push_back(std::make_unique<AddVectorObjectCommand>(make_object(5, 0)));
    cmds.push_back(nullptr);
    CompositeCommand composite("Déplacer 3 objets \xE2\x9C\x93", std::move(cmds));
    CHECK(composite.size() == 1);
    composite.add(nullptr);
    CHECK(composite.size() == 1);
    CHECK(composite.name() == "Déplacer 3 objets \xE2\x9C\x93");
    CompositeCommand unnamed("");
    CHECK(unnamed.name().empty());
    CHECK(unnamed.empty());
}

TEST_CASE("adv composite undo stack handles thousands of steps with exact round trip") {
    document::Project project;
    UndoStack stack;
    constexpr int steps = 3000;
    std::vector<std::string> snaps;
    snaps.push_back(snapshot(project));
    for (int i = 0; i < steps; ++i) {
        auto c = std::make_unique<CompositeCommand>("Pas " + std::to_string(i));
        c->add(std::make_unique<AddVectorObjectCommand>(
            make_object(static_cast<std::uint32_t>(i + 1), i)));
        if (i % 3 == 0) {
            c->add(std::make_unique<TranslateVectorObjectCommand>(
                ObjectId{static_cast<std::uint32_t>(i + 1)},
                Vec2um{Micrometers{1}, Micrometers{2}}));
        }
        stack.execute(std::move(c), project);
        if (i % 100 == 0) {
            snaps.push_back(snapshot(project));
        }
    }
    CHECK(stack.undoName() == "Pas " + std::to_string(steps - 1));
    int undone = 0;
    while (stack.undo(project)) {
        ++undone;
    }
    CHECK(undone == steps);
    CHECK(snapshot(project) == snaps.front());
    int redone = 0;
    while (stack.redo(project)) {
        ++redone;
    }
    CHECK(redone == steps);
    CHECK(project.vector_objects.size() == static_cast<std::size_t>(steps));
    // Une nouvelle commande après un undo invalide la branche redo.
    REQUIRE(stack.undo(project));
    stack.execute(std::make_unique<CompositeCommand>("Vide"), project);
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoName() == "Vide");
}
