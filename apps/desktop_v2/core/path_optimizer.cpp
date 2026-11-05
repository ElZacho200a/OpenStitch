// SPDX-License-Identifier: Apache-2.0
#include "path_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

namespace {

[[nodiscard]] optimization::OrderStrategy engine(PathStrategy s) {
    switch (s) {
    case PathStrategy::ByColor:
        return optimization::OrderStrategy::ByColor;
    case PathStrategy::ByProximity:
        return optimization::OrderStrategy::ByProximity;
    case PathStrategy::ColorThenProximity:
        return optimization::OrderStrategy::ColorThenProximity;
    case PathStrategy::Layered:
        return optimization::OrderStrategy::LayeredColorThenProximity;
    }
    return optimization::OrderStrategy::Document;
}

// Centre et aire de la forme source d'un objet (repli : origine / aire nulle).
struct Footprint {
    Vec2um centre{};
    double areaMm2{0.0};
};

[[nodiscard]] Footprint footprintOf(const document::Project& project,
                                    const document::EmbroideryObject& object) {
    Footprint out;
    const auto* vector = project.findObject(object.source_vector);
    if (vector == nullptr) {
        return out;
    }
    std::int64_t minX = 0;
    std::int64_t maxX = 0;
    std::int64_t minY = 0;
    std::int64_t maxY = 0;
    bool first = true;
    for (const auto& set : vector->paths) {
        out.areaMm2 += std::abs(geometry::signed_area_um2(set.outer)) / 1e6;
        for (const auto& node : set.outer.nodes) {
            const std::int64_t x = node.pos.x.value;
            const std::int64_t y = node.pos.y.value;
            minX = first ? x : std::min(minX, x);
            maxX = first ? x : std::max(maxX, x);
            minY = first ? y : std::min(minY, y);
            maxY = first ? y : std::max(maxY, y);
            first = false;
        }
    }
    if (!first) {
        out.centre = Vec2um{Micrometers{static_cast<std::int32_t>((minX + maxX) / 2)},
                            Micrometers{static_cast<std::int32_t>((minY + maxY) / 2)}};
    }
    return out;
}

[[nodiscard]] PathMetrics measure(const stitch::StitchSequence& sequence) {
    const auto stats = stitch::compute_stats(sequence);
    PathMetrics m;
    m.stitches = stats.stitches;
    m.jumps = stats.jumps;
    m.trims = stats.trims;
    m.colorChanges = stats.color_changes;
    m.threadM = stats.thread_length_um / 1e6;
    // Distance à vide entre objets successifs, d'après les extrémités réelles de couture.
    const auto endpoints = stitch::object_endpoints(sequence);
    std::vector<optimization::SewnSpan> spans;
    spans.reserve(endpoints.size());
    for (const auto& e : endpoints) {
        spans.push_back({e.first, e.last});
    }
    m.jumpLengthMm =
        static_cast<double>(optimization::jump_length_um(optimization::jump_segments(spans))) /
        1000.0;
    return m;
}

[[nodiscard]] std::string delta(const char* label, double before, double after, const char* unit,
                                int decimals) {
    char buffer[200];
    std::snprintf(buffer, sizeof buffer, "%s : %.*f → %.*f %s (%+.*f)", label, decimals, before,
                  decimals, after, unit, decimals, after - before);
    return buffer;
}

} // namespace

std::string pathStrategyLabel(PathStrategy strategy) {
    switch (strategy) {
    case PathStrategy::ByColor:
        return "Regrouper les couleurs";
    case PathStrategy::ByProximity:
        return "Réduire les déplacements";
    case PathStrategy::ColorThenProximity:
        return "Couleurs puis proximité";
    case PathStrategy::Layered:
        return "Fond d'abord, détails ensuite";
    }
    return {};
}

