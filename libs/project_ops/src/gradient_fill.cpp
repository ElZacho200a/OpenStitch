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
#include "openstitch/document/multi_blend.hpp"
#include "openstitch/stitch_generation/color_blend.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/satin.hpp"

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

// Insère `newIds` juste après `afterId` dans l'ordre de couture actuel (création : l'ancre garde
// sa position, les couches physiques sont neuves).
std::vector<ObjectId> inserted_after_ids(const document::Project& project, ObjectId afterId,
                                         const std::vector<ObjectId>& newIds) {
    std::vector<ObjectId> result;
    result.reserve(project.embroidery_objects.size() + newIds.size());
    for (const auto& object : project.embroidery_objects) {
        result.push_back(object.id);
        if (object.id == afterId) {
            result.insert(result.end(), newIds.begin(), newIds.end());
        }
    }
    return result;
}

bool validate_palette(const std::vector<document::GradientThread>& palette) {
    if (palette.size() < 2 || palette.size() > 8) {
        return false;
    }
    for (std::size_t i = 1; i < palette.size(); ++i) {
        if (palette[i].rgb == palette[i - 1].rgb) {
            return false;
        }
    }
    return true;
}

// Texture du générateur directionnel qui réalise `method` (Phase 3, mission 2026-11-14) :
// Directional/LongAndShort/ControlledScatter partagent le même générateur, seule la texture du
// `DensityGradient` change -- aucun nouvel algorithme de points.
document::DensityTexture texture_for_method(document::GradientMethod method) {
    switch (method) {
    case document::GradientMethod::LongAndShort:
        return document::DensityTexture::LongShort;
    case document::GradientMethod::ControlledScatter:
        return document::DensityTexture::Stippled;
    default:
        return document::DensityTexture::None;
    }
}

MultiGradientFillSpec to_legacy_spec(const MulticolorGradientSpec& spec) {
    MultiGradientFillSpec legacy;
    legacy.colors.reserve(spec.palette.size());
    legacy.layer_curves.reserve(spec.palette.size());
    bool anyCurve = false;
    for (const auto& thread : spec.palette) {
        legacy.colors.push_back(thread.rgb);
        legacy.layer_curves.push_back(thread.curve);
        anyCurve = anyCurve || thread.curve != document::DensityCurve::Linear;
    }
    if (!anyCurve) {
        legacy.layer_curves.clear();
    }
    legacy.gradient = spec.transition;
    legacy.gradient.texture = texture_for_method(spec.method);
    legacy.complementary = spec.complementary;
    return legacy;
}

// Ligne guide droite traversant `shape` à `angle` (modèle, Y vers le haut), centrée sur sa boîte
// englobante et prolongée bien au-delà -- pour forcer un remplissage directionnel à cet angle
// EXACT (guides vide = repli sur l'axe principal, cf. `DirectionalFillParams::guides`). Réutilise
// `projected_extent`, déjà utilisé pour l'axe du dégradé.
geometry::Path straight_guide(const std::vector<geometry::PathSet>& shape, Angle angle) {
    const auto ex = projected_extent(shape, 1.0, 0.0);
    const auto ey = projected_extent(shape, 0.0, 1.0);
    const double cx = (ex.min + ex.max) / 2.0;
    const double cy = (ey.min + ey.max) / 2.0;
    const double diagonal = std::hypot(ex.max - ex.min, ey.max - ey.min);
    const double half = diagonal > 1.0 ? diagonal : 1.0;
    const double dx = std::cos(angle.radians) * half;
    const double dy = std::sin(angle.radians) * half;
    geometry::Path guide;
    guide.closed = false;
    guide.nodes = {{rounded(cx - dx, cy - dy), geometry::NodeType::Corner, {}, {}},
                   {rounded(cx + dx, cy + dy), geometry::NodeType::Corner, {}, {}}};
    return guide;
}

