// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/debug_export.hpp"

#include "openstitch/geometry/polyline.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace openstitch::auto_satin {

namespace {

double mmx(Vec2um p) { return static_cast<double>(p.x.value) / 1000.0; }
double mmy(Vec2um p) { return -static_cast<double>(p.y.value) / 1000.0; }  // Y vers le bas en SVG

void poly(std::ostringstream& o, const std::vector<Vec2um>& pts, const char* stroke, double w,
          bool closed, const char* dash = "") {
    if (pts.size() < 2) {
        return;
    }
    o << "<path d=\"M";
    for (std::size_t i = 0; i < pts.size(); ++i) {
        o << (i ? "L" : "") << mmx(pts[i]) << " " << mmy(pts[i]) << " ";
    }
    if (closed) {
        o << "Z";
    }
    o << "\" fill=\"none\" stroke=\"" << stroke << "\" stroke-width=\"" << w << "\"";
    if (dash[0]) {
        o << " stroke-dasharray=\"" << dash << "\"";
    }
    o << "/>\n";
}

std::vector<Vec2um> nodes_of(const geometry::Path& p) {
    std::vector<Vec2um> v;
    for (const auto& n : p.nodes) {
        v.push_back(n.pos);
    }
    return v;
}

}  // namespace

std::string to_debug_svg(
    const geometry::PathSet& region, const AutoSatinAnalysis& a,
    const std::optional<std::pair<geometry::Path, geometry::Path>>& current_rails) {
    // Bornes.
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto& n : region.outer.nodes) {
        minx = std::min(minx, mmx(n.pos));
        maxx = std::max(maxx, mmx(n.pos));
        miny = std::min(miny, mmy(n.pos));
        maxy = std::max(maxy, mmy(n.pos));
    }
    const double m = 3.0;
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << (minx - m) << " " << (miny - m)
      << " " << (maxx - minx + 2 * m) << " " << (maxy - miny + 2 * m) << "\">\n";
    o << "<!-- satinabilite: " << to_string(a.report.status)
      << " branches=" << a.report.branch_count << " jonctions=" << a.report.junction_count
      << " extremites=" << a.report.endpoint_count << " -->\n";

    // Contour + trous.
    poly(o, nodes_of(region.outer), "#111", 0.15, true);
    for (const auto& h : region.holes) {
        poly(o, nodes_of(h), "#c22", 0.15, true);
    }
    // Rails actuels (comparaison).
    if (current_rails) {
        poly(o, nodes_of(current_rails->first), "#999", 0.12, false, "0.6 0.4");
        poly(o, nodes_of(current_rails->second), "#999", 0.12, false, "0.6 0.4");
    }
    // Arêtes du squelette.
    for (const auto& e : a.debug.graph.edges) {
        poly(o, e.centerline, "#1a6", 0.18, false);
    }
    // Nœuds.
    for (const auto& n : a.debug.graph.nodes) {
        const char* col = n.type == SkeletonNodeType::Junction ? "#e80"
                          : n.type == SkeletonNodeType::Endpoint ? "#0a0"
                                                                 : "#66c";
        o << "<circle cx=\"" << mmx(n.position) << "\" cy=\"" << mmy(n.position)
          << "\" r=\"0.5\" fill=\"" << col << "\"/>\n";
    }
    o << "</svg>\n";
    return o.str();
}

