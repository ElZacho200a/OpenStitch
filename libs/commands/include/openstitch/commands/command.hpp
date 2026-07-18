// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Command pattern (ADR-010) : TOUTE mutation du document passe par une
// commande, dès la première. `apply` puis `revert` doivent ramener le
// document exactement à son état antérieur.
class ICommand {
public:
    virtual ~ICommand() = default;

    virtual void apply(document::Project& project) = 0;
    virtual void revert(document::Project& project) = 0;
    [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace openstitch::commands
