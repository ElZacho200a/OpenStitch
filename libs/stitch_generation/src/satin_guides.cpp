// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/satin_guides.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {
namespace {

struct Projection {
    Vec2um point{};
    double station{};
};

double distance2(Vec2um a, Vec2um b) {
    const double dx = static_cast<double>(a.x.value) - b.x.value;
    const double dy = static_cast<double>(a.y.value) - b.y.value;
    return dx * dx + dy * dy;
}

std::optional<Projection> project(const std::vector<Vec2um>& points,
                                  const std::vector<double>& cumulative, Vec2um desired) {
    if (points.size() < 2 || cumulative.size() != points.size()) {
        return std::nullopt;
    }
    double bestDistance2 = std::numeric_limits<double>::infinity();
    Projection best{};
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2um p = points[i];
        const Vec2um q = points[i + 1];
        const double vx = static_cast<double>(q.x.value) - p.x.value;
        const double vy = static_cast<double>(q.y.value) - p.y.value;
        const double length2 = vx * vx + vy * vy;
        double t = 0.0;
        if (length2 > 0.0) {
            const double wx = static_cast<double>(desired.x.value) - p.x.value;
            const double wy = static_cast<double>(desired.y.value) - p.y.value;
            t = std::clamp((wx * vx + wy * vy) / length2, 0.0, 1.0);
        }
        const Vec2um snapped{
            Micrometers{static_cast<std::int32_t>(std::llround(p.x.value + t * vx))},
            Micrometers{static_cast<std::int32_t>(std::llround(p.y.value + t * vy))}};
        const double d2 = distance2(snapped, desired);
        if (d2 < bestDistance2) {
            bestDistance2 = d2;
            best.point = snapped;
            best.station = cumulative[i] + t * std::sqrt(length2);
        }
    }
    return best;
}

bool opposite_orientation(const std::vector<Vec2um>& a, const std::vector<Vec2um>& b) {
    return distance2(a.front(), b.back()) + distance2(a.back(), b.front()) <
           distance2(a.front(), b.front()) + distance2(a.back(), b.back());
}

}  // namespace

std::vector<std::optional<std::uint32_t>> satin_guide_junctions(
    const document::SatinParams& satin, Micrometers flatten_tolerance) {
    std::vector<std::optional<std::uint32_t>> junctions(satin.rungs.size());
    if (!satin.topology || satin.rungs.size() < 2) {
        return junctions;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return junctions;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    const auto cumulativeA = geometry::cumulative_lengths(railA);
    const auto cumulativeB = geometry::cumulative_lengths(railB);
    struct Anchor {
        double station_sum{};
        std::size_t original_index{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto pa = project(railA, cumulativeA, satin.rungs[i].a);
        const auto pb = project(railB, cumulativeB, satin.rungs[i].b);
        if (!pa || !pb) {
            return junctions;
        }
        anchors.push_back({pa->station + pb->station, i});
    }
    std::stable_sort(anchors.begin(), anchors.end(), [](const Anchor& lhs, const Anchor& rhs) {
        return lhs.station_sum < rhs.station_sum;
    });
    if (satin.topology->start_junction) {
        junctions[anchors.front().original_index] = satin.topology->start_junction;
    }
    if (satin.topology->end_junction) {
        junctions[anchors.back().original_index] = satin.topology->end_junction;
    }
    return junctions;
}

std::optional<std::uint32_t> satin_guide_junction(const document::SatinParams& satin,
                                                  std::size_t guide_index,
                                                  Micrometers flatten_tolerance) {
    if (guide_index >= satin.rungs.size()) {
        return std::nullopt;
    }
    return satin_guide_junctions(satin, flatten_tolerance)[guide_index];
}

std::vector<SatinJunctionGuideRef> satin_junction_guides(
    const document::Project& project, ObjectId source_vector, std::uint32_t junction_id,
    Micrometers flatten_tolerance) {
    std::vector<SatinJunctionGuideRef> result;
    if (!source_vector.valid()) {
        return result;
    }
    std::optional<std::uint32_t> sectionCount;
    std::vector<std::uint32_t> sectionIndices;
    for (const auto& object : project.embroidery_objects) {
        if (object.source_vector != source_vector) {
            continue;
        }
        const auto* satin = std::get_if<document::SatinParams>(&object.params);
        if (satin == nullptr || !satin->topology) {
            continue;
        }
        const auto& topology = *satin->topology;
        if (!sectionCount) {
            sectionCount = topology.section_count;
        }
        if (topology.section_count == 0 || topology.section_count != *sectionCount ||
            topology.section_index >= topology.section_count ||
            std::find(sectionIndices.begin(), sectionIndices.end(), topology.section_index) !=
                sectionIndices.end()) {
            return {};
        }
        sectionIndices.push_back(topology.section_index);
        const std::size_t expectedEnds =
            static_cast<std::size_t>(topology.start_junction == junction_id) +
            static_cast<std::size_t>(topology.end_junction == junction_id);
        if (expectedEnds == 0) {
            continue;
        }
        const auto junctions = satin_guide_junctions(*satin, flatten_tolerance);
        std::size_t foundEnds = 0;
        for (std::size_t i = 0; i < junctions.size(); ++i) {
            if (junctions[i] == junction_id) {
                result.push_back(
                    {object.id, i, topology.section_index});
                ++foundEnds;
            }
        }
        if (foundEnds != expectedEnds) {
            return {};
        }
    }
    if (!sectionCount || sectionIndices.size() != *sectionCount) {
        return {};
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.section_index != rhs.section_index) {
            return lhs.section_index < rhs.section_index;
        }
        if (lhs.embroidery_id != rhs.embroidery_id) {
            return lhs.embroidery_id < rhs.embroidery_id;
        }
        return lhs.guide_index < rhs.guide_index;
    });
    return result;
}