std::string columns_to_svg(const geometry::PathSet& region, const SatinColumnsResult& result) {
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto& n : region.outer.nodes) {
        minx = std::min(minx, mmx(n.pos));
        maxx = std::max(maxx, mmx(n.pos));
        miny = std::min(miny, mmy(n.pos));
        maxy = std::max(maxy, mmy(n.pos));
    }
    const double m = 3.0;
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << (minx - m) << " " << (miny - m)
      << " " << (maxx - minx + 2 * m) << " " << (maxy - miny + 2 * m) << "\">\n";
    o << "<!-- statut: " << to_string(result.status) << " colonnes=" << result.columns.size();
    if (!result.refusal.empty()) {
        o << " REFUS: " << result.refusal;
    }
    o << " -->\n";
    // Une ligne de diagnostic par colonne (§11) : longueur, largeur min/max,
    // nombre de guides (barreaux). Lisible en ouvrant le SVG en texte, sans
    // dépendre d'un rendu graphique.
    for (std::size_t i = 0; i < result.columns.size(); ++i) {
        const auto& c = result.columns[i];
        o << "<!-- colonne " << i << " : longueur=" << (c.length_um / 1000.0)
          << "mm largeur_min=" << (c.min_width_um / 1000.0)
          << "mm largeur_max=" << (c.max_width_um / 1000.0) << "mm guides=" << c.rungs.size()
          << " -->\n";
    }
    for (const auto& core : result.junction_cores) {
        o << "<!-- jonction " << core.junction_id << " : noyau"
          << (core.requires_fill ? " (remplissage separe requis)" : "") << ", aire="
          << (core.area_um2 / 1'000'000.0) << "mm2 -- configured_radius="
          << (core.configured_radius_um / 1000.0)
          << "mm (plafond de securite, PAS le rayon reel) local_radius="
          << (core.local_radius_um / 1000.0) << "mm actual_core_max_radius="
          << (core.actual_max_radius_um / 1000.0) << "mm -->\n";
    }

    poly(o, nodes_of(region.outer), "#111", 0.15, true);
    for (const auto& h : region.holes) {
        poly(o, nodes_of(h), "#c22", 0.15, true);
    }
    for (const auto& e : result.debug.graph.edges) {
        poly(o, e.centerline, "#8c8", 0.12, false, "0.5 0.4");
    }
    // Région locale de jonction (§2) : disque en pointillé gris, rayon
    // `local_radius_um` — la valeur RÉELLEMENT utilisée (données réelles),
    // pas `configured_radius_um` (simple plafond de sécurité, presque
    // jamais atteint). Tracée sous les secteurs/noyau pour rester lisible.
    for (const auto& core : result.junction_cores) {
        for (const auto& n : result.debug.graph.nodes) {
            if (n.id != core.junction_id) {
                continue;
            }
            o << "<circle cx=\"" << mmx(n.position) << "\" cy=\"" << mmy(n.position) << "\" r=\""
              << (core.local_radius_um / 1000.0)
              << "\" fill=\"none\" stroke=\"#999\" stroke-width=\"0.06\" "
                 "stroke-dasharray=\"0.2 0.2\"/>\n";
            break;
        }
    }
    // Secteurs de branche (§4) : un remplissage translucide par branche,
    // couleur qui tourne dans une petite palette (indexée sur l'ordre
    // d'apparition dans `junction_sectors`, stable/déterministe).
    {
        static constexpr const char* kSectorPalette[] = {"#f90", "#09f", "#9c0", "#c6f",
                                                          "#0cc", "#f5a"};
        constexpr std::size_t kPaletteSize = sizeof(kSectorPalette) / sizeof(kSectorPalette[0]);
        std::size_t sectorOrdinal = 0;
        for (const auto& sector : result.junction_sectors) {
            if (sector.boundary.size() < 3) {
                ++sectorOrdinal;
                continue;
            }
            const char* fill = kSectorPalette[sectorOrdinal % kPaletteSize];
            o << "<path d=\"M";
            for (std::size_t i = 0; i < sector.boundary.size(); ++i) {
                o << (i ? "L" : "") << mmx(sector.boundary[i]) << " " << mmy(sector.boundary[i])
                  << " ";
            }
            o << "Z\" fill=\"" << fill << "\" fill-opacity=\"0.2\" stroke=\"" << fill
              << "\" stroke-width=\"0.06\"/>\n";
            ++sectorOrdinal;
        }
    }
    // JunctionCore (§6) : remplissage translucide + contour pointillé
    // magenta, résidu APRÈS partition en secteurs (pas « région entière
    // moins colonnes ») — au-dessus des secteurs, sous les rails/barreaux.
    for (const auto& core : result.junction_cores) {
        if (core.boundary.size() < 3) {
            continue;
        }
        o << "<path d=\"M";
        for (std::size_t i = 0; i < core.boundary.size(); ++i) {
            o << (i ? "L" : "") << mmx(core.boundary[i]) << " " << mmy(core.boundary[i]) << " ";
        }
        o << "Z\" fill=\"#e0f\" fill-opacity=\"0.45\" stroke=\"#e0f\" stroke-width=\"0.1\" "
             "stroke-dasharray=\"0.3 0.3\"/>\n";
    }
    // JunctionSeparator (§3) : point vert à la frontière locale partagée
    // entre deux branches angulairement adjacentes.
    for (const auto& sep : result.junction_separators) {
        o << "<circle cx=\"" << mmx(sep.point) << "\" cy=\"" << mmy(sep.point)
          << "\" r=\"0.3\" fill=\"#0a0\"/>\n";
    }
    // StableBranchEnd (§1) : la dernière section transversale RÉELLEMENT
    // stable d'une branche, en rouge — déjà visible comme barreau terminal
    // (ci-dessous) mais soulignée ici explicitement par ses deux extrémités.
    for (const auto& end : result.stable_branch_ends) {
        o << "<line x1=\"" << mmx(end.rail_a_point) << "\" y1=\"" << mmy(end.rail_a_point)
          << "\" x2=\"" << mmx(end.rail_b_point) << "\" y2=\"" << mmy(end.rail_b_point)
          << "\" stroke=\"#c00\" stroke-width=\"0.2\"/>\n";
    }
    for (const auto& col : result.columns) {
        // Barreaux : les deux terminaux de jonction (bridges verrouillés,
        // §5) ressortent en rouge/épais ; les guides intermédiaires restent
        // gris fin.
        for (std::size_t ri = 0; ri < col.rungs.size(); ++ri) {
            const auto& rung = col.rungs[ri];
            const bool isStartBridge = ri == 0 && col.start_junction.has_value();
            const bool isEndBridge = ri + 1 == col.rungs.size() && col.end_junction.has_value();
            const char* stroke = (isStartBridge || isEndBridge) ? "#c00" : "#999";
            const double w = (isStartBridge || isEndBridge) ? 0.22 : 0.12;
            o << "<line x1=\"" << mmx(rung.a) << "\" y1=\"" << mmy(rung.a) << "\" x2=\""
              << mmx(rung.b) << "\" y2=\"" << mmy(rung.b) << "\" stroke=\"" << stroke
              << "\" stroke-width=\"" << w << "\"/>\n";
        }
        poly(o, nodes_of(col.rail_a), "#1560c8", 0.2, false);
        poly(o, nodes_of(col.rail_b), "#e07000", 0.2, false);
    }
    // Étiquettes : identifiant de colonne (au milieu du premier barreau) et
    // identifiant de jonction (à la position du nœud du squelette).
    for (std::size_t i = 0; i < result.columns.size(); ++i) {
        const auto& c = result.columns[i];
        if (c.rungs.empty()) {
            continue;
        }
        const auto& mid = c.rungs[c.rungs.size() / 2];
        const double x = (mmx(mid.a) + mmx(mid.b)) * 0.5;
        const double y = (mmy(mid.a) + mmy(mid.b)) * 0.5;
        o << "<text x=\"" << x << "\" y=\"" << y
          << "\" font-size=\"1.2\" fill=\"#000\">c" << i << "</text>\n";
    }
    for (const auto& n : result.debug.graph.nodes) {
        if (n.type != SkeletonNodeType::Junction) {
            continue;
        }
        o << "<circle cx=\"" << mmx(n.position) << "\" cy=\"" << mmy(n.position)
          << "\" r=\"0.4\" fill=\"#e80\"/>\n";
        o << "<text x=\"" << (mmx(n.position) + 0.5) << "\" y=\"" << (mmy(n.position) - 0.5)
          << "\" font-size=\"1.2\" fill=\"#e80\">J" << n.id << "</text>\n";
    }
    o << "</svg>\n";
    return o.str();
}

