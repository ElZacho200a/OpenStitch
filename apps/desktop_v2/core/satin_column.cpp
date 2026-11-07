// SPDX-License-Identifier: Apache-2.0
#include "satin_column.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr double kMaxCoordMm = 1000.0;
constexpr std::size_t kMaxRungs = 500;

ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}

bool inRange(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) <= kMaxCoordMm &&
           std::fabs(p.y) <= kMaxCoordMm;
}

Vec2um toUm(PointMm p) {
    return {to_micrometers(Millimeters{p.x}), to_micrometers(Millimeters{p.y})};
}

const document::SatinParams* satinOf(const document::Project& project, std::uint64_t id) {
    const auto* obj = project.findEmbroidery(ObjectId{id});
    return obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
}

const geometry::Path& railOf(const document::SatinParams& p, RailSide side) {
    return side == RailSide::A ? p.rail_a : p.rail_b;
}

commands::SatinRailSide engineSide(RailSide side) {
    return side == RailSide::A ? commands::SatinRailSide::RailA : commands::SatinRailSide::RailB;
}

SatinCreateResult failure(std::string why) {
    return {false, std::move(why), 0, false};
}

PointMm toMmPoint(Vec2um p) {
    return {to_millimeters(p.x).value, to_millimeters(p.y).value};
}

// Produit vectoriel (b-a)x(c-a) en entiers 64 bits : aucun flottant pour décider d'un croisement.
std::int64_t orient(Vec2um a, Vec2um b, Vec2um c) {
    return (static_cast<std::int64_t>(b.x.value) - a.x.value) *
               (static_cast<std::int64_t>(c.y.value) - a.y.value) -
           (static_cast<std::int64_t>(b.y.value) - a.y.value) *
               (static_cast<std::int64_t>(c.x.value) - a.x.value);
}

// Croisement STRICT de [p1 p2] et [q1 q2] (un contact aux extrémités n'est pas un croisement).
bool segmentsCross(Vec2um p1, Vec2um p2, Vec2um q1, Vec2um q2) {
    const auto d1 = orient(q1, q2, p1);
    const auto d2 = orient(q1, q2, p2);
    const auto d3 = orient(p1, p2, q1);
    const auto d4 = orient(p1, p2, q2);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
} // namespace

SatinCreateResult createSatinColumn(DocumentSession& session,
                                    const std::vector<std::pair<PointMm, PointMm>>& rungs) {
    if (rungs.size() < 2) {
        return failure("Une colonne satin demande au moins deux paires de points.");
    }
    if (rungs.size() > kMaxRungs) {
        return failure("Trop de barreaux (500 au maximum).");
    }
    geometry::Path railA;
    geometry::Path railB;
    railA.closed = false;
    railB.closed = false;
    std::vector<document::SatinRung> rungList;
    for (const auto& [a, b] : rungs) {
        if (!inRange(a) || !inRange(b)) {
            return failure("Point hors limites (±1000 mm).");
        }
        const Vec2um ua = toUm(a);
        const Vec2um ub = toUm(b);
        if (ua == ub) {
            return failure("Les deux rails se touchent : écartez les points d'une paire.");
        }
        railA.nodes.push_back(geometry::PathNode{ua, geometry::NodeType::Corner, {}, {}});
        railB.nodes.push_back(geometry::PathNode{ub, geometry::NodeType::Corner, {}, {}});
        rungList.push_back(document::SatinRung{ua, ub, std::nullopt});
    }
    const auto flatA = geometry::flatten(railA, Micrometers{50});
    const auto flatB = geometry::flatten(railB, Micrometers{50});
    const bool cross = geometry::polylines_cross(flatA.points, flatB.points);

    // Contour source synthétique (rail A aller, rail B retour) : seulement le rattachement
    // `source_vector` exigé par l'objet ; les rails de SatinParams restent la géométrie réelle.
    geometry::Path outline;
    outline.closed = true;
    outline.nodes = railA.nodes;
    for (auto it = railB.nodes.rbegin(); it != railB.nodes.rend(); ++it) {
        outline.nodes.push_back(*it);
    }
    auto& ids = session.projectForIdAllocation().object_ids;
    document::VectorObject vec;
    vec.id = ids.next();
    vec.name = "Colonne satin (rails)";
    vec.rgb = {200, 90, 40};
    vec.visible = false;
    vec.paths.push_back(geometry::PathSet{std::move(outline), {}});

    document::EmbroideryObject emb;
    emb.id = ids.next();
    emb.name = "Colonne satin manuelle";
    emb.source_vector = vec.id;
    emb.rgb = {200, 90, 40};
    document::SatinParams params;
    params.rail_a = std::move(railA);
    params.rail_b = std::move(railB);
    params.rungs = std::move(rungList);
    params.split_stitch = document::SatinSplit::Staggered;
    emb.params = std::move(params);
    const auto newId = emb.id.value;

    std::vector<document::VectorObject> vectors;
    vectors.push_back(std::move(vec));
    std::vector<document::EmbroideryObject> embs;
    embs.push_back(std::move(emb));
    session.execute(std::make_unique<commands::AddObjectBatchCommand>(
        std::move(vectors), std::move(embs), "Colonne satin (création manuelle)"));
    return {true, {}, newId, cross};
}