std::vector<SatinJunctionGuideRef> satin_linked_guides(const document::Project& project,
                                                       ObjectId source_vector,
                                                       std::uint32_t link_id) {
    std::vector<SatinJunctionGuideRef> result;
    if (!source_vector.valid()) {
        return result;
    }
    std::optional<std::uint32_t> sectionCount;
    std::vector<std::uint32_t> sectionIndices;
    for (const auto& object : project.embroidery_objects) {
        if (object.source_vector != source_vector) {
            continue;
        }
        const auto* satin = std::get_if<document::SatinParams>(&object.params);
        if (satin == nullptr || !satin->topology) {
            continue;
        }
        const auto& topology = *satin->topology;
        if (!sectionCount) {
            sectionCount = topology.section_count;
        }
        if (topology.section_count == 0 || topology.section_count != *sectionCount ||
            topology.section_index >= topology.section_count ||
            std::find(sectionIndices.begin(), sectionIndices.end(), topology.section_index) !=
                sectionIndices.end()) {
            return {};
        }
        std::optional<std::size_t> matchIndex;
        for (std::size_t i = 0; i < satin->rungs.size(); ++i) {
            if (satin->rungs[i].link_id == link_id) {
                if (matchIndex) {
                    return {}; // deux guides du meme link_id dans une section : corrompu
                }
                matchIndex = i;
            }
        }
        sectionIndices.push_back(topology.section_index);
        if (!matchIndex) {
            continue;
        }
        result.push_back({object.id, *matchIndex, topology.section_index});
    }
    if (!sectionCount || sectionIndices.size() != *sectionCount || result.size() < 2) {
        return {}; // un groupe lie relie au moins deux sections
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.section_index != rhs.section_index) {
            return lhs.section_index < rhs.section_index;
        }
        if (lhs.embroidery_id != rhs.embroidery_id) {
            return lhs.embroidery_id < rhs.embroidery_id;
        }
        return lhs.guide_index < rhs.guide_index;
    });
    return result;
}

