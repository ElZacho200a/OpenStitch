// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/composite_command.hpp"

#include <utility>

namespace openstitch::commands {

CompositeCommand::CompositeCommand(std::string name,
                                   std::vector<std::unique_ptr<ICommand>> commands)
    : name_(std::move(name)), commands_(std::move(commands)) {}

void CompositeCommand::add(std::unique_ptr<ICommand> command) {
    commands_.push_back(std::move(command));
}

void CompositeCommand::apply(document::Project& project) {
    std::size_t applied = 0;
    try {
        for (; applied < commands_.size(); ++applied) {
            commands_[applied]->apply(project);
        }
    } catch (...) {
        // Garantie forte : on défait ce qui avait réussi, puis on relance.
        for (std::size_t i = applied; i > 0; --i) {
            commands_[i - 1]->revert(project);
        }
        throw;
    }
}

void CompositeCommand::revert(document::Project& project) {
    for (std::size_t i = commands_.size(); i > 0; --i) {
        commands_[i - 1]->revert(project);
    }
}

} // namespace openstitch::commands