// Hachures croisées multicolores (méthode E, Phase 3) : chaque couche est un remplissage
// directionnel à guide droit fixe, l'angle tournant de 180°/`crosshatch_layers` d'une couche à
// l'autre (cycle sur 2 ou 3 groupes) ; la densité par couche suit le même profil complémentaire
// que les autres méthodes, sur le même axe de transition -- seule l'orientation des lignes de
// couture distingue les couches, pas l'axe de fondu des couleurs.
std::vector<document::EmbroideryObject>
crosshatch_layers(const document::EmbroideryObject& base,
                  const std::vector<geometry::PathSet>& shape, const MulticolorGradientSpec& spec) {
    const std::size_t n = spec.palette.size();
    const int angleGroups = std::clamp(spec.crosshatch_layers, 2, 3);
    const Micrometers spacing{std::clamp(
        spec.transition.spacing_from.value > 0 ? spec.transition.spacing_from.value : 1'500, 300,
        10'000)};
    std::vector<document::EmbroideryObject> output;
    output.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        document::DirectionalFillParams params;
        const double angle = spec.crosshatch_angle.radians +
                             static_cast<double>(i % static_cast<std::size_t>(angleGroups)) *
                                 (std::numbers::pi / angleGroups);
        params.guides.push_back(straight_guide(shape, Angle{angle}));
        params.row_spacing = spacing;
        document::DensityGradient gradient = spec.transition;
        gradient.interpolate_density = true;
        gradient.density_stops.clear();
        gradient.density_stops.reserve(n);
        for (std::size_t s = 0; s < n; ++s) {
            const double position =
                n > 1 ? static_cast<double>(s) / static_cast<double>(n - 1) : 0.0;
            double weight = s == i ? 1.0 : 0.0;
            if (!spec.complementary) {
                weight = i == 0 ? 1.0 : weight * 0.30;
            }
            gradient.density_stops.push_back({position, weight});
        }
        if (spec.palette[i].curve != document::DensityCurve::Linear) {
            gradient.curve = spec.palette[i].curve;
        }
        params.density_gradient = std::move(gradient);
        params.underlay_mode = document::UnderlayMode::Manual;
        auto layer = base;
        layer.params = std::move(params);
        layer.rgb = spec.palette[i].rgb;
        layer.thread = spec.palette[i].thread;
        layer.color_blend.reset();
        layer.multi_blend.reset();
        layer.overrides.clear();
        layer.edited_fingerprint = 0;
        layer.edited_point_count = 0;
        output.push_back(std::move(layer));
    }
    return output;
}

// Construit les couches physiques pour `spec`, depuis le gabarit `base` (rgb/params ACTUELS,
// jamais modifié ici). Directional/LongAndShort/ControlledScatter et SatinInterlaced-sur-rails-
// existants réutilisent intégralement `multicolor_layers` (générateurs de la PR #336, aucun
// nouvel algorithme) ; SatinInterlaced sans rails valides les dérive automatiquement du contour
// source (HP satin entrelacé, Phase 5) ; CrossHatching est nouveau (ci-dessus).
std::optional<std::vector<document::EmbroideryObject>>
gradient_object_layers(const document::EmbroideryObject& base,
                       const std::vector<geometry::PathSet>& shape,
                       const MulticolorGradientSpec& spec) {
    if (!validate_palette(spec.palette)) {
        return std::nullopt;
    }
    if (spec.method == document::GradientMethod::CrossHatching) {
        return crosshatch_layers(base, shape, spec);
    }
    const auto legacy = to_legacy_spec(spec);
    if (spec.method == document::GradientMethod::SatinInterlaced) {
        document::EmbroideryObject satinBase = base;
        const auto* existing = std::get_if<document::SatinParams>(&base.params);
        const bool hasRails = existing != nullptr && existing->rail_a.nodes.size() >= 2 &&
                              existing->rail_b.nodes.size() >= 2;
        if (!hasRails) {
            if (shape.empty()) {
                return std::nullopt;
            }
            auto rails = stitch_generation::rails_from_contour(shape.front().outer);
            if (!rails) {
                return std::nullopt;
            }
            document::SatinParams satin;
            satin.rail_a = std::move(rails->first);
            satin.rail_b = std::move(rails->second);
            satin.density = spec.satin_density;
            satinBase.params = std::move(satin);
        }
        return multicolor_layers(satinBase, shape, legacy);
    }
    document::EmbroideryObject layerBase = base;
    if (std::holds_alternative<document::SatinParams>(layerBase.params)) {
        // Méthode non satin choisie sur une forme actuellement satin : repli sur un gabarit
        // directionnel neuf (comme `multicolor_layers` le fait déjà pour tout `base` non satin).
        layerBase.params = document::DirectionalFillParams{};
    }
    return multicolor_layers(layerBase, shape, legacy);
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

document::MulticolorGradientParams anchor_params_from_spec(const MulticolorGradientSpec& spec) {
    document::MulticolorGradientParams params;
    params.method = spec.method;
    params.palette = spec.palette;
    params.transition = spec.transition;
    params.complementary = spec.complementary;
    params.satin_density = spec.satin_density;
    params.crosshatch_layers = spec.crosshatch_layers;
    params.crosshatch_angle = spec.crosshatch_angle;
    return params;
}

// Construit l'ancre (conversion EN PLACE de `base`, même id) et ses couches physiques, UN pas
// d'annulation. Ne vérifie PAS les préconditions de `base` (c'est le rôle de l'appelant public) :
// réutilisé aussi par la conversion d'un ancien fondu à deux fils (`color_blend` encore actif),
// où `clearColorBlendPartner` ajoute la commande de déliaison dans le MÊME lot.
Result<std::unique_ptr<commands::ICommand>>
build_gradient_anchor_command(document::Project& project, const document::EmbroideryObject& base,
                              const MulticolorGradientSpec& spec, bool clearColorBlendPartner,
                              ObjectId colorBlendPartner) {
    const auto* source = project.findObject(base.source_vector);
    if (source == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Forme source introuvable.");
    }
    auto layers = gradient_object_layers(base, source->paths, spec);
    if (!layers) {
        return fail(ErrorCategory::OperationImpossible,
                    "Fondu multicolore invalide pour cette géométrie/méthode.");
    }
    std::vector<ObjectId> layerIds;
    layerIds.reserve(layers->size());
    for (std::size_t i = 0; i < layers->size(); ++i) {
        layerIds.push_back(project.object_ids.next());
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Créer un fondu multicolore");
    if (clearColorBlendPartner) {
        batch->add(
            std::make_unique<commands::ClearColorBlendLinkCommand>(base.id, colorBlendPartner));
    }
    batch->add(std::make_unique<commands::SetStitchTypeCommand>(
        base.id, document::StitchParams{anchor_params_from_spec(spec)}, "Fondu multicolore"));
    for (std::size_t i = 0; i < layers->size(); ++i) {
        auto next = std::move((*layers)[i]);
        next.id = layerIds[i];
        next.source_vector = base.source_vector;
        next.name = base.name + " (fondu " + std::to_string(i + 1) + ")";
        next.multi_blend = document::MultiBlendLink{base.id, static_cast<std::uint8_t>(i),
                                                    static_cast<std::uint8_t>(layerIds.size()),
                                                    spec.complementary};
        batch->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(next)));
    }
    batch->add(std::make_unique<commands::ReorderEmbroideryCommand>(
        inserted_after_ids(project, base.id, layerIds)));
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

} // namespace

// --- Fondu multicolore AUTONOME (mission 2026-11-14, Phase 3) -----------------------------------
//
// API primaire : l'ancre (`document::MulticolorGradientParams`) porte palette/transition/méthode
// de façon permanente. Les fonctions historiques ci-dessous (`*_multicolor_gradient_fill_*`,
// `MultiGradientFillSpec`) deviennent de simples adaptateurs vers celle-ci, pour les appelants
// existants (Studio, tests) qui n'ont pas encore de méthode explicite.

std::optional<MulticolorGradientSpec> describe_multicolor_gradient(const document::Project& project,
                                                                   ObjectId anchorOrMember) {
    const auto group = document::find_gradient_group(project, anchorOrMember);
    if (!group) {
        return std::nullopt;
    }
    const auto* anchor = project.findEmbroidery(group->anchor);
    if (anchor == nullptr) {
        return std::nullopt;
    }
    const auto& params = std::get<document::MulticolorGradientParams>(anchor->params);
    MulticolorGradientSpec spec;
    spec.method = params.method;
    spec.palette = params.palette;
    spec.transition = params.transition;
    spec.complementary = params.complementary;
    spec.satin_density = params.satin_density;
    spec.crosshatch_layers = params.crosshatch_layers;
    spec.crosshatch_angle = params.crosshatch_angle;
    return spec;
}

std::optional<MulticolorGradientSpec> default_multicolor_gradient(const document::Project& project,
                                                                  ObjectId fill) {
    const auto* base = project.findEmbroidery(fill);
    if (base == nullptr) {
        return std::nullopt;
    }
    MulticolorGradientSpec spec;
    if (base->is_satin()) {
        spec.method = document::GradientMethod::SatinInterlaced;
        spec.palette = {document::GradientThread{base->rgb},
                        document::GradientThread{{255, 255, 255}}};
        return spec;
    }
    const auto twoColor = default_gradient_fill(project, fill);
    if (!twoColor) {
        return std::nullopt;
    }
    spec.method = document::GradientMethod::Directional;
    spec.palette = {document::GradientThread{twoColor->background, twoColor->background_thread},
                    document::GradientThread{twoColor->foreground, twoColor->foreground_thread}};
    spec.transition = twoColor->gradient;
    spec.complementary = twoColor->complementary;
    return spec;
}

std::string multicolor_gradient_incompatibility(const document::Project& project, ObjectId fill,
                                                document::GradientMethod method) {
    const auto* base = project.findEmbroidery(fill);
    if (base == nullptr) {
        return "Objet introuvable.";
    }
    if (base->is_multicolor_gradient()) {
        return "Cet objet est déjà l'ancre d'un fondu multicolore.";
    }
    if (base->multi_blend) {
        return "Cet objet appartient déjà à un groupe de fondu multicolore.";
    }
    if (base->color_blend) {
        return "Cet objet appartient déjà à un fondu à deux fils ; modifiez-le depuis son "
               "panneau de fondu plutôt que d'en créer un nouveau.";
    }
    if (!(base->is_tatami() || base->is_directional() || base->is_satin())) {
        return "Choisissez un remplissage (tatami, directionnel ou satin) pour porter un fondu "
               "multicolore.";
    }
    const auto* source = project.findObject(base->source_vector);
    if (source == nullptr || source->paths.empty()) {
        return "Forme source introuvable ou vide.";
    }
    if (method == document::GradientMethod::SatinInterlaced) {
        const auto* satin = std::get_if<document::SatinParams>(&base->params);
        const bool hasRails =
            satin != nullptr && satin->rail_a.nodes.size() >= 2 && satin->rail_b.nodes.size() >= 2;
        if (!hasRails && !stitch_generation::rails_from_contour(source->paths.front().outer)) {
            return "Satin entrelacé : cette forme est trop compacte pour en dériver deux rails "
                   "automatiquement (il lui faut deux côtés allongés). Dessinez un satin à "
                   "rails explicites sur cette forme, ou choisissez une autre méthode.";
        }
    }
    return {};
}

Result<std::unique_ptr<commands::ICommand>>
make_create_multicolor_gradient_command(document::Project& project, ObjectId fill,
                                        const MulticolorGradientSpec& spec) {
    if (!validate_palette(spec.palette)) {
        return fail(ErrorCategory::OperationImpossible, "Choisissez entre 2 et 8 fils distincts.");
    }
    if (const auto why = multicolor_gradient_incompatibility(project, fill, spec.method);
        !why.empty()) {
        return fail(ErrorCategory::OperationImpossible, why);
    }
    const auto* base = project.findEmbroidery(fill);
    return build_gradient_anchor_command(project, *base, spec, false, ObjectId{});
}

Result<std::unique_ptr<commands::ICommand>>
make_edit_multicolor_gradient_command(document::Project& project, ObjectId anchorOrMember,
                                      const MulticolorGradientSpec& spec) {
    if (!validate_palette(spec.palette)) {
        return fail(ErrorCategory::OperationImpossible, "Choisissez entre 2 et 8 fils distincts.");
    }
    const auto group = document::find_gradient_group(project, anchorOrMember);
    if (!group) {
        return fail(ErrorCategory::OperationImpossible, "Aucun fondu multicolore sélectionné.");
    }
    const auto* anchor = project.findEmbroidery(group->anchor);
    const auto* templateBase = project.findEmbroidery(group->layers.front());
    if (anchor == nullptr || templateBase == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Groupe incomplet.");
    }
    const auto* source = project.findObject(anchor->source_vector);
    if (source == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Forme source introuvable.");
    }
    auto layers = gradient_object_layers(*templateBase, source->paths, spec);
    if (!layers) {
        return fail(ErrorCategory::OperationImpossible,
                    "Fondu multicolore invalide pour cette géométrie/méthode.");
    }
    const auto& oldIds = group->layers;
    std::vector<ObjectId> newIds;
    newIds.reserve(layers->size());
    for (std::size_t i = 0; i < layers->size(); ++i) {
        newIds.push_back(i < oldIds.size() ? oldIds[i] : project.object_ids.next());
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Modifier le fondu multicolore");
    const auto newAnchorParams = anchor_params_from_spec(spec);
    if (std::get<document::MulticolorGradientParams>(anchor->params) != newAnchorParams) {
        batch->add(std::make_unique<commands::SetStitchParamsCommand>(
            anchor->id, document::StitchParams{newAnchorParams}, "Fondu multicolore"));
    }
    for (std::size_t i = 0; i < layers->size(); ++i) {
        auto next = std::move((*layers)[i]);
        next.id = newIds[i];
        next.source_vector = anchor->source_vector;
        const document::MultiBlendLink tag{group->anchor, static_cast<std::uint8_t>(i),
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
            next.name = templateBase->name + " (fondu " + std::to_string(i + 1) + ")";
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
make_remove_multicolor_gradient_command(const document::Project& project, ObjectId anchorOrMember) {
    const auto group = document::find_gradient_group(project, anchorOrMember);
    if (!group) {
        return fail(ErrorCategory::OperationImpossible, "Aucun fondu multicolore sélectionné.");
    }
    const auto* anchor = project.findEmbroidery(group->anchor);
    const auto* first = project.findEmbroidery(group->layers.front());
    if (anchor == nullptr || first == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Groupe incomplet.");
    }
    auto batch = std::make_unique<commands::CompositeCommand>("Supprimer le fondu multicolore");
    // L'ancre redevient un remplissage uni (couleur de la première couche), jamais supprimée :
    // la forme reste cousue, seul le fondu disparaît.
    document::StitchParams plain = first->params;
    if (auto* directional = std::get_if<document::DirectionalFillParams>(&plain)) {
        directional->density_gradient.reset();
    }
    batch->add(
        std::make_unique<commands::SetStitchTypeCommand>(anchor->id, plain, "Remplissage uni"));
    for (const auto layerId : group->layers) {
        batch->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(layerId));
    }
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

// --- Adaptateurs historiques (MultiGradientFillSpec, méthode devinée depuis la texture) ---------

namespace {

document::GradientMethod infer_method(const document::EmbroideryObject* base,
                                      document::DensityTexture texture) {
    if (base != nullptr && base->is_satin()) {
        return document::GradientMethod::SatinInterlaced;
    }
    switch (texture) {
    case document::DensityTexture::LongShort:
        return document::GradientMethod::LongAndShort;
    case document::DensityTexture::Stippled:
        return document::GradientMethod::ControlledScatter;
    default:
        return document::GradientMethod::Directional;
    }
}

MulticolorGradientSpec from_legacy_spec(const MultiGradientFillSpec& spec,
                                        const document::EmbroideryObject* base) {
    MulticolorGradientSpec unified;
    unified.method = infer_method(base, spec.gradient.texture);
    unified.transition = spec.gradient;
    unified.complementary = spec.complementary;
    unified.palette.reserve(spec.colors.size());
    for (std::size_t i = 0; i < spec.colors.size(); ++i) {
        document::GradientThread thread;
        thread.rgb = spec.colors[i];
        if (i < spec.layer_curves.size()) {
            thread.curve = spec.layer_curves[i];
        }
        unified.palette.push_back(thread);
    }
    return unified;
}

} // namespace

std::optional<MultiGradientFillSpec>
describe_multicolor_gradient_fill(const document::Project& project, ObjectId layer) {
    const auto full = describe_multicolor_gradient(project, layer);
    if (!full) {
        return std::nullopt;
    }
    MultiGradientFillSpec spec;
    spec.gradient = full->transition;
    spec.complementary = full->complementary;
    bool anyCurve = false;
    for (const auto& thread : full->palette) {
        spec.colors.push_back(thread.rgb);
        spec.layer_curves.push_back(thread.curve);
        anyCurve = anyCurve || thread.curve != document::DensityCurve::Linear;
    }
    if (!anyCurve) {
        spec.layer_curves.clear();
    }
    return spec;
}

Result<std::unique_ptr<commands::ICommand>>
make_create_multicolor_gradient_fill_command(document::Project& project, ObjectId fill,
                                             const MultiGradientFillSpec& spec) {
    if (!validate_multicolor(spec)) {
        return fail(ErrorCategory::OperationImpossible,
                    "Choisissez un remplissage unique et entre 2 et 8 fils distincts.");
    }
    const auto* base = project.findEmbroidery(fill);
    return make_create_multicolor_gradient_command(project, fill, from_legacy_spec(spec, base));
}

Result<std::unique_ptr<commands::ICommand>>
make_edit_multicolor_gradient_fill_command(document::Project& project, ObjectId member,
                                           const MultiGradientFillSpec& spec) {
    if (!validate_multicolor(spec)) {
        return fail(ErrorCategory::OperationImpossible,
                    "Entre 2 et 8 couleurs consécutives distinctes.");
    }
    if (const auto group = document::find_gradient_group(project, member)) {
        // Préserve la méthode et ses réglages propres (absents de `MultiGradientFillSpec`) :
        // ne JAMAIS la redeviner depuis la texture, qui romprait un satin entrelacé existant
        // dès que l'appelant passe `member` = l'ancre (jamais satin elle-même).
        auto unified = from_legacy_spec(spec, project.findEmbroidery(group->layers.front()));
        if (const auto current = describe_multicolor_gradient(project, member)) {
            unified.method = current->method;
            unified.satin_density = current->satin_density;
            unified.crosshatch_layers = current->crosshatch_layers;
            unified.crosshatch_angle = current->crosshatch_angle;
        }
        return make_edit_multicolor_gradient_command(project, member, unified);
    }
    // Ancien fondu à deux fils (ColorBlendLink) : upgrade en un seul pas d'annulation, comme
    // avant -- `build_gradient_anchor_command` déliera le lien dans le MÊME lot.
    const auto legacy = document::find_color_blend(project, member);
    if (!legacy) {
        return fail(ErrorCategory::OperationImpossible, "Aucun fondu de fils éditable.");
    }
    const auto* base = project.findEmbroidery(legacy->background);
    if (base == nullptr) {
        return fail(ErrorCategory::OperationImpossible, "Couche de fond introuvable.");
    }
    // Pas d'appel à `multicolor_gradient_incompatibility` ici : son premier refus serait "déjà un
    // fondu à deux fils", exactement le cas attendu de cette conversion. `build_gradient_anchor_
    // command` échoue proprement (via `gradient_object_layers`) si la géométrie/méthode ne
    // convient pas (ex. satin entrelacé sans rails dérivables).
    auto inner = build_gradient_anchor_command(project, *base, from_legacy_spec(spec, base), true,
                                               legacy->foreground);
    if (!inner) {
        return inner;
    }
    // `build_gradient_anchor_command` ne connaît que `base` (devenu l'ancre) : l'ancienne couche
    // de dessus (`legacy->foreground`) n'est réutilisée par AUCUNE des nouvelles couches
    // physiques (ids tous neufs) -- sans ce retrait, elle resterait un objet fantôme, délié mais
    // jamais supprimé.
    auto batch = std::make_unique<commands::CompositeCommand>("Modifier le fondu multicolore");
    batch->add(std::move(*inner));
    batch->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(legacy->foreground));
    return std::unique_ptr<commands::ICommand>(std::move(batch));
}

Result<std::unique_ptr<commands::ICommand>>
make_remove_multicolor_gradient_fill_command(const document::Project& project, ObjectId member) {
    return make_remove_multicolor_gradient_command(project, member);
}

} // namespace openstitch::project_ops