std::optional<std::uint32_t> next_satin_guide_link_id(const document::Project& project,
                                                      ObjectId source_vector) {
    if (!source_vector.valid()) {
        return std::nullopt;
    }
    std::uint32_t next = 0;
    for (const auto& object : project.embroidery_objects) {
        if (object.source_vector != source_vector) {
            continue;
        }
        const auto* satin = std::get_if<document::SatinParams>(&object.params);
        if (satin == nullptr) {
            continue;
        }
        for (const auto& rung : satin->rungs) {
            if (!rung.link_id) {
                continue;
            }
            if (*rung.link_id == std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            next = std::max(next, static_cast<std::uint32_t>(*rung.link_id + 1));
        }
    }
    return next;
}

std::optional<document::SatinRung> move_satin_guide_endpoint(
    const document::SatinParams& satin, std::size_t guide_index, SatinGuideSide side,
    Vec2um desired, Micrometers flatten_tolerance) {
    if (guide_index >= satin.rungs.size() || satin.rungs.size() < 2) {
        return std::nullopt;
    }
    if (satin_guide_junction(satin, guide_index, flatten_tolerance)) {
        return std::nullopt;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return std::nullopt;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    const auto cumulativeA = geometry::cumulative_lengths(railA);
    const auto cumulativeB = geometry::cumulative_lengths(railB);

    document::SatinRung candidate = satin.rungs[guide_index];
    const auto moved = side == SatinGuideSide::RailA
                           ? project(railA, cumulativeA, desired)
                           : project(railB, cumulativeB, desired);
    if (!moved) {
        return std::nullopt;
    }
    (side == SatinGuideSide::RailA ? candidate.a : candidate.b) = moved->point;

    struct Anchor {
        double sa{};
        double sb{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto& rung = i == guide_index ? candidate : satin.rungs[i];
        const auto pa = project(railA, cumulativeA, rung.a);
        const auto pb = project(railB, cumulativeB, rung.b);
        if (!pa || !pb) {
            return std::nullopt;
        }
        anchors.push_back({pa->station, pb->station});
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const Anchor& lhs, const Anchor& rhs) {
                         return lhs.sa + lhs.sb < rhs.sa + rhs.sb;
                     });
    const double minimumGap = std::max(1.0, static_cast<double>(satin.density.value) * 0.5);
    for (std::size_t i = 1; i < anchors.size(); ++i) {
        if (anchors[i].sa <= anchors[i - 1].sa + minimumGap ||
            anchors[i].sb <= anchors[i - 1].sb + minimumGap) {
            return std::nullopt;
        }
    }
    return candidate;
}

std::optional<SatinGuideInsertion> make_satin_guide_in_largest_gap(
    const document::SatinParams& satin, Micrometers flatten_tolerance) {
    if (satin.rungs.size() < 2) {
        return std::nullopt;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return std::nullopt;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    const auto cumulativeA = geometry::cumulative_lengths(railA);
    const auto cumulativeB = geometry::cumulative_lengths(railB);
    struct Anchor {
        double sa{};
        double sb{};
        std::size_t original_index{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto& rung = satin.rungs[i];
        const auto pa = project(railA, cumulativeA, rung.a);
        const auto pb = project(railB, cumulativeB, rung.b);
        if (!pa || !pb) {
            return std::nullopt;
        }
        anchors.push_back({pa->station, pb->station, i});
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const Anchor& lhs, const Anchor& rhs) {
                         return lhs.sa + lhs.sb < rhs.sa + rhs.sb;
                     });

    std::size_t best = 0;
    double bestMedialGap = -1.0;
    for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
        const double da = anchors[i + 1].sa - anchors[i].sa;
        const double db = anchors[i + 1].sb - anchors[i].sb;
        const double medialGap = 0.5 * (da + db);
        if (da > 0.0 && db > 0.0 && medialGap > bestMedialGap) {
            best = i;
            bestMedialGap = medialGap;
        }
    }
    const double minimumGap = std::max(1.0, static_cast<double>(satin.density.value) * 0.5);
    if (bestMedialGap < 0.0 ||
        anchors[best + 1].sa - anchors[best].sa <= 2.0 * minimumGap ||
        anchors[best + 1].sb - anchors[best].sb <= 2.0 * minimumGap) {
        return std::nullopt;
    }
    const double sa = 0.5 * (anchors[best].sa + anchors[best + 1].sa);
    const double sb = 0.5 * (anchors[best].sb + anchors[best + 1].sb);
    // Les colonnes produites par l'auto-satin stockent les guides dans l'ordre
    // de progression (croissant ou décroissant). Insérer entre les deux indices
    // d'origine préserve cet ordre et garde front()/back() représentatifs pour
    // le routage textile. Le générateur reste lui-même tolérant aux imports non
    // triés, mais l'éditeur ne doit pas en créer de nouveaux.
    const std::size_t insertionIndex =
        std::max(anchors[best].original_index, anchors[best + 1].original_index);
    return SatinGuideInsertion{
        document::SatinRung{geometry::point_at_length(railA, cumulativeA, sa),
                            geometry::point_at_length(railB, cumulativeB, sb)},
        insertionIndex};
}

std::optional<SatinGuideInsertion> make_satin_guide_next_to_junction(
    const document::SatinParams& satin, std::size_t junction_guide_index,
    Micrometers flatten_tolerance) {
    if (satin.rungs.size() < 2 || junction_guide_index >= satin.rungs.size() ||
        !satin_guide_junction(satin, junction_guide_index, flatten_tolerance)) {
        return std::nullopt;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return std::nullopt;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    const auto cumulativeA = geometry::cumulative_lengths(railA);
    const auto cumulativeB = geometry::cumulative_lengths(railB);
    struct Anchor {
        double sa{};
        double sb{};
        std::size_t original_index{};
    };
    std::vector<Anchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto pa = project(railA, cumulativeA, satin.rungs[i].a);
        const auto pb = project(railB, cumulativeB, satin.rungs[i].b);
        if (!pa || !pb) {
            return std::nullopt;
        }
        anchors.push_back({pa->station, pb->station, i});
    }
    std::stable_sort(anchors.begin(), anchors.end(), [](const Anchor& lhs, const Anchor& rhs) {
        return lhs.sa + lhs.sb < rhs.sa + rhs.sb;
    });
    const auto junction = std::find_if(anchors.begin(), anchors.end(), [&](const Anchor& anchor) {
        return anchor.original_index == junction_guide_index;
    });
    if (junction == anchors.end() ||
        (junction != anchors.begin() && junction + 1 != anchors.end())) {
        return std::nullopt;
    }
    const Anchor& adjacent =
        junction == anchors.begin() ? anchors[1] : anchors[anchors.size() - 2];
    const Anchor& first = junction == anchors.begin() ? *junction : adjacent;
    const Anchor& second = junction == anchors.begin() ? adjacent : *junction;
    const double minimumGap = std::max(1.0, static_cast<double>(satin.density.value) * 0.5);
    if (second.sa - first.sa <= 2.0 * minimumGap ||
        second.sb - first.sb <= 2.0 * minimumGap) {
        return std::nullopt;
    }
    const double sa = 0.5 * (first.sa + second.sa);
    const double sb = 0.5 * (first.sb + second.sb);
    return SatinGuideInsertion{
        document::SatinRung{geometry::point_at_length(railA, cumulativeA, sa),
                            geometry::point_at_length(railB, cumulativeB, sb)},
        std::max(first.original_index, second.original_index)};
}

namespace {

struct GroupAnchor {
    double sa{};
    double sb{};
    std::size_t original_index{};
};

struct GroupInterval {
    GroupAnchor junction;
    GroupAnchor other;
    std::uint32_t junction_id{};
};

// Un guide de groupe est toujours créé collé à un guide de jonction (extrémité
// du tableau trié) : son intervalle admissible est donc borné par cette
// jonction d'un côté et par son unique voisin de l'autre, exactement comme au
// moment de sa création (make_satin_guide_next_to_junction). Si la topologie a
// bougé depuis (un autre guide inséré entre les deux, jonction disparue), le
// guide n'a plus de voisin de jonction univoque : on refuse plutôt que de
// deviner un intervalle arbitraire.
std::optional<GroupInterval> find_group_interval(const document::SatinParams& satin,
                                                 std::size_t guide_index,
                                                 const std::vector<GroupAnchor>& anchors,
                                                 Micrometers flatten_tolerance) {
    if (anchors.size() < 3) {
        return std::nullopt;
    }
    const auto pos = std::find_if(anchors.begin(), anchors.end(), [&](const GroupAnchor& anchor) {
        return anchor.original_index == guide_index;
    });
    if (pos == anchors.end()) {
        return std::nullopt;
    }
    const auto idx = static_cast<std::size_t>(pos - anchors.begin());
    if (idx == 1) {
        if (const auto junction =
                satin_guide_junction(satin, anchors.front().original_index, flatten_tolerance)) {
            return GroupInterval{anchors.front(), anchors[idx + 1], *junction};
        }
    }
    if (idx + 2 == anchors.size()) {
        if (const auto junction =
                satin_guide_junction(satin, anchors.back().original_index, flatten_tolerance)) {
            return GroupInterval{anchors.back(), anchors[idx - 1], *junction};
        }
    }
    return std::nullopt;
}

struct GroupContext {
    std::vector<Vec2um> railA;
    std::vector<Vec2um> railB;
    std::vector<double> cumulativeA;
    std::vector<double> cumulativeB;
    GroupInterval interval;
};

std::optional<GroupContext> resolve_group_context(const document::SatinParams& satin,
                                                  std::size_t guide_index,
                                                  Micrometers flatten_tolerance) {
    if (satin.rungs.size() < 3 || guide_index >= satin.rungs.size()) {
        return std::nullopt;
    }
    auto railA = geometry::flatten(satin.rail_a, flatten_tolerance).points;
    auto railB = geometry::flatten(satin.rail_b, flatten_tolerance).points;
    if (railA.size() < 2 || railB.size() < 2) {
        return std::nullopt;
    }
    if (opposite_orientation(railA, railB)) {
        std::reverse(railB.begin(), railB.end());
    }
    auto cumulativeA = geometry::cumulative_lengths(railA);
    auto cumulativeB = geometry::cumulative_lengths(railB);
    std::vector<GroupAnchor> anchors;
    anchors.reserve(satin.rungs.size());
    for (std::size_t i = 0; i < satin.rungs.size(); ++i) {
        const auto pa = project(railA, cumulativeA, satin.rungs[i].a);
        const auto pb = project(railB, cumulativeB, satin.rungs[i].b);
        if (!pa || !pb) {
            return std::nullopt;
        }
        anchors.push_back({pa->station, pb->station, i});
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const GroupAnchor& lhs, const GroupAnchor& rhs) {
                         return lhs.sa + lhs.sb < rhs.sa + rhs.sb;
                     });
    for (std::size_t i = 1; i < anchors.size(); ++i) {
        if (anchors[i].sa <= anchors[i - 1].sa || anchors[i].sb <= anchors[i - 1].sb) {
            return std::nullopt; // ordre croisé, replié ou station dupliquée
        }
    }
    auto interval = find_group_interval(satin, guide_index, anchors, flatten_tolerance);
    if (!interval) {
        return std::nullopt;
    }
    return GroupContext{std::move(railA), std::move(railB), std::move(cumulativeA),
                        std::move(cumulativeB), *interval};
}

} // namespace

std::optional<std::vector<SatinGuideGroupEdit>>
move_satin_guide_group(const document::Project& doc, ObjectId source_vector, std::uint32_t link_id,
                       ObjectId dragged_id, SatinGuideSide dragged_side, Vec2um desired,
                       Micrometers flatten_tolerance) {
    const auto refs = satin_linked_guides(doc, source_vector, link_id);
    if (refs.size() < 2) {
        return std::nullopt;
    }

    struct SectionState {
        ObjectId id{};
        std::size_t guide_index{};
        GroupContext context;
        double currentTA{};
        double currentTB{};
        double minimumGap{};
    };
    std::vector<SectionState> sections;
    sections.reserve(refs.size());
    std::optional<double> deltaT;
    std::optional<std::uint32_t> sharedJunction;

    for (const auto& ref : refs) {
        const auto* object = doc.findEmbroidery(ref.embroidery_id);
        const auto* satin =
            object != nullptr ? std::get_if<document::SatinParams>(&object->params) : nullptr;
        if (satin == nullptr || ref.guide_index >= satin->rungs.size()) {
            return std::nullopt;
        }
        auto context = resolve_group_context(*satin, ref.guide_index, flatten_tolerance);
        if (!context) {
            return std::nullopt;
        }
        if (!sharedJunction) {
            sharedJunction = context->interval.junction_id;
        } else if (*sharedJunction != context->interval.junction_id) {
            return std::nullopt; // link_id corrompu : jonctions différentes
        }
        const auto& rung = satin->rungs[ref.guide_index];
        const auto currentA = project(context->railA, context->cumulativeA, rung.a);
        const auto currentB = project(context->railB, context->cumulativeB, rung.b);
        if (!currentA || !currentB) {
            return std::nullopt;
        }
        const double spanA = context->interval.other.sa - context->interval.junction.sa;
        const double spanB = context->interval.other.sb - context->interval.junction.sb;
        const double minimumGap = std::max(1.0, static_cast<double>(satin->density.value) * 0.5);
        if (std::abs(spanA) <= 2.0 * minimumGap || std::abs(spanB) <= 2.0 * minimumGap) {
            return std::nullopt; // intervalle degenere ou trop court
        }
        const double tA = (currentA->station - context->interval.junction.sa) / spanA;
        const double tB = (currentB->station - context->interval.junction.sb) / spanB;

        if (ref.embroidery_id == dragged_id) {
            // Seule donnée géométrique brute qui traverse la frontière de
            // section : la position glissée projetée sur SON PROPRE rail,
            // convertie en un delta normalisé partagé (jamais une coordonnée
            // recopiée telle quelle vers les autres sections).
            const auto& dragRail =
                dragged_side == SatinGuideSide::RailA ? context->railA : context->railB;
            const auto& dragCumulative =
                dragged_side == SatinGuideSide::RailA ? context->cumulativeA : context->cumulativeB;
            const double junctionStation = dragged_side == SatinGuideSide::RailA
                                               ? context->interval.junction.sa
                                               : context->interval.junction.sb;
            const double span = dragged_side == SatinGuideSide::RailA ? spanA : spanB;
            const double currentT = dragged_side == SatinGuideSide::RailA ? tA : tB;
            const auto desiredProjection = project(dragRail, dragCumulative, desired);
            if (!desiredProjection) {
                return std::nullopt;
            }
            deltaT = (desiredProjection->station - junctionStation) / span - currentT;
        }
        sections.push_back(
            {ref.embroidery_id, ref.guide_index, std::move(*context), tA, tB, minimumGap});
    }
    if (!deltaT) {
        return std::nullopt; // la section glissée n'appartient pas au groupe
    }

    std::vector<SatinGuideGroupEdit> edits;
    edits.reserve(sections.size());
    for (const auto& section : sections) {
        const double spanA =
            section.context.interval.other.sa - section.context.interval.junction.sa;
        const double spanB =
            section.context.interval.other.sb - section.context.interval.junction.sb;
        const double boundA = section.minimumGap / std::abs(spanA);
        const double boundB = section.minimumGap / std::abs(spanB);
        const double newTA = section.currentTA + *deltaT;
        const double newTB = section.currentTB + *deltaT;
        if (newTA <= boundA || newTA >= 1.0 - boundA || newTB <= boundB || newTB >= 1.0 - boundB) {
            return std::nullopt; // sortirait de l'intervalle admissible : rejet total
        }
        const double sa = section.context.interval.junction.sa + newTA * spanA;
        const double sb = section.context.interval.junction.sb + newTB * spanB;
        document::SatinRung candidate{
            geometry::point_at_length(section.context.railA, section.context.cumulativeA, sa),
            geometry::point_at_length(section.context.railB, section.context.cumulativeB, sb)};
        candidate.link_id = link_id;
        edits.push_back({section.id, section.guide_index, candidate});
    }
    return edits;
}

}  // namespace openstitch::stitch_generation
