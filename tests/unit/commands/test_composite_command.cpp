// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

// Sous-commande de test : journalise apply/revert dans un vecteur partagé.
// (Le Project n'est pas touché : seul l'ordre d'appel est observé.)
class LogCommand final : public ICommand {
public:
    LogCommand(std::vector<std::string>& log, std::string tag, bool failOnApply = false)
        : log_(log), tag_(std::move(tag)), failOnApply_(failOnApply) {}
    void apply(document::Project&) override {
        if (failOnApply_) {
            log_.push_back("fail " + tag_);
            throw std::runtime_error("boom " + tag_);
        }
        log_.push_back("apply " + tag_);
    }
    void revert(document::Project&) override { log_.push_back("revert " + tag_); }
    [[nodiscard]] std::string name() const override { return tag_; }

private:
    std::vector<std::string>& log_;
    std::string tag_;
    bool failOnApply_;
};

std::unique_ptr<CompositeCommand> make_abc(std::vector<std::string>& log) {
    auto c = std::make_unique<CompositeCommand>("Trois");
    c->add(std::make_unique<LogCommand>(log, "A"));
    c->add(std::make_unique<LogCommand>(log, "B"));
    c->add(std::make_unique<LogCommand>(log, "C"));
    return c;
}

std::vector<std::string> run_scenario() {
    std::vector<std::string> log;
    document::Project project;
    UndoStack stack;
    stack.execute(make_abc(log), project);
    stack.undo(project);
    stack.redo(project);
    stack.undo(project);
    return log;
}

} // namespace

TEST_CASE("composite applies in order and reverts in reverse order") {
    std::vector<std::string> log;
    document::Project project;
    auto composite = make_abc(log);
    CHECK(composite->size() == 3);

    composite->apply(project);
    CHECK(log == std::vector<std::string>{"apply A", "apply B", "apply C"});

    log.clear();
    composite->revert(project);
    CHECK(log == std::vector<std::string>{"revert C", "revert B", "revert A"});
}

TEST_CASE("composite is one undo step") {
    std::vector<std::string> log;
    document::Project project;
    UndoStack stack;

    stack.execute(make_abc(log), project);
    CHECK(stack.undoName() == "Trois");

    CHECK(stack.undo(project));
    CHECK_FALSE(stack.canUndo()); // un seul pas pour les trois sous-commandes
    CHECK(stack.canRedo());
    CHECK(stack.redoName() == "Trois");
    CHECK(log == std::vector<std::string>{"apply A", "apply B", "apply C", "revert C", "revert B",
                                          "revert A"});

    log.clear();
    CHECK(stack.redo(project));
    CHECK(log == std::vector<std::string>{"apply A", "apply B", "apply C"});
    CHECK_FALSE(stack.canRedo());
}

TEST_CASE("composite empty is a no-op") {
    document::Project project;
    UndoStack stack;
    auto composite = std::make_unique<CompositeCommand>("Vide");
    CHECK(composite->empty());
    stack.execute(std::move(composite), project);
    CHECK(stack.undoName() == "Vide");
    CHECK(stack.undo(project));
    CHECK(stack.redo(project));
}

TEST_CASE("composite is deterministic across two runs") {
    CHECK(run_scenario() == run_scenario());
}

TEST_CASE("composite name is reported") {
    CHECK(CompositeCommand("Supprimer 3 objets").name() == "Supprimer 3 objets");
    std::vector<std::unique_ptr<ICommand>> subs;
    std::vector<std::string> log;
    subs.push_back(std::make_unique<LogCommand>(log, "X"));
    CompositeCommand c("Nom", std::move(subs));
    CHECK(c.name() == "Nom");
    CHECK(c.size() == 1);
}

TEST_CASE("composite rolls back applied children when a child throws") {
    std::vector<std::string> log;
    document::Project project;
    CompositeCommand c("Echec");
    c.add(std::make_unique<LogCommand>(log, "A"));
    c.add(std::make_unique<LogCommand>(log, "B"));
    c.add(std::make_unique<LogCommand>(log, "C", true));
    c.add(std::make_unique<LogCommand>(log, "D"));

    CHECK_THROWS_AS(c.apply(project), std::runtime_error);
    CHECK(log == std::vector<std::string>{"apply A", "apply B", "fail C", "revert B", "revert A"});
}

TEST_CASE("failed composite is not pushed on the undo stack") {
    std::vector<std::string> log;
    document::Project project;
    UndoStack stack;
    auto c = std::make_unique<CompositeCommand>("Echec");
    c->add(std::make_unique<LogCommand>(log, "A"));
    c->add(std::make_unique<LogCommand>(log, "B", true));

    CHECK_THROWS_AS(stack.execute(std::move(c), project), std::runtime_error);
    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.canRedo());
}