int railNodeCount(const document::Project& project, std::uint64_t objectId, RailSide side) {
    const auto* satin = satinOf(project, objectId);
    return satin == nullptr ? -1 : static_cast<int>(railOf(*satin, side).nodes.size());
}

std::vector<PointMm> satinRailNodes(const document::Project& project, std::uint64_t objectId,
                                    RailSide side) {
    std::vector<PointMm> out;
    if (const auto* satin = satinOf(project, objectId); satin != nullptr) {
        for (const auto& node : railOf(*satin, side).nodes) {
            out.push_back(toMmPoint(node.pos));
        }
    }
    return out;
}

ServiceResult moveSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                std::size_t node, PointMm to) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    const auto& rail = railOf(*satin, side);
    if (node >= rail.nodes.size()) {
        return refuse("Nœud inconnu.");
    }
    if (!inRange(to)) {
        return refuse("Position hors limites (±1000 mm).");
    }
    session.execute(std::make_unique<commands::MoveSatinRailNodeCommand>(
        ObjectId{objectId}, engineSide(side), node, rail.nodes[node].pos, toUm(to)));
    return {true, {}};
}

ServiceResult insertSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                  std::size_t segment, double t) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    const auto& rail = railOf(*satin, side);
    if (rail.nodes.size() < 2) {
        return refuse("Segment inconnu.");
    }
    const std::size_t segments = rail.closed ? rail.nodes.size() : rail.nodes.size() - 1;
    if (segment >= segments) {
        return refuse("Segment inconnu.");
    }
    if (!(t > 0.0 && t < 1.0)) {
        return refuse("Le nœud doit tomber à l'intérieur du segment (0 < t < 1).");
    }
    session.execute(std::make_unique<commands::InsertSatinRailNodeCommand>(
        ObjectId{objectId}, engineSide(side), segment, t));
    return {true, {}};
}

} // namespace openstitch::desktop_v2

namespace openstitch::desktop_v2 {

int satinRungCount(const document::Project& project, std::uint64_t objectId) {
    const auto* satin = satinOf(project, objectId);
    return satin == nullptr ? -1 : static_cast<int>(satin->rungs.size());
}

std::vector<std::pair<PointMm, PointMm>> satinRungs(const document::Project& project,
                                                    std::uint64_t objectId) {
    std::vector<std::pair<PointMm, PointMm>> out;
    if (const auto* satin = satinOf(project, objectId); satin != nullptr) {
        for (const auto& rung : satin->rungs) {
            out.emplace_back(toMmPoint(rung.a), toMmPoint(rung.b));
        }
    }
    return out;
}

ServiceResult addSatinRung(DocumentSession& session, std::uint64_t objectId, std::size_t index,
                           PointMm a, PointMm b) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    if (index > satin->rungs.size()) {
        return refuse("Position de barreau inconnue.");
    }
    if (satin->rungs.size() >= kMaxRungs) {
        return refuse("Trop de barreaux (500 au maximum).");
    }
    if (!inRange(a) || !inRange(b)) {
        return refuse("Point hors limites (±1000 mm).");
    }
    if (toUm(a) == toUm(b)) {
        return refuse("Les deux extrémités du barreau sont confondues.");
    }
    session.execute(std::make_unique<commands::AddSatinGuideCommand>(
        ObjectId{objectId}, document::SatinRung{toUm(a), toUm(b), std::nullopt}, index));
    return {true, {}};
}

ServiceResult insertSatinRungBetween(DocumentSession& session, std::uint64_t objectId,
                                     std::size_t index) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    if (index + 1 >= satin->rungs.size()) {
        return refuse("Il faut deux barreaux consécutifs pour en insérer un entre eux.");
    }
    const auto mid = [](Vec2um p, Vec2um q) {
        return PointMm{(to_millimeters(p.x).value + to_millimeters(q.x).value) / 2.0,
                       (to_millimeters(p.y).value + to_millimeters(q.y).value) / 2.0};
    };
    const auto& lo = satin->rungs[index];
    const auto& hi = satin->rungs[index + 1];
    return addSatinRung(session, objectId, index + 1, mid(lo.a, hi.a), mid(lo.b, hi.b));
}

