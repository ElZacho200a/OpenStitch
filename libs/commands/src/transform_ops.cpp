// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/transform_ops.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <variant>

namespace openstitch::commands {

namespace {

std::int32_t to_um(double v) {
    constexpr double kMax = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    constexpr double kMin = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    return static_cast<std::int32_t>(std::lround(std::clamp(v, kMin, kMax)));
}

double wrap_half_turn(double radians) {
    double v = std::fmod(radians, std::numbers::pi);
    if (v < 0.0) {
        v += std::numbers::pi;
    }
    // pi lui-même (arrondi de fmod) équivaut à 0.
    return v >= std::numbers::pi ? 0.0 : v;
}

class TransformObjectsCommand final : public ICommand {
public:
    TransformObjectsCommand(std::vector<ObjectId> ids, AffineTransform transform, std::string label)
        : ids_(std::move(ids)), transform_(transform), label_(std::move(label)) {}

    void apply(document::Project& project) override {
        pathsBefore_.clear();
        paramsBefore_.clear();
        for (const ObjectId id : ids_) {
            auto* object = project.findObject(id);
            if (object == nullptr) {
                continue;
            }
            pathsBefore_.emplace_back(id, object->paths);
            for (auto& set : object->paths) {
                transformPath(set.outer);
                for (auto& hole : set.holes) {
                    transformPath(hole);
                }
            }
        }
        for (auto& emb : project.embroidery_objects) {
            if (std::find(ids_.begin(), ids_.end(), emb.source_vector) == ids_.end() ||
                project.findObject(emb.source_vector) == nullptr) {
                continue;
            }
            paramsBefore_.emplace_back(emb.id, emb.params);
            std::visit([this](auto& params) { transformParams(params); }, emb.params);
        }
    }

    void revert(document::Project& project) override {
        for (auto& [id, paths] : pathsBefore_) {
            if (auto* object = project.findObject(id)) {
                object->paths = paths;
            }
        }
        for (auto& [id, params] : paramsBefore_) {
            if (auto* emb = project.findEmbroidery(id)) {
                emb->params = params;
            }
        }
    }

    [[nodiscard]] std::string name() const override { return label_; }

private:
    void transformNode(geometry::PathNode& node) const {
        node.pos = transform_.apply(node.pos);
        if (node.tan_in) {
            node.tan_in = transform_.apply_vector(*node.tan_in);
        }
        if (node.tan_out) {
            node.tan_out = transform_.apply_vector(*node.tan_out);
        }
    }
    void transformPath(geometry::Path& path) const {
        for (auto& node : path.nodes) {
            transformNode(node);
        }
    }
    void transformPoint(std::optional<Vec2um>& p) const {
        if (p) {
            p = transform_.apply(*p);
        }
    }

    void transformParams(document::RunningStitchParams&) const {}
    void transformParams(document::TatamiParams& p) const {
        p.angle = transform_.apply_direction(p.angle);
    }
    void transformParams(document::DirectionalFillParams& p) const {
        for (auto* paths : {&p.guides, &p.break_lines}) {
            for (auto& path : *paths) {
                transformPath(path);
            }
        }
        if (p.density_gradient) {
            p.density_gradient->from = transform_.apply(p.density_gradient->from);
            p.density_gradient->to = transform_.apply(p.density_gradient->to);
        }
    }
    void transformParams(document::SatinParams& p) const {
        transformPath(p.rail_a);
        transformPath(p.rail_b);
        for (auto& rung : p.rungs) {
            rung.a = transform_.apply(rung.a);
            rung.b = transform_.apply(rung.b);
        }
        transformPoint(p.entry_point);
        transformPoint(p.exit_point);
    }
    void transformParams(document::AutoSatinParams& p) const {
        for (auto& guide : p.guides) {
            guide.anchor = transform_.apply(guide.anchor);
            // Absolu : direction du repère, transformée comme un vecteur. Relatif (écart à
            // la perpendiculaire de l'axe, qui suit déjà la forme) : seul un retournement
            // inverse le sens de l'écart.
            if (guide.absolute) {
                guide.angle = transform_.apply_direction(guide.angle);
            } else if (transform_.flips()) {
                guide.angle = Angle{wrap_half_turn(-guide.angle.radians)};
            }
        }
        // Angle des fils du balayage parallèle : direction du repère, comme un guide absolu.
        // (Absent = automatique, perpendiculaire à l'axe principal : suit déjà la forme.)
        if (p.sweep_angle) {
            p.sweep_angle = transform_.apply_direction(*p.sweep_angle);
        }
        transformPoint(p.entry_point);
        transformPoint(p.exit_point);
    }
    // Tout autre type de paramètres (futur) : inchangé.
    template <typename T> void transformParams(T&) const {}