bool PathProposal::improves() const {
    if (!changes) {
        return false;
    }
    const bool better = after.colorChanges < before.colorChanges || after.jumps < before.jumps ||
                        after.trims < before.trims ||
                        after.jumpLengthMm + 1e-6 < before.jumpLengthMm;
    const bool worse = after.colorChanges > before.colorChanges || after.jumps > before.jumps ||
                       after.trims > before.trims ||
                       after.jumpLengthMm > before.jumpLengthMm + 1e-6;
    return better && !worse;
}

std::vector<std::string> PathProposal::summary() const {
    std::vector<std::string> out;
    out.push_back(delta("Changements de fil", static_cast<double>(before.colorChanges),
                        static_cast<double>(after.colorChanges), "", 0));
    out.push_back(
        delta("Sauts", static_cast<double>(before.jumps), static_cast<double>(after.jumps), "", 0));
    out.push_back(delta("Coupes", static_cast<double>(before.trims),
                        static_cast<double>(after.trims), "", 0));
    out.push_back(delta("Déplacements à vide", before.jumpLengthMm, after.jumpLengthMm, "mm", 1));
    out.push_back(delta("Points", static_cast<double>(before.stitches),
                        static_cast<double>(after.stitches), "", 0));
    return out;
}

Result<PathProposal> proposeOrder(std::shared_ptr<const document::Project> project,
                                  std::uint64_t revision, PathStrategy strategy, bool improve,
                                  const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Optimisation demandée sans projet");
    }
    if (project->embroidery_objects.size() < 2) {
        return fail(ErrorCategory::OperationImpossible, "Rien à réordonner",
                    "Il faut au moins deux objets brodés pour optimiser l'ordre.");
    }
    auto currentSequence = stitch_generation::effective_sequence(*project, task);
    if (!currentSequence) {
        return std::unexpected(currentSequence.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    PathProposal proposal;
    proposal.strategy = strategy;
    proposal.improve = improve;
    proposal.revision = revision;
    proposal.before = measure(*currentSequence);

    const auto endpoints = stitch::object_endpoints(*currentSequence);
    std::vector<optimization::OrderItem> items;
    for (const std::size_t index : project->sewingOrder()) {
        const auto& object = project->embroidery_objects[index];
        const auto footprint = footprintOf(*project, object);
        optimization::OrderItem item{object.id, object.rgb, footprint.centre, object.locked};
        item.area_mm2 = footprint.areaMm2;
        for (const auto& e : endpoints) {
            if (e.id == object.id) {
                item.entry = e.first;
                item.exit = e.last;
                break;
            }
        }
        items.push_back(item);
    }
    optimization::OrderOptions options;
    options.start = project->finishing.design_start;
    options.end = project->finishing.design_end;
    if (improve) {
        options.improve_budget = optimization::kDefaultImproveBudget;
    }
    const auto order =
        project->groupedByCalque(optimization::optimize_order(items, engine(strategy), options));
    for (const auto id : order) {
        proposal.order.push_back(id.value);
    }
    std::vector<std::uint64_t> current;
    for (const auto& object : project->embroidery_objects) {
        current.push_back(object.id.value);
    }
    proposal.changes = proposal.order != current;
    if (task.cancelled()) {
        return cancelled_error();
    }

    // Mesure RÉELLE de l'ordre proposé, sur une copie : jamais sur le document vivant.
    document::Project copy = *project;
    commands::ReorderEmbroideryCommand reorder(order);
    reorder.apply(copy);
    auto proposed = stitch_generation::effective_sequence(copy, task);
    if (!proposed) {
        return std::unexpected(proposed.error());
    }
    proposal.after = measure(*proposed);
    return proposal;
}

ServiceResult applyProposal(DocumentSession& session, const PathProposal& proposal) {
    if (proposal.revision != session.revision()) {
        return {false, "Le document a changé depuis le calcul : relancez l'optimisation."};
    }
    if (!proposal.changes) {
        return {false, "Cet ordre est déjà celui proposé : rien à changer."};
    }
    std::vector<ObjectId> order;
    order.reserve(proposal.order.size());
    for (const auto id : proposal.order) {
        order.push_back(ObjectId{id});
    }
    session.execute(std::make_unique<commands::ReorderEmbroideryCommand>(std::move(order)));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
