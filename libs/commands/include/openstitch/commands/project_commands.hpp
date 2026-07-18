// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <utility>

#include "openstitch/commands/command.hpp"
#include "openstitch/image/ops.hpp"

namespace openstitch::commands {

// Ajoute une opération de prétraitement à la pile du projet.
class AppendImageOpCommand final : public ICommand {
public:
    explicit AppendImageOpCommand(image::ImageOp op) : op_(std::move(op)) {}

    void apply(document::Project& project) override { project.ops.push_back(op_); }
    void revert(document::Project& project) override { project.ops.pop_back(); }
    [[nodiscard]] std::string name() const override { return image::op_name(op_); }

private:
    image::ImageOp op_;
};

}  // namespace openstitch::commands
