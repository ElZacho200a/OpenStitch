// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/composite_command.hpp"

#include <utility>

namespace openstitch::commands {

CompositeCommand::CompositeCommand(std::string name,
                                   std::vector<std::unique_ptr<ICommand>> commands)
    : name_(std::move(name)), commands_(std::move(commands)) {
    std::erase(commands_, nullptr);
}

void CompositeCommand::add(std::unique_ptr<ICommand> command) {
    if (command) {
        commands_.push_back(std::move(command));
    }
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

bool CompositeCommand::mergeWith(const ICommand& newer) {
    const auto* other = dynamic_cast<const CompositeCommand*>(&newer);
    if (other == nullptr || mergeKey_.empty() || other->mergeKey_ != mergeKey_ ||
        other->commands_.size() != commands_.size()) {
        return false;
    }
    // Vérification préalable : tout ou rien (jamais de fusion partielle).
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        const std::string key = commands_[i]->mergeKey();
        if (key.empty() || key != other->commands_[i]->mergeKey()) {
            return false;
        }
    }
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        if (!commands_[i]->mergeWith(*other->commands_[i])) {
            return false; // ne se produit pas si les clés concordent
        }
    }
    return true;
}

void CompositeCommand::revert(document::Project& project) {
    for (std::size_t i = commands_.size(); i > 0; --i) {
        commands_[i - 1]->revert(project);
    }
}

} // namespace openstitch::commands
