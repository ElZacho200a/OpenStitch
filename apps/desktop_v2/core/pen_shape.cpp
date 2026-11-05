// SPDX-License-Identifier: Apache-2.0
#include "pen_shape.hpp"

#include <algorithm>
#include <cmath>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/curve_fit.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/geometry/reshape.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr double kMaxCoordMm = 1000.0;
constexpr double kMinAreaMm2 = 1.0;
constexpr double kMinGapMm = 0.05; // deux nœuds plus proches sont confondus
constexpr Micrometers kCrossingTolerance{50};

[[nodiscard]] Vec2um toUm(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

[[nodiscard]] bool inRange(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= kMaxCoordMm &&
           std::abs(p.y) <= kMaxCoordMm;
}

PenResult refuse(std::string why) {
    PenResult r;
    r.message = std::move(why);
    return r;
}

} // namespace

// Pose un contour fermé comme nouvel objet (vectoriel + broderie) en UNE commande.
static PenResult commitShape(DocumentSession& session, geometry::Path path,
                             const std::string& name);

namespace {} // namespace

PenResult createPenShape(DocumentSession& session, const std::vector<PenNode>& nodes) {
    if (nodes.size() < 3) {
        return refuse("Une forme demande au moins trois nœuds.");
    }
    if (nodes.size() > 2000) {
        return refuse("Trop de nœuds (2000 au maximum).");
    }
    geometry::Path path;
    path.closed = true;
    bool curved = false;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!inRange(n.pos) || (n.handleOut && !inRange(*n.handleOut))) {
            return refuse("Un nœud est hors de la zone de travail (±1000 mm).");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (std::hypot(nodes[j].pos.x - n.pos.x, nodes[j].pos.y - n.pos.y) < kMinGapMm) {
                return refuse("Deux nœuds sont confondus : écartez-les.");
            }
        }
        geometry::PathNode node;
        node.pos = toUm(n.pos.x, n.pos.y);
        if (n.handleOut &&
            std::hypot(n.handleOut->x - n.pos.x, n.handleOut->y - n.pos.y) > kMinGapMm) {
            const double dx = n.handleOut->x - n.pos.x;
            const double dy = n.handleOut->y - n.pos.y;
            node.type = geometry::NodeType::Smooth;
            node.tan_out = toUm(dx, dy);
            node.tan_in = toUm(-dx, -dy); // tangente symétrique : nœud lisse
            curved = true;
        }
        path.nodes.push_back(node);
    }
    if (std::abs(geometry::signed_area_um2(path)) / 1e6 < kMinAreaMm2) {
        return refuse("Forme trop petite ou dégénérée (aire inférieure à 1 mm²).");
    }
    if (geometry::path_self_intersects(path, kCrossingTolerance)) {
        return refuse("Le contour se croise : déplacez un nœud pour le dégager.");
    }

    PenResult made = commitShape(session, std::move(path), curved ? "Courbe" : "Forme");
    made.curved = curved;
    return made;
}

PenResult createFreehandShape(DocumentSession& session, const std::vector<PointMm>& stroke) {
    // Points de trop près (tremblement de la main) écartés avant l'ajustement.
    std::vector<Vec2um> points;
    for (const auto& p : stroke) {
        if (!inRange(p)) {
            return refuse("Le trait sort de la zone de travail (±1000 mm).");
        }
        const Vec2um u = toUm(p.x, p.y);
        if (points.empty() ||
            std::hypot(static_cast<double>(u.x.value - points.back().x.value),
                       static_cast<double>(u.y.value - points.back().y.value)) >= 150.0) {
            points.push_back(u);
        }
    }
    // Le dernier point revenu sur le premier ne doit pas doubler un sommet.
    if (points.size() > 2 &&
        std::hypot(static_cast<double>(points.front().x.value - points.back().x.value),
                   static_cast<double>(points.front().y.value - points.back().y.value)) < 600.0) {
        points.pop_back();
    }
    if (points.size() < 8) {
        return refuse("Trait trop court : dessinez une forme plus grande.");
    }
    geometry::Path polygon = geometry::polygon_path(points);
    if (std::abs(geometry::signed_area_um2(polygon)) / 1e6 < 4.0) {
        return refuse("Forme trop petite ou plate (aire inférieure à 4 mm²).");
    }
    if (geometry::path_self_intersects(polygon, kCrossingTolerance)) {
        return refuse("Le trait se croise : dessinez un contour sans boucle.");
    }
    // Tolérance proportionnelle à la taille du dessin (le tremblement de la main grandit avec lui)
    // : 2 % de la diagonale, bornée entre 0,3 et 3 mm.
    std::int64_t minX = points.front().x.value;
    std::int64_t maxX = minX;
    std::int64_t minY = points.front().y.value;
    std::int64_t maxY = minY;
    for (const auto& p : points) {
        minX = std::min<std::int64_t>(minX, p.x.value);
        maxX = std::max<std::int64_t>(maxX, p.x.value);
        minY = std::min<std::int64_t>(minY, p.y.value);
        maxY = std::max<std::int64_t>(maxY, p.y.value);
    }
    const double diagonalUm =
        std::hypot(static_cast<double>(maxX - minX), static_cast<double>(maxY - minY));
    // Une courbe fermée à deux nœuds serait valide mais difficile à retoucher : on resserre la
    // tolérance jusqu'à obtenir au moins quatre nœuds (ou le tracé brut en dernier recours).
    geometry::Path fitted = polygon;
    double toleranceUm = std::clamp(diagonalUm * 0.02, 300.0, 3000.0);
    for (int attempt = 0; attempt < 6; ++attempt) {
        geometry::CurveFitOptions options;
        options.tolerance = Micrometers{static_cast<std::int32_t>(toleranceUm)};
        geometry::Path candidate = geometry::fit_curves(polygon, options);
        if (candidate.nodes.size() >= 4 &&
            !geometry::path_self_intersects(candidate, kCrossingTolerance)) {
            fitted = std::move(candidate);
            break;
        }
        toleranceUm /= 2.0;
    }
    bool curved = false;
    for (const auto& n : fitted.nodes) {
        curved = curved || n.tan_in.has_value() || n.tan_out.has_value();
    }
    PenResult made = commitShape(session, std::move(fitted), "Dessin");
    made.curved = curved;
    return made;
}

PenResult commitShape(DocumentSession& session, geometry::Path path, const std::string& name) {
    // Même recette que l'opération `add_shape` : objet vectoriel + objet de broderie (tatami),
    // posés par une seule commande. Les identifiants viennent du compteur du document.
    document::VectorObject vector;
    vector.id = session.allocateObjectId();
    vector.name = name;
    vector.rgb = {80, 120, 200};
    vector.paths.push_back(geometry::PathSet{std::move(path), {}});

    document::EmbroideryObject object;
    object.id = session.allocateObjectId();
    object.name = vector.name;
    object.source_vector = vector.id;
    object.rgb = vector.rgb;
    object.intent = document::EmbroideryIntent::ForcedUserChoice;
    object.params = document::TatamiParams{};

    const auto objectId = object.id.value;
    session.execute(std::make_unique<commands::AddObjectBatchCommand>(
        std::vector<document::VectorObject>{std::move(vector)},
        std::vector<document::EmbroideryObject>{std::move(object)}, "Ajouter : " + name));
    PenResult r;
    r.ok = true;
    r.objectId = objectId;
    return r;
}

} // namespace openstitch::desktop_v2
