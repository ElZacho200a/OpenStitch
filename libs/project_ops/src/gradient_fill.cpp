// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/gradient_fill.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/commands/color_blend_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/document/color_blend.hpp"
#include "openstitch/stitch_generation/color_blend.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::project_ops {

namespace {

struct Extent {
    double min{0.0};
    double max{0.0};
    bool any{false};
};

// Projection des nœuds extérieurs sur `u` (mêmes points que density_gradient_across).
Extent projected_extent(const std::vector<geometry::PathSet>& shape, double ux, double uy) {
    Extent extent;
    for (const auto& set : shape) {
        for (const auto& node : set.outer.nodes) {
            const double t = node.pos.x.value * ux + node.pos.y.value * uy;
            extent.min = extent.any ? std::min(extent.min, t) : t;
            extent.max = extent.any ? std::max(extent.max, t) : t;
            extent.any = true;
        }
    }
    return extent;
}

bool axis_unit(const document::DensityGradient& gradient, double& ux, double& uy) {
    const double ax = static_cast<double>(gradient.to.x.value) - gradient.from.x.value;
    const double ay = static_cast<double>(gradient.to.y.value) - gradient.from.y.value;
    const double length = std::hypot(ax, ay);
    if (length < 1.0) {
        return false;
    }
    ux = ax / length;
    uy = ay / length;
    return true;
}

const std::vector<geometry::PathSet>* shape_of(const document::Project& project,
                                               const document::EmbroideryObject& layer) {
    const auto* source = project.findObject(layer.source_vector);
    return source != nullptr ? &source->paths : nullptr;
}

Vec2um rounded(double x, double y) {
    return {Micrometers{static_cast<std::int32_t>(std::lround(x))},
            Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

void layer_update(const document::EmbroideryObject& before, const document::EmbroideryObject& after,
                  commands::CompositeCommand& batch, const std::string& field) {
    if (before.params != after.params) {
        batch.add(
            std::make_unique<commands::SetStitchParamsCommand>(before.id, after.params, field));
    }
    if (before.thread != after.thread || before.rgb != after.rgb) {
        batch.add(std::make_unique<commands::SetObjectThreadCommand>(
            std::vector<commands::ObjectThreadAssignment>{{before.id, after.thread, after.rgb}}));
    }
}

} // namespace

std::optional<document::ColorBlendLayers> gradient_layers(const document::Project& project,
                                                          ObjectId layer) {
    return document::find_color_blend(project, layer);
}

std::optional<GradientFillSpec> describe_gradient_fill(const document::Project& project,
                                                       ObjectId layer) {
    const auto layers = document::find_color_blend(project, layer);
    if (!layers) {
        return std::nullopt;
    }
    const auto* base = project.findEmbroidery(layers->background);
    const auto* top = project.findEmbroidery(layers->foreground);
    const auto* topParams =
        top != nullptr ? std::get_if<document::DirectionalFillParams>(&top->params) : nullptr;
    if (base == nullptr || topParams == nullptr || !topParams->density_gradient) {
        return std::nullopt;
    }
    const auto* baseParams = std::get_if<document::DirectionalFillParams>(&base->params);
    GradientFillSpec spec;
    spec.background = base->rgb;
    spec.foreground = top->rgb;
    spec.background_thread = base->thread;
    spec.foreground_thread = top->thread;
    spec.gradient = *topParams->density_gradient;
    spec.complementary = baseParams != nullptr && baseParams->density_gradient.has_value();
    return spec;
}

std::optional<GradientZone> gradient_zone(const std::vector<geometry::PathSet>& shape,
                                          const document::DensityGradient& gradient) {
    double ux = 0.0;
    double uy = 0.0;
    const auto extent = projected_extent(shape, 0.0, 0.0);
    if (!extent.any || !axis_unit(gradient, ux, uy)) {
        return std::nullopt;
    }
    const auto along = projected_extent(shape, ux, uy);
    const double span = along.max - along.min;
    if (span < 1.0) {
        return std::nullopt;
    }
    const auto at = [&](Vec2um p) { return (p.x.value * ux + p.y.value * uy - along.min) / span; };
    return GradientZone{at(gradient.from), at(gradient.to)};
}

std::optional<document::DensityGradient>
gradient_with_zone(const std::vector<geometry::PathSet>& shape,
                   const document::DensityGradient& gradient, GradientZone zone) {
    double ux = 0.0;
    double uy = 0.0;
    if (!std::isfinite(zone.start) || !std::isfinite(zone.end) || zone.start < 0.0 ||
        zone.end > 1.0 || zone.start >= zone.end || !axis_unit(gradient, ux, uy)) {
        return std::nullopt;
    }
    const auto along = projected_extent(shape, ux, uy);
    if (!along.any || along.max - along.min < 1.0) {
        return std::nullopt;
    }
    const double tf = gradient.from.x.value * ux + gradient.from.y.value * uy;
    const double span = along.max - along.min;
    // Déplacement le long de l'axe seulement : la droite support ne change pas.
    const auto place = [&](double fraction) {
        const double shift = along.min + fraction * span - tf;
        return rounded(gradient.from.x.value + ux * shift, gradient.from.y.value + uy * shift);
    };
    auto moved = gradient;
    moved.from = place(zone.start);
    moved.to = place(zone.end);
    return moved;
}

std::optional<document::DensityGradient>
drag_gradient_handle(const std::vector<geometry::PathSet>& shape,
                     const document::DensityGradient& gradient, GradientHandle handle,
                     Vec2um pointer, bool symmetric) {
    double ux = 0.0;
    double uy = 0.0;
    const auto zone = gradient_zone(shape, gradient);
    if (!zone || !axis_unit(gradient, ux, uy)) {
        return std::nullopt;
    }
    const auto along = projected_extent(shape, ux, uy);
    const double span = along.max - along.min;
    constexpr double kMinWidth = 0.01; // la transition garde une largeur non nulle
    const double fx = gradient.from.x.value;
    const double fy = gradient.from.y.value;
    const double cx = (fx + gradient.to.x.value) * 0.5;
    const double cy = (fy + gradient.to.y.value) * 0.5;
    const auto fraction = [&](double t) { return (t - along.min) / span; };
    const double pointerT = pointer.x.value * ux + pointer.y.value * uy;
    GradientZone next = *zone;
    switch (handle) {
    case GradientHandle::Start:
    case GradientHandle::End: {
        const double mid = (zone->start + zone->end) * 0.5;
        const double f = std::clamp(fraction(pointerT), 0.0, 1.0);
        if (handle == GradientHandle::Start) {
            next.start = std::min(f, zone->end - kMinWidth);
            if (symmetric) {
                next.end = std::min(1.0, 2.0 * mid - next.start);
                next.start = std::max(0.0, 2.0 * mid - next.end);
            }
        } else {
            next.end = std::max(f, zone->start + kMinWidth);
            if (symmetric) {
                next.start = std::max(0.0, 2.0 * mid - next.end);
                next.end = std::min(1.0, 2.0 * mid - next.start);
            }
        }
        break;
    }
    case GradientHandle::Move: {
        const double dx = pointer.x.value - cx;
        const double dy = pointer.y.value - cy;
        const double shift = std::clamp((dx * ux + dy * uy) / span, -zone->start, 1.0 - zone->end);
        auto moved = gradient;
        moved.from = rounded(fx + dx, fy + dy);
        moved.to = rounded(gradient.to.x.value + dx, gradient.to.y.value + dy);
        next.start = zone->start + shift;
        next.end = zone->end + shift;
        return gradient_with_zone(shape, moved, next);
    }
    case GradientHandle::Rotate: {
        const double length = std::hypot(gradient.to.x.value - fx, gradient.to.y.value - fy);
        const double a =
            std::atan2(pointer.y.value - cy, pointer.x.value - cx) - std::numbers::pi / 2.0;
        auto turned = gradient;
        turned.from = rounded(cx - std::cos(a) * length * 0.5, cy - std::sin(a) * length * 0.5);
        turned.to = rounded(cx + std::cos(a) * length * 0.5, cy + std::sin(a) * length * 0.5);
        const auto refit = gradient_with_zone(shape, turned, *zone);
        return refit ? refit : std::optional{turned};
    }
    }
    return gradient_with_zone(shape, gradient, next);
}

std::string gradient_incompatibility(const document::Project& project, ObjectId layer) {
    const auto* emb = project.findEmbroidery(layer);
    if (emb == nullptr) {
        return "Aucune forme brodée sélectionnée.";
    }
    if (document::find_color_blend(project, layer)) {
        return {};
    }
    if (!(emb->is_tatami() || emb->is_directional())) {
        return "Le dégradé s'applique à un remplissage (tatami ou directionnel), pas à un "
               "satin ou un point droit.";
    }
    // Deux intentions indépendantes. Le moteur historique utilise le même
    // DensityGradient pour une rampe monochrome et pour une paire de fils :
    // refuser une conversion destructrice plutôt que supprimer la rampe.
    if (const auto* directional = std::get_if<document::DirectionalFillParams>(&emb->params);
        directional != nullptr && directional->density_gradient) {
        return "Ce remplissage possède déjà un dégradé de densité. Le fondu de fils "
               "ne peut pas encore être combiné sans écraser ce réglage.";
    }
    const auto* shape = shape_of(project, *emb);
    if (shape == nullptr || shape->empty()) {
        return "La forme source est introuvable.";
    }
    int fills = 0;
    for (const auto& other : project.embroidery_objects) {
        if (other.source_vector == emb->source_vector &&
            (other.is_tatami() || other.is_directional())) {
            ++fills;
        }
    }
    if (fills > 1) {
        return "Cette forme porte plusieurs remplissages : créez le dégradé sur un seul.";
    }
    return {};
}

std::optional<GradientFillSpec> default_gradient_fill(const document::Project& project,
                                                      ObjectId fill_layer) {
    const auto* fill = project.findEmbroidery(fill_layer);
    const auto* shape = fill != nullptr ? shape_of(project, *fill) : nullptr;
    if (shape == nullptr) {
        return std::nullopt;
    }
    auto gradient = stitch_generation::density_gradient_across(
        *shape, Angle{0.0}, Micrometers{4'000}, Micrometers{400});
    if (!gradient) {
        return std::nullopt;
    }
    GradientFillSpec spec;
    spec.background = fill->rgb;
    spec.background_thread = fill->thread;
    spec.foreground = {40, 90, 210};
    if (spec.foreground == spec.background) {
        spec.foreground = {240, 180, 40};
    }
    spec.gradient = *gradient;
    spec.complementary = true;
    return spec;
}

Result<std::unique_ptr<commands::ICommand>> make_create_gradient_fill_command(
    document::Project& project, ObjectId fill_layer, const GradientFillSpec& spec,
    const std::vector<geometry::PathSet>& shape, const document::EmbroideryObject* second_fill,
    ObjectId removed_layer) {
    const auto* base = project.findEmbroidery(fill_layer);
    if (base == nullptr || !(base->is_tatami() || base->is_directional())) {
        return fail(ErrorCategory::OperationImpossible, "Le dégradé s'applique à un remplissage.");
    }
    if (second_fill == nullptr && !gradient_incompatibility(project, fill_layer).empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    gradient_incompatibility(project, fill_layer));
    }
    const document::EmbroideryObject baseBefore = *base;
    const document::EmbroideryObject& template_top =
        second_fill != nullptr ? *second_fill : baseBefore;
    auto layers = stitch_generation::make_color_blend_layers(baseBefore, template_top, shape,
                                                             spec.gradient, spec.complementary);
    if (!layers) {
        return fail(
            ErrorCategory::OperationImpossible,
            "Dégradé impossible : la forme n'a pas d'étendue ou les réglages sont invalides.");
    }
    auto top = std::move(layers->second);
    top.id = project.object_ids.next();
    top.source_vector = baseBefore.source_vector;
    top.name = template_top.name + " (dessus du fondu)";
    top.rgb = spec.foreground;
    top.thread = spec.foreground_thread;
    top.color_blend.reset();
    top.overrides.clear();
    top.edited_fingerprint = 0;
    top.edited_point_count = 0;
    auto newBase = std::move(layers->first);
    newBase.rgb = spec.background;
    newBase.thread = spec.background_thread;

    auto batch = std::make_unique<commands::CompositeCommand>("Remplissage en dégradé");
    layer_update(baseBefore, newBase, *batch, "Dégradé");
    const ObjectId topId = top.id;
    batch->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(top)));
    batch->add(std::make_unique<commands::SetColorBlendLinkCommand>(fill_layer, topId));
    std::vector<ObjectId> order;
    order.reserve(project.embroidery_objects.size() + 1);
    for (const auto& e : project.embroidery_objects) {
        if (removed_layer.valid() && e.id == removed_layer) {
            continue; // supprimé par l'union des deux zones
        }
        order.push_back(e.id);
        if (e.id == fill_layer) {
            order.push_back(topId); // juste après le fond, pas en fin de motif
        }
    }
    batch->add(std::make_unique<commands::ReorderEmbroideryCommand>(std::move(order)));
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

Result<std::unique_ptr<commands::ICommand>>
make_edit_gradient_fill_command(const document::Project& project, ObjectId layer,
                                const GradientFillSpec& spec) {
    const auto layers = document::find_color_blend(project, layer);
    if (!layers) {
        return fail(ErrorCategory::OperationImpossible,
                    "Cette couche n'appartient pas à un dégradé.");
    }
    const auto* base = project.findEmbroidery(layers->background);
    const auto* top = project.findEmbroidery(layers->foreground);
    const auto* shape = base != nullptr ? shape_of(project, *base) : nullptr;
    if (base == nullptr || top == nullptr || shape == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Dégradé incomplet.");
    }
    auto rebuilt = stitch_generation::make_color_blend_layers(*base, *top, *shape, spec.gradient,
                                                              spec.complementary);
    if (!rebuilt) {
        return fail(ErrorCategory::OperationImpossible,
                    "Dégradé refusé : réglages invalides ou forme sans étendue.");
    }
    rebuilt->first.rgb = spec.background;
    rebuilt->first.thread = spec.background_thread;
    rebuilt->second.rgb = spec.foreground;
    rebuilt->second.thread = spec.foreground_thread;
    auto batch = std::make_unique<commands::CompositeCommand>("Modifier le dégradé");
    layer_update(*base, rebuilt->first, *batch, "Dégradé");
    layer_update(*top, rebuilt->second, *batch, "Dégradé");
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

Result<std::unique_ptr<commands::ICommand>>
make_remove_gradient_fill_command(const document::Project& project, ObjectId layer) {
    const auto layers = document::find_color_blend(project, layer);
    if (!layers) {
        return fail(ErrorCategory::OperationImpossible,
                    "Cette couche n'appartient pas à un dégradé.");
    }
    const auto* base = project.findEmbroidery(layers->background);
    if (base == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Dégradé incomplet.");
    }
    auto uniform = *base;
    if (auto* params = std::get_if<document::DirectionalFillParams>(&uniform.params)) {
        params->density_gradient.reset();
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Remplissage uni");
    if (uniform.params != base->params) {
        batch->add(std::make_unique<commands::SetStitchParamsCommand>(base->id, uniform.params,
                                                                      "Remplissage"));
    }
    batch->add(std::make_unique<commands::ClearColorBlendLinkCommand>(layers->background,
                                                                      layers->foreground));
    batch->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(layers->foreground));
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

namespace {

// L'unique définition du comportement multicolore. Chaque fil dispose d'un
// profil triangulaire à N arrêts. Entre deux teintes voisines, les poids sont
// (1-u,u) : leur somme est 1 sans nécessiter N mélanges binaires.
std::optional<std::vector<document::EmbroideryObject>>
multicolor_layers(const document::EmbroideryObject& base,
                  const std::vector<geometry::PathSet>& shape, const MultiGradientFillSpec& spec) {
    const std::size_t n = spec.colors.size();
    if (n < 2 || n > 8) {
        return std::nullopt;
    }
    if (!spec.layer_curves.empty() && spec.layer_curves.size() != n) {
        return std::nullopt;
    }
    if (const auto* satin = std::get_if<document::SatinParams>(&base.params)) {
        if (satin->rail_a.nodes.size() < 2 || satin->rail_b.nodes.size() < 2) {
            return std::nullopt;
        }
        std::vector<document::EmbroideryObject> layers;
        layers.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            auto next = base;
            auto params = *satin;
            if (i != 0) {
                params.center_underlay = false;
                params.underlay_edge = false;
                params.underlay_zigzag = false;
                params.underlay_mode = document::UnderlayMode::Manual;
            }
            next.params = std::move(params);
            next.rgb = spec.colors[i];
            next.thread.reset();
            next.color_blend.reset();
            next.multi_blend.reset();
            next.overrides.clear();
            next.edited_fingerprint = 0;
            next.edited_point_count = 0;
            layers.push_back(std::move(next));
        }
        return layers;
    }
    auto templateGradient = spec.gradient;
    templateGradient.density_stops.clear();
    auto pair =
        stitch_generation::make_color_blend_layers(base, base, shape, templateGradient, true);
    if (!pair) {
        return std::nullopt;
    }
    const auto& baseParams = std::get<document::DirectionalFillParams>(pair->first.params);
    const auto& topParams = std::get<document::DirectionalFillParams>(pair->second.params);
    const Micrometers spacing{std::clamp(baseParams.row_spacing.value, 100, 4'000)};
    std::vector<document::EmbroideryObject> output;
    output.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto layer = (i == 0) ? pair->first : pair->second;
        auto params = (i == 0) ? baseParams : topParams;
        auto gradient = templateGradient;
        if (!spec.layer_curves.empty()) {
            gradient.curve = spec.layer_curves[i];
        }
        gradient.spacing_from = gradient.spacing_to = spacing;
        gradient.interpolate_density = true;
        gradient.density_stops.reserve(n);
        for (std::size_t s = 0; s < n; ++s) {
            const double position = static_cast<double>(s) / static_cast<double>(n - 1);
            double weight = s == i ? 1.0 : 0.0;
            if (!spec.complementary) {
                // Fond uniforme et voiles colorés légers, sans accumulation
                // de n sous-couches pleines.
                weight = i == 0 ? 1.0 : weight * 0.30;
            }
            gradient.density_stops.push_back({position, weight});
        }
        params.density_gradient = std::move(gradient);
        // Seule la couche de fond doit pouvoir recevoir une sous-couche :
        // le moteur de fondu désactive pour le moment les sous-couches de
        // chacune pour éviter de les empiler N fois.
        params.underlay_mode = document::UnderlayMode::Manual;
        params.underlay_edge = false;
        params.underlay_parallel = false;
        params.underlay_zigzag = false;
        layer.params = std::move(params);
        layer.rgb = spec.colors[i];
        layer.thread.reset(); // palette libre, pas de fil réel inventé
        layer.color_blend.reset();
        layer.multi_blend.reset();
        layer.overrides.clear();
        layer.edited_fingerprint = 0;
        layer.edited_point_count = 0;
        output.push_back(std::move(layer));
    }
    return output;
}

std::vector<ObjectId> reordered_blend_ids(const document::Project& project,
                                          const std::vector<ObjectId>& oldIds,
                                          const std::vector<ObjectId>& newIds) {
    std::vector<ObjectId> result;
    result.reserve(project.embroidery_objects.size() + newIds.size());
    for (const auto& object : project.embroidery_objects) {
        if (object.id == oldIds.front()) {
            // Garder le remplissage de base, puis placer tous les fils du
            // groupe dans l'ordre exact de couture.
            result.insert(result.end(), newIds.begin(), newIds.end());
            continue;
        }
        if (std::find(oldIds.begin() + 1, oldIds.end(), object.id) != oldIds.end()) {
            continue; // couche ancienne déjà insérée à côté de la base
        }
        result.push_back(object.id);
    }
    return result;
}

bool validate_multicolor(const MultiGradientFillSpec& spec) {
    if (spec.colors.size() < 2 || spec.colors.size() > 8 ||
        (!spec.layer_curves.empty() && spec.layer_curves.size() != spec.colors.size())) {
        return false;
    }
    for (const auto curve : spec.layer_curves) {
        if (static_cast<int>(curve) > static_cast<int>(document::DensityCurve::ConcaveConvex)) {
            return false;
        }
    }
    for (std::size_t i = 1; i < spec.colors.size(); ++i) {
        if (spec.colors[i] == spec.colors[i - 1]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<MultiGradientFillSpec>
describe_multicolor_gradient_fill(const document::Project& project, ObjectId layer) {
    const auto group = document::find_multi_blend(project, layer);
    if (!group) {
        return std::nullopt;
    }
    const auto* first = project.findEmbroidery(group->layers.front());
    if (first == nullptr) {
        return std::nullopt;
    }
    const auto* params = std::get_if<document::DirectionalFillParams>(&first->params);
    if (!first->is_satin() && (params == nullptr || !params->density_gradient)) {
        return std::nullopt;
    }
    MultiGradientFillSpec spec;
    spec.complementary = group->complementary;
    if (params != nullptr && params->density_gradient) {
        spec.gradient = *params->density_gradient;
        spec.gradient.density_stops.clear(); // chaque membre a son propre profil
    }
    for (const auto id : group->layers) {
        const auto* member = project.findEmbroidery(id);
        if (member == nullptr) {
            return std::nullopt;
        }
        spec.colors.push_back(member->rgb);
        const auto* params = std::get_if<document::DirectionalFillParams>(&member->params);
        if (params != nullptr) {
            const auto curve =
                params->density_gradient ? params->density_gradient->curve : spec.gradient.curve;
            spec.layer_curves.push_back(curve);
        }
    }
    return spec;
}

Result<std::unique_ptr<commands::ICommand>>
make_create_multicolor_gradient_fill_command(document::Project& project, ObjectId fill,
                                             const MultiGradientFillSpec& spec) {
    const auto* base = project.findEmbroidery(fill);
    if (base == nullptr || base->color_blend || base->multi_blend ||
        !(base->is_tatami() || base->is_directional() || base->is_satin()) ||
        !validate_multicolor(spec)) {
        return fail(ErrorCategory::OperationImpossible,
                    "Choisissez un remplissage unique et entre 2 et 8 fils distincts.");
    }
    if (!base->is_satin()) {
        if (const auto why = gradient_incompatibility(project, fill); !why.empty()) {
            return fail(ErrorCategory::OperationImpossible, why);
        }
    }
    const auto* source = project.findObject(base->source_vector);
    if (source == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Forme source introuvable.");
    }
    auto layers = multicolor_layers(*base, source->paths, spec);
    if (!layers) {
        return fail(ErrorCategory::OperationImpossible, "Profil multicolore invalide.");
    }
    std::vector<ObjectId> newIds{base->id};
    for (std::size_t i = 1; i < layers->size(); ++i) {
        newIds.push_back(project.object_ids.next());
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Créer un fondu multicolore");
    layer_update(*base, layers->front(), *batch, "Fondu multicolore");
    batch->add(std::make_unique<commands::SetMultiBlendLinkCommand>(
        base->id, document::MultiBlendLink{base->id, 0, static_cast<std::uint8_t>(newIds.size()),
                                           spec.complementary}));
    for (std::size_t i = 1; i < layers->size(); ++i) {
        auto next = std::move((*layers)[i]);
        next.id = newIds[i];
        next.source_vector = base->source_vector;
        next.name = base->name + " (fondu " + std::to_string(i + 1) + ")";
        next.multi_blend =
            document::MultiBlendLink{base->id, static_cast<std::uint8_t>(i),
                                     static_cast<std::uint8_t>(newIds.size()), spec.complementary};
        batch->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(next)));
    }
    batch->add(std::make_unique<commands::ReorderEmbroideryCommand>(
        reordered_blend_ids(project, {base->id}, newIds)));
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

Result<std::unique_ptr<commands::ICommand>>
make_edit_multicolor_gradient_fill_command(document::Project& project, ObjectId member,
                                           const MultiGradientFillSpec& spec) {
    if (!validate_multicolor(spec)) {
        return fail(ErrorCategory::OperationImpossible,
                    "Entre 2 et 8 couleurs consécutives distinctes.");
    }
    const auto multi = document::find_multi_blend(project, member);
    const auto legacy = multi ? std::nullopt : document::find_color_blend(project, member);
    if (!multi && !legacy) {
        return fail(ErrorCategory::OperationImpossible, "Aucun fondu de fils éditable.");
    }
    const std::vector<ObjectId> oldIds =
        multi ? multi->layers : std::vector<ObjectId>{legacy->background, legacy->foreground};
    const auto* base = project.findEmbroidery(oldIds.front());
    const auto* source = base ? project.findObject(base->source_vector) : nullptr;
    if (base == nullptr || source == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Groupe incomplet.");
    }
    auto layers = multicolor_layers(*base, source->paths, spec);
    if (!layers) {
        return fail(ErrorCategory::OperationImpossible, "Transition multicolore invalide.");
    }
    std::vector<ObjectId> newIds;
    newIds.reserve(layers->size());
    for (std::size_t i = 0; i < layers->size(); ++i) {
        newIds.push_back(i < oldIds.size() ? oldIds[i] : project.object_ids.next());
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Modifier le fondu multicolore");
    if (legacy) {
        batch->add(std::make_unique<commands::ClearColorBlendLinkCommand>(legacy->background,
                                                                          legacy->foreground));
    }
    for (std::size_t i = 0; i < layers->size(); ++i) {
        auto next = std::move((*layers)[i]);
        next.id = newIds[i];
        next.source_vector = base->source_vector;
        const document::MultiBlendLink tag{base->id, static_cast<std::uint8_t>(i),
                                           static_cast<std::uint8_t>(layers->size()),
                                           spec.complementary};
        if (i < oldIds.size()) {
            const auto* before = project.findEmbroidery(oldIds[i]);
            if (before == nullptr) {
                return fail(ErrorCategory::OperationImpossible, "Couche manquante.");
            }
            layer_update(*before, next, *batch, "Fondu multicolore");
            if (before->multi_blend != tag) {
                batch->add(std::make_unique<commands::SetMultiBlendLinkCommand>(before->id, tag));
            }
        } else {
            next.name = base->name + " (fondu " + std::to_string(i + 1) + ")";
            next.multi_blend = tag;
            batch->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(next)));
        }
    }
    for (std::size_t i = layers->size(); i < oldIds.size(); ++i) {
        batch->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(oldIds[i]));
    }
    if (oldIds.size() != newIds.size()) {
        batch->add(std::make_unique<commands::ReorderEmbroideryCommand>(
            reordered_blend_ids(project, oldIds, newIds)));
    }
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

Result<std::unique_ptr<commands::ICommand>>
make_remove_multicolor_gradient_fill_command(const document::Project& project, ObjectId member) {
    const auto group = document::find_multi_blend(project, member);
    if (!group) {
        return fail(ErrorCategory::OperationImpossible, "Aucun groupe multicolore sélectionné.");
    }
    const auto* base = project.findEmbroidery(group->layers.front());
    if (base == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Couche de base introuvable.");
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Supprimer le fondu multicolore");
    if (const auto* params = std::get_if<document::DirectionalFillParams>(&base->params)) {
        auto uniform = *params;
        uniform.density_gradient.reset();
        batch->add(std::make_unique<commands::SetStitchParamsCommand>(base->id, uniform,
                                                                      "Remplissage uni"));
    }
    batch->add(std::make_unique<commands::SetMultiBlendLinkCommand>(base->id, std::nullopt));
    for (std::size_t i = 1; i < group->layers.size(); ++i) {
        batch->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(group->layers[i]));
    }
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

} // namespace openstitch::project_ops