ServiceResult moveSatinRung(DocumentSession& session, std::uint64_t objectId, std::size_t index,
                            PointMm a, PointMm b) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    if (index >= satin->rungs.size()) {
        return refuse("Barreau inconnu.");
    }
    if (!inRange(a) || !inRange(b)) {
        return refuse("Point hors limites (±1000 mm).");
    }
    if (toUm(a) == toUm(b)) {
        return refuse("Les deux extrémités du barreau sont confondues.");
    }
    const document::SatinRung moved{toUm(a), toUm(b), satin->rungs[index].link_id};
    if (moved == satin->rungs[index]) {
        return refuse("Le barreau est déjà à cette position.");
    }
    session.execute(
        std::make_unique<commands::MoveSatinGuideCommand>(ObjectId{objectId}, index, moved));
    return {true, {}};
}

ServiceResult removeSatinRung(DocumentSession& session, std::uint64_t objectId, std::size_t index) {
    const auto* satin = satinOf(session.project(), objectId);
    if (satin == nullptr) {
        return refuse("Cet objet n'est pas une colonne satin à rails.");
    }
    if (index >= satin->rungs.size()) {
        return refuse("Barreau inconnu.");
    }
    if (satin->rungs.size() <= 2) {
        return refuse("Une colonne satin garde au moins deux barreaux.");
    }
    session.execute(std::make_unique<commands::RemoveSatinGuideCommand>(ObjectId{objectId}, index));
    return {true, {}};
}

SatinDiagnosis diagnoseSatin(const document::Project& project, std::uint64_t objectId) {
    SatinDiagnosis d;
    const auto* satin = satinOf(project, objectId);
    if (satin == nullptr) {
        return d;
    }
    d.isSatin = true;
    d.rungCount = static_cast<int>(satin->rungs.size());
    const auto flatA = geometry::flatten(satin->rail_a, Micrometers{50});
    const auto flatB = geometry::flatten(satin->rail_b, Micrometers{50});
    d.railsCross = geometry::polylines_cross(flatA.points, flatB.points);
    const double maxUm = static_cast<double>(satin->max_width.value);
    for (std::size_t i = 0; i < satin->rungs.size(); ++i) {
        const auto& r = satin->rungs[i];
        const double widthMm = length_um(r.b - r.a) / 1000.0;
        d.widestMm = std::max(d.widestMm, widthMm);
        if (maxUm > 0.0 && length_um(r.b - r.a) > maxUm) {
            d.tooWide.push_back(i);
        }
        if (i + 1 < satin->rungs.size()) {
            const auto& n = satin->rungs[i + 1];
            if (segmentsCross(r.a, r.b, n.a, n.b)) {
                d.crossing.push_back(i);
            }
        }
    }
    return d;
}

std::string satinDiagnosisSummary(const SatinDiagnosis& d) {
    if (!d.isSatin) {
        return "Sélectionnez une colonne satin à rails.";
    }
    if (d.clean()) {
        return "Colonne saine : rails séparés, barreaux sans croisement, largeur dans la limite.";
    }
    std::string out;
    const auto add = [&out](const std::string& part) { out += out.empty() ? part : " ; " + part; };
    if (d.railsCross) {
        add("les deux rails se croisent");
    }
    if (!d.crossing.empty()) {
        add(std::to_string(d.crossing.size()) +
            " paire(s) de barreaux consécutifs se croisent (nœud papillon)");
    }
    if (!d.tooWide.empty()) {
        char buffer[96];
        std::snprintf(buffer, sizeof buffer, "%zu barreau(x) trop large(s) (jusqu'à %.1f mm)",
                      d.tooWide.size(), d.widestMm);
        add(buffer);
    }
    return "À corriger : " + out + ".";
}

std::optional<std::size_t> widestRungGap(const document::Project& project, std::uint64_t objectId) {
    const auto* satin = satinOf(project, objectId);
    if (satin == nullptr || satin->rungs.size() < 2) {
        return std::nullopt;
    }
    std::size_t best = 0;
    double bestGap = -1.0;
    for (std::size_t i = 0; i + 1 < satin->rungs.size(); ++i) {
        const auto& p = satin->rungs[i];
        const auto& q = satin->rungs[i + 1];
        const double gap = length_um(q.a - p.a) + length_um(q.b - p.b);
        if (gap > bestGap) { // strict : le premier écart maximal l'emporte (déterministe)
            bestGap = gap;
            best = i;
        }
    }
    return best;
}

std::optional<std::size_t> redundantRung(const document::Project& project, std::uint64_t objectId) {
    const auto* satin = satinOf(project, objectId);
    if (satin == nullptr || satin->rungs.size() < 3) {
        return std::nullopt;
    }
    std::size_t best = 1;
    double bestSpan = -1.0;
    for (std::size_t i = 1; i + 1 < satin->rungs.size(); ++i) {
        const auto& p = satin->rungs[i - 1];
        const auto& q = satin->rungs[i + 1];
        const double span = length_um(q.a - p.a) + length_um(q.b - p.b);
        if (bestSpan < 0.0 || span < bestSpan) { // plus petit écart de part et d'autre
            bestSpan = span;
            best = i;
        }
    }
    return best;
}

} // namespace openstitch::desktop_v2