std::string parametric_to_svg(const geometry::PathSet& region, const SatinColumnsResult& result) {
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto& n : region.outer.nodes) {
        minx = std::min(minx, mmx(n.pos));
        maxx = std::max(maxx, mmx(n.pos));
        miny = std::min(miny, mmy(n.pos));
        maxy = std::max(maxy, mmy(n.pos));
    }
    const double m = 3.0;
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << (minx - m) << " " << (miny - m)
      << " " << (maxx - minx + 2 * m) << " " << (maxy - miny + 2 * m) << "\">\n";
    o << "<!-- mode: parametric statut=" << to_string(result.status)
      << " objets=" << result.parametric_columns.size();
    if (!result.refusal.empty()) {
        o << " REFUS: " << result.refusal;
    }
    o << " -->\n";

    // Statistiques par objet (§ étape 15), une ligne, lisible en texte.
    const Micrometers flattenTol{30};
    for (std::size_t i = 0; i < result.parametric_columns.size(); ++i) {
        const auto& obj = result.parametric_columns[i];
        const auto flatA = geometry::flatten(obj.rail_a, flattenTol);
        const auto flatB = geometry::flatten(obj.rail_b, flattenTol);
        o << "<!-- objet " << i << " : stations_brutes=" << obj.raw_station_count
          << " paires=" << obj.control_pairs.size()
          << " segments_bezier=" << (obj.rail_a.nodes.empty() ? 0 : obj.rail_a.nodes.size() - 1)
          << " erreur_max=" << (obj.max_fit_error_um / 1000.0)
          << "mm longueur_rail_a=" << (geometry::polyline_length(flatA.points) / 1000.0)
          << "mm longueur_rail_b=" << (geometry::polyline_length(flatB.points) / 1000.0)
          << "mm largeur_min=" << (obj.min_width_um / 1000.0)
          << "mm largeur_max=" << (obj.max_width_um / 1000.0)
          << "mm recouvrement_debut=" << (obj.start_overlap_um / 1000.0)
          << "mm recouvrement_fin=" << (obj.end_overlap_um / 1000.0) << "mm -->\n";
    }
    for (const auto& plan : result.junction_plans) {
        o << "<!-- jonction " << plan.junction_id << " : ordre de couture (dessous->dessus) =";
        for (auto idx : plan.stitch_order) {
            o << " " << idx;
        }
        o << " -->\n";
    }

    // Contour + trous + squelette (mêmes conventions que `columns_to_svg`).
    poly(o, nodes_of(region.outer), "#111", 0.15, true);
    for (const auto& h : region.holes) {
        poly(o, nodes_of(h), "#c22", 0.15, true);
    }
    for (const auto& e : result.debug.graph.edges) {
        poly(o, e.centerline, "#8c8", 0.1, false, "0.4 0.35");
    }

    // Recouvrement de jonction : segment d'axe supplémentaire (au-delà de la
    // dernière paire structurante non-jonction), en jaune translucide épais —
    // rend visible la zone où deux objets voisins se chevauchent VOLONTAIREMENT.
    for (const auto& obj : result.parametric_columns) {
        if (obj.control_pairs.size() < 2) {
            continue;
        }
        if (obj.start_overlap_um > 0.0) {
            const auto& p0 = obj.control_pairs.front();
            const auto& p1 = obj.control_pairs[1];
            o << "<line x1=\"" << mmx(p0.rail_a_point) << "\" y1=\"" << mmy(p0.rail_a_point)
              << "\" x2=\"" << mmx(p1.rail_a_point) << "\" y2=\"" << mmy(p1.rail_a_point)
              << "\" stroke=\"#fc0\" stroke-width=\"1.0\" stroke-opacity=\"0.35\"/>\n";
        }
        if (obj.end_overlap_um > 0.0) {
            const auto& p0 = obj.control_pairs[obj.control_pairs.size() - 2];
            const auto& p1 = obj.control_pairs.back();
            o << "<line x1=\"" << mmx(p0.rail_a_point) << "\" y1=\"" << mmy(p0.rail_a_point)
              << "\" x2=\"" << mmx(p1.rail_a_point) << "\" y2=\"" << mmy(p1.rail_a_point)
              << "\" stroke=\"#fc0\" stroke-width=\"1.0\" stroke-opacity=\"0.35\"/>\n";
        }
    }

    for (std::size_t oi = 0; oi < result.parametric_columns.size(); ++oi) {
        const auto& obj = result.parametric_columns[oi];
        // Rails Bézier APLATIS (lisses) — jamais les nœuds de contrôle bruts.
        const auto flatA = geometry::flatten(obj.rail_a, flattenTol);
        const auto flatB = geometry::flatten(obj.rail_b, flattenTol);
        poly(o, flatA.points, "#1560c8", 0.25, false);
        poly(o, flatB.points, "#e07000", 0.25, false);
        // Poignées Bézier : nœud (petit cercle noir) + segment vers chaque
        // poignée (gris fin), pour distinguer la géométrie de contrôle des
        // rails aplatis eux-mêmes.
        for (const geometry::Path* rail : {&obj.rail_a, &obj.rail_b}) {
            for (const auto& node : rail->nodes) {
                o << "<circle cx=\"" << mmx(node.pos) << "\" cy=\"" << mmy(node.pos)
                  << "\" r=\"0.12\" fill=\"#333\"/>\n";
                if (node.tan_out) {
                    const Vec2um h = node.pos + *node.tan_out;
                    o << "<line x1=\"" << mmx(node.pos) << "\" y1=\"" << mmy(node.pos) << "\" x2=\""
                      << mmx(h) << "\" y2=\"" << mmy(h)
                      << "\" stroke=\"#aaa\" stroke-width=\"0.06\"/>\n";
                }
                if (node.tan_in) {
                    const Vec2um h = node.pos + *node.tan_in;
                    o << "<line x1=\"" << mmx(node.pos) << "\" y1=\"" << mmy(node.pos) << "\" x2=\""
                      << mmx(h) << "\" y2=\"" << mmy(h)
                      << "\" stroke=\"#aaa\" stroke-width=\"0.06\"/>\n";
                }
            }
        }
        // Lignes d'angle (barreaux structurants) : jonction en rouge épais,
        // bout ouvert en vert épais, intermédiaire en gris.
        for (std::size_t pi = 0; pi < obj.control_pairs.size(); ++pi) {
            const auto& pair = obj.control_pairs[pi];
            const char* stroke = pair.junction_pair ? "#c00" : pair.open_tip_pair ? "#0a0" : "#999";
            const double w = (pair.junction_pair || pair.open_tip_pair) ? 0.22 : 0.12;
            o << "<line x1=\"" << mmx(pair.rail_a_point) << "\" y1=\"" << mmy(pair.rail_a_point)
              << "\" x2=\"" << mmx(pair.rail_b_point) << "\" y2=\"" << mmy(pair.rail_b_point)
              << "\" stroke=\"" << stroke << "\" stroke-width=\"" << w << "\"/>\n";
        }
        if (!obj.control_pairs.empty()) {
            const auto& mid = obj.control_pairs[obj.control_pairs.size() / 2];
            const double x = (mmx(mid.rail_a_point) + mmx(mid.rail_b_point)) * 0.5;
            const double y = (mmy(mid.rail_a_point) + mmy(mid.rail_b_point)) * 0.5;
            o << "<text x=\"" << x << "\" y=\"" << y << "\" font-size=\"1.2\" fill=\"#000\">p" << oi
              << "</text>\n";
        }
    }
    for (const auto& n : result.debug.graph.nodes) {
        if (n.type != SkeletonNodeType::Junction) {
            continue;
        }
        o << "<circle cx=\"" << mmx(n.position) << "\" cy=\"" << mmy(n.position)
          << "\" r=\"0.4\" fill=\"#e80\"/>\n";
        o << "<text x=\"" << (mmx(n.position) + 0.5) << "\" y=\"" << (mmy(n.position) - 0.5)
          << "\" font-size=\"1.2\" fill=\"#e80\">J" << n.id << "</text>\n";
    }
    o << "</svg>\n";
    return o.str();
}

}  // namespace openstitch::auto_satin
