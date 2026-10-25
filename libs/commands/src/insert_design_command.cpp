// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/insert_design_command.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>

#include "openstitch/commands/transform_ops.hpp"

namespace openstitch::commands {

namespace {

[[nodiscard]] Micrometers shifted(Micrometers v, Micrometers d) {
    const std::int64_t sum = static_cast<std::int64_t>(v.value) + d.value;
    return Micrometers{static_cast<std::int32_t>(std::clamp<std::int64_t>(
        sum, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()))};
}

[[nodiscard]] Vec2um shifted(Vec2um p, Vec2um d) {
    return Vec2um{shifted(p.x, d.x), shifted(p.y, d.y)};
}

[[nodiscard]] bool has_objects(const document::Project& p) {
    return !p.vector_objects.empty() || !p.embroidery_objects.empty() || !p.text_objects.empty();
}

template <typename T> void erase_ids(std::vector<T>& items, const std::vector<ObjectId>& ids) {
    std::erase_if(items, [&](const T& item) {
        return std::find(ids.begin(), ids.end(), item.id) != ids.end();
    });
}

} // namespace

std::unique_ptr<ICommand> make_insert_design_command(const document::Project& target,
                                                     document::Project incoming, Vec2um offset,
                                                     std::string* why) {
    auto refuse = [&](const char* message) -> std::unique_ptr<ICommand> {
        if (why != nullptr) {
            *why = message;
        }
        return nullptr;
    };
    const bool incoming_objects = has_objects(incoming);
    const bool incoming_machine =
        incoming.imported_design.has_value() && !incoming.imported_design->sequence.empty();
    if (!incoming_objects && !incoming_machine) {
        return refuse("Le design a inserer est vide.");
    }
    if (incoming_machine && has_objects(target)) {
        return refuse("Un fichier machine ne peut pas etre insere dans un projet qui contient des "
                      "objets editables.");
    }
    if (incoming_objects && target.imported_design.has_value()) {
        return refuse("Le projet contient une broderie machine importee : des objets editables "
                      "ne peuvent pas y etre inseres.");
    }
    return std::make_unique<InsertDesignCommand>(std::move(incoming), offset);
}

InsertDesignCommand::InsertDesignCommand(document::Project incoming, Vec2um offset)
    : incoming_(std::move(incoming)), offset_(offset) {}

void InsertDesignCommand::apply(document::Project& project) {
    if (!prepared_) {
        prepared_ = true;
        const bool moves = offset_.x.value != 0 || offset_.y.value != 0;
        // Translation des formes et de tout ce qui en dépend (satin, guides...), sur la copie.
        if (moves && !incoming_.vector_objects.empty()) {
            if (auto move =
                    make_transform_command(incoming_, all_object_ids(incoming_),
                                           AffineTransform::translate(offset_), "Translater")) {
                move->apply(incoming_);
            }
        }
        std::map<ObjectId, ObjectId> vector_map;
        std::map<ObjectId, ObjectId> text_map;
        const bool calque_ok =
            project.active_calque.valid() && project.findCalque(project.active_calque) != nullptr;
        for (auto& text : incoming_.text_objects) {
            const ObjectId fresh = project.object_ids.next();
            text_map[text.id] = fresh;
            text.id = fresh;
            text.origin = shifted(text.origin, offset_);
            text_ids_.push_back(fresh);
        }
        for (auto& shape : incoming_.vector_objects) {
            const ObjectId fresh = project.object_ids.next();
            vector_map[shape.id] = fresh;
            shape.id = fresh;
            shape.source_region.reset();
            shape.calque = calque_ok ? project.active_calque : ObjectId{};
            if (shape.text_owner) {
                const auto it = text_map.find(*shape.text_owner);
                if (it != text_map.end()) {
                    shape.text_owner = it->second;
                } else {
                    shape.text_owner.reset();
                }
            }
            vector_ids_.push_back(fresh);
        }
        std::map<ObjectId, ObjectId> embroidery_map;
        for (auto& object : incoming_.embroidery_objects) {
            const ObjectId fresh = project.object_ids.next();
            embroidery_map[object.id] = fresh;
            object.id = fresh;
            const auto it = vector_map.find(object.source_vector);
            object.source_vector = it != vector_map.end() ? it->second : ObjectId{};
            embroidery_ids_.push_back(fresh);
        }
        for (auto& object : incoming_.embroidery_objects) {
            if (object.color_blend) {
                const auto it = embroidery_map.find(object.color_blend->partner);
                if (it != embroidery_map.end()) {
                    object.color_blend->partner = it->second;
                } else {
                    object.color_blend.reset();
                }
            }
        }
    }

    for (const auto& shape : incoming_.vector_objects) {
        project.vector_objects.push_back(shape);
    }
    for (const auto& object : incoming_.embroidery_objects) {
        project.embroidery_objects.push_back(object);
    }
    for (const auto& text : incoming_.text_objects) {
        project.text_objects.push_back(text);
    }

    if (incoming_.imported_design && !incoming_.imported_design->sequence.empty()) {
        imported_touched_ = true;
        previous_imported_ = project.imported_design;
        document::ImportedDesign merged =
            project.imported_design ? *project.imported_design : document::ImportedDesign{};
        if (merged.source_format.empty()) {
            merged.source_format = incoming_.imported_design->source_format;
        }
        auto& commands = merged.sequence.commands;
        // Un seul « fin de motif » : celui du dernier design.
        if (!commands.empty() && commands.back().type == stitch::CommandType::End) {
            commands.pop_back();
            if (!merged.color_blocks.empty() && merged.color_blocks.back().end > commands.size()) {
                merged.color_blocks.back().end = commands.size();
            }
        }
        const std::size_t base = commands.size();
        for (auto command : incoming_.imported_design->sequence.commands) {
            command.pos = shifted(command.pos, offset_);
            commands.push_back(command);
        }
        for (auto block : incoming_.imported_design->color_blocks) {
            block.start += base;
            block.end += base;
            merged.color_blocks.push_back(block);
        }
        project.imported_design = std::move(merged);
    }
}

void InsertDesignCommand::revert(document::Project& project) {
    erase_ids(project.vector_objects, vector_ids_);
    erase_ids(project.embroidery_objects, embroidery_ids_);
    erase_ids(project.text_objects, text_ids_);
    if (imported_touched_) {
        project.imported_design = previous_imported_;
        previous_imported_.reset();
        imported_touched_ = false;
    }
}

} // namespace openstitch::commands
