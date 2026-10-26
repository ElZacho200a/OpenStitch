// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/arrange_ops.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::commands {

namespace {

struct Box {
    std::int64_t min_x{0};
    std::int64_t max_x{0};
    std::int64_t min_y{0};
    std::int64_t max_y{0};
};

Box to_box(const BoundsUm& b) {
    return Box{b.min.x.value, b.max.x.value, b.min.y.value, b.max.y.value};
}

Vec2um vec(std::int64_t x, std::int64_t y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(x)},
                  Micrometers{static_cast<std::int32_t>(y)}};
}

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

// Boîte complète (contours et trous) d'une forme ; nullopt sans nœud.
std::optional<Box> shape_box(const document::Project& project, ObjectId id) {
    const auto* object = project.findObject(id);
    if (object == nullptr) {
        return std::nullopt;
    }
    std::optional<Box> box;
    const auto scan = [&box](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            const std::int64_t x = node.pos.x.value;
            const std::int64_t y = node.pos.y.value;
            if (!box) {
                box = Box{x, x, y, y};
                continue;
            }
            box->min_x = std::min(box->min_x, x);
            box->max_x = std::max(box->max_x, x);
            box->min_y = std::min(box->min_y, y);
            box->max_y = std::max(box->max_y, y);
        }
    };
    for (const auto& set : object->paths) {
        scan(set.outer);
        for (const auto& hole : set.holes) {
            scan(hole);
        }
    }
    return box;
}

} // namespace

std::vector<ObjectShift> align_shifts(const document::Project& project,
                                      const std::vector<ObjectId>& ids, AlignMode mode,
                                      AlignReference reference,
                                      const std::optional<BoundsUm>& frame) {
    std::vector<std::pair<ObjectId, Box>> boxes;
    for (const ObjectId id : ids) {
        if (std::any_of(boxes.begin(), boxes.end(),
                        [id](const auto& entry) { return entry.first == id; })) {
            continue;
        }
        if (const auto box = shape_box(project, id)) {
            boxes.emplace_back(id, *box);
        }
    }
    if (boxes.empty()) {
        return {};
    }
    Box ref = boxes.front().second;
    if (reference == AlignReference::Frame && frame) {
        ref = to_box(*frame);
    } else if (reference != AlignReference::FirstObject) {
        for (const auto& entry : boxes) {
            ref.min_x = std::min(ref.min_x, entry.second.min_x);
            ref.max_x = std::max(ref.max_x, entry.second.max_x);
            ref.min_y = std::min(ref.min_y, entry.second.min_y);
            ref.max_y = std::max(ref.max_y, entry.second.max_y);
        }
    }
    std::vector<ObjectShift> out;
    for (const auto& [id, b] : boxes) {
        std::int64_t dx = 0;
        std::int64_t dy = 0;
        switch (mode) {
        case AlignMode::Left:
            dx = ref.min_x - b.min_x;
            break;
        case AlignMode::CenterHorizontal:
            dx = (ref.min_x + ref.max_x) / 2 - (b.min_x + b.max_x) / 2;
            break;
        case AlignMode::Right:
            dx = ref.max_x - b.max_x;
            break;
        case AlignMode::Top:
            dy = ref.max_y - b.max_y;
            break;
        case AlignMode::CenterVertical:
            dy = (ref.min_y + ref.max_y) / 2 - (b.min_y + b.max_y) / 2;
            break;
        case AlignMode::Bottom:
            dy = ref.min_y - b.min_y;
            break;
        }
        if (dx != 0 || dy != 0) {
            out.push_back(ObjectShift{id, vec(dx, dy)});
        }
    }
    return out;
}

std::vector<ObjectShift> distribute_shifts(const document::Project& project,
                                           const std::vector<ObjectId>& ids, bool horizontal) {
    struct Item {
        ObjectId id;
        std::int64_t lo{0};
        std::int64_t hi{0};
    };
    std::vector<Item> items;
    for (const ObjectId id : ids) {
        if (std::any_of(items.begin(), items.end(), [id](const Item& i) { return i.id == id; })) {
            continue;
        }
        if (const auto box = shape_box(project, id)) {
            items.push_back(horizontal ? Item{id, box->min_x, box->max_x}
                                       : Item{id, box->min_y, box->max_y});
        }
    }
    if (items.size() < 3) {
        return {};
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const std::int64_t ca = a.lo + a.hi;
        const std::int64_t cb = b.lo + b.hi;
        return ca != cb ? ca < cb : a.id.value < b.id.value;
    });
    const std::int64_t n = static_cast<std::int64_t>(items.size());
    // Espace total libre entre les deux extrêmes une fois retirée l'épaisseur de chaque forme.
    std::int64_t span = items.back().hi - items.front().lo;
    for (const Item& item : items) {
        span -= item.hi - item.lo;
    }
    const std::int64_t gaps = n - 1;
    const std::int64_t base = floor_div(span, gaps);
    const std::int64_t remainder = span - base * gaps; // 0 <= remainder < gaps
    std::vector<ObjectShift> out;
    std::int64_t cursor = items.front().hi;
    for (std::int64_t i = 1; i + 1 < n; ++i) {
        const std::int64_t gap = base + (i - 1 < remainder ? 1 : 0);
        const Item& item = items[static_cast<std::size_t>(i)];
        const std::int64_t target = cursor + gap;
        const std::int64_t delta = target - item.lo;
        cursor = target + (item.hi - item.lo);
        if (delta != 0) {
            out.push_back(ObjectShift{item.id, horizontal ? vec(delta, 0) : vec(0, delta)});
        }
    }
    return out;
}

std::unique_ptr<ICommand> make_shift_command(const std::vector<ObjectShift>& shifts,
                                             std::string label) {
    if (shifts.empty()) {
        return nullptr;
    }
    auto composite = std::make_unique<CompositeCommand>(std::move(label));
    for (const ObjectShift& shift : shifts) {
        composite->add(std::make_unique<TranslateVectorObjectCommand>(shift.id, shift.delta));
    }
    return composite;
}

namespace {

class SetShapeLockedCommand final : public ICommand {
public:
    SetShapeLockedCommand(std::vector<ObjectId> ids, bool locked)
        : ids_(std::move(ids)), locked_(locked) {}

    void apply(document::Project& project) override {
        previous_.clear();
        for (const ObjectId id : ids_) {
            if (auto* object = project.findObject(id)) {
                previous_.emplace_back(id, object->locked);
                object->locked = locked_;
            }
        }
    }
    void revert(document::Project& project) override {
        for (const auto& [id, was] : previous_) {
            if (auto* object = project.findObject(id)) {
                object->locked = was;
            }
        }
    }
    [[nodiscard]] std::string name() const override {
        return locked_ ? "Verrouiller les formes" : "Déverrouiller les formes";
    }

private:
    std::vector<ObjectId> ids_;
    bool locked_;
    std::vector<std::pair<ObjectId, bool>> previous_;
};

} // namespace

std::unique_ptr<ICommand> make_shape_lock_command(const document::Project& project,
                                                  const std::vector<ObjectId>& ids, bool locked) {
    std::vector<ObjectId> changing;
    for (const ObjectId id : ids) {
        const auto* object = project.findObject(id);
        if (object != nullptr && object->locked != locked &&
            std::find(changing.begin(), changing.end(), id) == changing.end()) {
            changing.push_back(id);
        }
    }
    if (changing.empty()) {
        return nullptr;
    }
    return std::make_unique<SetShapeLockedCommand>(std::move(changing), locked);
}

} // namespace openstitch::commands