    std::vector<ObjectId> ids_;
    AffineTransform transform_;
    std::string label_;
    std::vector<std::pair<ObjectId, std::vector<geometry::PathSet>>> pathsBefore_;
    std::vector<std::pair<ObjectId, document::StitchParams>> paramsBefore_;
};

} // namespace

AffineTransform AffineTransform::rotation(Vec2um pivot, Angle angle) {
    AffineTransform t;
    // Quarts de tour exacts : pas de 6e-17 résiduel qui déplacerait un nœud d'un µm.
    const double quarter = angle.radians / (std::numbers::pi / 2.0);
    const double rounded = std::round(quarter);
    double c = std::cos(angle.radians);
    double s = std::sin(angle.radians);
    if (std::abs(quarter - rounded) < 1e-12) {
        const int k = static_cast<int>(static_cast<long long>(rounded) % 4 + 4) % 4;
        c = k == 0 ? 1.0 : k == 2 ? -1.0 : 0.0;
        s = k == 1 ? 1.0 : k == 3 ? -1.0 : 0.0;
    }
    t.m00 = c;
    t.m01 = -s;
    t.m10 = s;
    t.m11 = c;
    t.pivot = pivot;
    return t;
}

AffineTransform AffineTransform::mirror(Vec2um pivot, bool horizontal) {
    AffineTransform t;
    t.m00 = horizontal ? -1.0 : 1.0;
    t.m11 = horizontal ? 1.0 : -1.0;
    t.pivot = pivot;
    return t;
}

AffineTransform AffineTransform::scale(Vec2um pivot, double factor) {
    AffineTransform t;
    const double f = std::isfinite(factor) && factor > 0.0 ? factor : 1.0;
    t.m00 = f;
    t.m11 = f;
    t.pivot = pivot;
    return t;
}

AffineTransform AffineTransform::translate(Vec2um delta) {
    AffineTransform t;
    t.translation = delta;
    return t;
}

Vec2um AffineTransform::apply(Vec2um p) const {
    const double dx = static_cast<double>(p.x.value) - static_cast<double>(pivot.x.value);
    const double dy = static_cast<double>(p.y.value) - static_cast<double>(pivot.y.value);
    return Vec2um{Micrometers{to_um(static_cast<double>(pivot.x.value) + m00 * dx + m01 * dy +
                                    static_cast<double>(translation.x.value))},
                  Micrometers{to_um(static_cast<double>(pivot.y.value) + m10 * dx + m11 * dy +
                                    static_cast<double>(translation.y.value))}};
}

Vec2um AffineTransform::apply_vector(Vec2um v) const {
    const double x = static_cast<double>(v.x.value);
    const double y = static_cast<double>(v.y.value);
    return Vec2um{Micrometers{to_um(m00 * x + m01 * y)}, Micrometers{to_um(m10 * x + m11 * y)}};
}

Angle AffineTransform::apply_direction(Angle a) const {
    const double x = std::cos(a.radians);
    const double y = std::sin(a.radians);
    const double tx = m00 * x + m01 * y;
    const double ty = m10 * x + m11 * y;
    if (tx == 0.0 && ty == 0.0) {
        return a;
    }
    return Angle{wrap_half_turn(std::atan2(ty, tx))};
}

std::optional<BoundsUm> objects_bounds(const document::Project& project,
                                       const std::vector<ObjectId>& ids) {
    bool any = false;
    BoundsUm box;
    for (const auto& object : project.vector_objects) {
        if (std::find(ids.begin(), ids.end(), object.id) == ids.end()) {
            continue;
        }
        for (const auto& set : object.paths) {
            for (const auto& node : set.outer.nodes) {
                if (!any) {
                    box.min = box.max = node.pos;
                    any = true;
                    continue;
                }
                box.min = Vec2um{std::min(box.min.x, node.pos.x), std::min(box.min.y, node.pos.y)};
                box.max = Vec2um{std::max(box.max.x, node.pos.x), std::max(box.max.y, node.pos.y)};
            }
        }
    }
    if (!any) {
        return std::nullopt;
    }
    return box;
}

std::vector<ObjectId> all_object_ids(const document::Project& project) {
    std::vector<ObjectId> ids;
    ids.reserve(project.vector_objects.size());
    for (const auto& object : project.vector_objects) {
        ids.push_back(object.id);
    }
    return ids;
}

std::unique_ptr<ICommand> make_transform_command(const document::Project& project,
                                                 const std::vector<ObjectId>& ids,
                                                 const AffineTransform& transform,
                                                 std::string label) {
    std::vector<ObjectId> present;
    for (const auto& object : project.vector_objects) {
        if (std::find(ids.begin(), ids.end(), object.id) != ids.end()) {
            present.push_back(object.id);
        }
    }
    if (present.empty()) {
        return nullptr;
    }
    return std::make_unique<TransformObjectsCommand>(std::move(present), transform,
                                                     std::move(label));
}

} // namespace openstitch::commands
