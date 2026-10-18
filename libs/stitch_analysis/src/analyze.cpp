// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/analyze.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

#include "openstitch/stitch_analysis/density_map.hpp"

namespace openstitch::stitch_analysis {

namespace {

bool outside(Vec2um p, const stitch::BoundsUm& hoop) {
    return p.x < hoop.min.x || p.x > hoop.max.x || p.y < hoop.min.y || p.y > hoop.max.y;
}

// --- Couches de fil superposées ---------------------------------------------

// Une zone contiguë de la grille où le fil s'épaissit trop.
struct LayerHotspot {
    Vec2um location{};     // centre de la première case (balayage ligne par ligne)
    ObjectId object{};     // objet dont la couture est la plus longue dans cette case
    double thickness{0.0}; // épaisseur maximale dans la zone (couches de remplissage dense)
    std::size_t cells{0};  // nombre de cases de la zone
};

// Épaisseur de fil, en « couches de remplissage dense » : longueur de fil dans
// la case x largeur du fil / surface de la case. Un remplissage dont l'écart
// entre rangées égale la largeur du fil vaut 1 ; une sous-couche espacée de 2 mm
// vaut ~0,2 ; les bords partiels d'un objet voisin valent peu. Contrairement à
// un compte de passes, la mesure ne fait pas de la moindre sous-couche une
// « couche » entière.
std::vector<LayerHotspot> find_layer_hotspots(const stitch::StitchSequence& sequence,
                                              double maxThickness, double threadWidth,
                                              double cell) {
    using Cell = std::pair<long long, long long>; // (y, x) : tri ligne par ligne
    // Pour chaque case : longueur de fil par objet.
    std::map<Cell, std::map<std::uint64_t, double>> grid;
    const double step = cell / 2.0;

    bool hasPrev = false;
    Vec2um prev{};
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::Stitch) {
            hasPrev = false; // saut, coupe, changement de fil : le fil est coupé
            continue;
        }
        const bool counts = cmd.pass == stitch::StitchPass::Underlay ||
                            cmd.pass == stitch::StitchPass::TopStitch ||
                            cmd.pass == stitch::StitchPass::Manual;
        if (hasPrev && counts) {
            const double dx = static_cast<double>(cmd.pos.x.value - prev.x.value);
            const double dy = static_cast<double>(cmd.pos.y.value - prev.y.value);
            const double len = std::sqrt(dx * dx + dy * dy);
            const auto key = static_cast<std::uint64_t>(cmd.source.value);
            const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
            for (int i = 0; i < n; ++i) {
                const double t = (i + 0.5) / n;
                const double x = prev.x.value + dx * t;
                const double y = prev.y.value + dy * t;
                const Cell c{static_cast<long long>(std::floor(y / cell)),
                             static_cast<long long>(std::floor(x / cell))};
                grid[c][key] += len / n;
            }
        }
        prev = cmd.pos;
        hasPrev = true;
    }

    // Cases en excès.
    std::map<Cell, double> over;
    std::map<Cell, ObjectId> mainObject;
    for (const auto& [c, perObject] : grid) {
        double total = 0.0;
        double bestLen = 0.0;
        std::uint64_t bestKey = 0;
        for (const auto& [key, len] : perObject) {
            total += len;
            if (len > bestLen) {
                bestLen = len;
                bestKey = key;
            }
        }
        const double thickness = total * threadWidth / (cell * cell);
        if (thickness > maxThickness) {
            over[c] = thickness;
            mainObject[c] = ObjectId{bestKey};
        }
    }

    // Composantes 4-connexes, parcourues dans l'ordre de la grille (déterministe).
    std::vector<LayerHotspot> out;
    std::map<Cell, bool> seen;
    for (const auto& [start, startCount] : over) {
        if (seen[start]) {
            continue;
        }
        LayerHotspot hot;
        hot.location =
            Vec2um{Micrometers{static_cast<std::int32_t>(std::lround((start.second + 0.5) * cell))},
                   Micrometers{static_cast<std::int32_t>(std::lround((start.first + 0.5) * cell))}};
        hot.object = mainObject[start];
        std::vector<Cell> stack{start};
        seen[start] = true;
        std::vector<Cell> members;
        while (!stack.empty()) {
            const Cell c = stack.back();
            stack.pop_back();
            ++hot.cells;
            members.push_back(c);
            hot.thickness = std::max(hot.thickness, over[c]);
            for (const Cell nb : {Cell{c.first + 1, c.second}, Cell{c.first - 1, c.second},
                                  Cell{c.first, c.second + 1}, Cell{c.first, c.second - 1}}) {
                if (over.count(nb) != 0 && !seen[nb]) {
                    seen[nb] = true;
                    stack.push_back(nb);
                }
            }
        }
        // Un vrai recouvrement contient au moins un bloc de 2 x 2 cases en excès :
        // un anneau d'une case (bord d'un objet, connexions de rangées) n'en est pas un.
        const bool solid = std::any_of(members.begin(), members.end(), [&](const Cell& c) {
            return over.count({c.first + 1, c.second}) != 0 &&
                   over.count({c.first, c.second + 1}) != 0 &&
                   over.count({c.first + 1, c.second + 1}) != 0;
        });
        if (solid) {
            out.push_back(hot);
        }
    }
    return out;
}

} // namespace

std::string format_mm_fr(double micrometers) {
    // Dixièmes de millimètre entiers (troncature), puis assemblage manuel :
    // ni std::to_string ni locale (le séparateur décimal est la virgule).
    const auto tenths = static_cast<long long>(micrometers / 100.0);
    const long long abs_t = tenths < 0 ? -tenths : tenths;
    std::string out = tenths < 0 ? "-" : "";
    out += std::to_string(abs_t / 10);
    out += ',';
    out += std::to_string(abs_t % 10);
    return out;
}

std::vector<Finding> analyze(const stitch::StitchSequence& sequence,
                             const AnalysisOptions& options) {
    return analyze_detailed(sequence, options).findings;
}

AnalysisReport analyze_detailed(const stitch::StitchSequence& sequence,
                                const AnalysisOptions& options) {
    std::vector<Finding> findings;
    std::map<std::string, std::size_t> perCategory;

    // Ajoute un problème en respectant le plafond par catégorie.
    const auto add = [&](Severity sev, const std::string& cat, std::string msg, Vec2um loc,
                         ObjectId obj, std::string hint = {}) {
        std::size_t& count = perCategory[cat];
        if (count < options.max_findings_per_category) {
            findings.push_back({sev, cat, std::move(msg), loc, obj, std::move(hint)});
        }
        ++count;
    };

    const auto stats = stitch::compute_stats(sequence);
    if (stats.stitches == 0) {
        findings.push_back({Severity::Error,
                            "vide",
                            "Le motif ne contient aucun point.",
                            {},
                            {},
                            "Créez un objet puis générez les points."});
        return {std::move(findings), {}};
    }

    bool hasPrevStitch = false;
    Vec2um prevStitch{};
    Vec2um prevPos{};
    bool hasPrevPos = false;
    // Déplacement en cours depuis la dernière piqûre (Lot G : déplacement
    // long sans coupe).
    bool hasLastStitch = false;
    Vec2um lastStitch{};
    bool inMove = false;
    bool trimmed = false;

    // Points courts entre `near_zero_stitch` et `min_stitch` : un constat d'information par
    // objet (clé = id d'objet, ordre croissant donc déterministe) au lieu d'un par point.
    struct ShortGroup {
        std::size_t count{0};
        double shortest{0.0};
        Vec2um first{};
    };
    std::map<std::uint64_t, ShortGroup> shortGroups;
    const double nearZero =
        static_cast<double>(std::min(options.near_zero_stitch.value, options.min_stitch.value));

    // Sauts longs du déplacement en cours : signalés seulement si le déplacement s'achève sans
    // coupe (une coupe avant OU après le saut rend le message « envisagez une coupe » faux).
    struct PendingJump {
        double len;
        Vec2um pos;
        ObjectId object;
    };
    std::vector<PendingJump> pendingJumps;
    const auto flushJumps = [&](bool hadTrim) {
        if (!hadTrim) {
            for (const PendingJump& j : pendingJumps) {
                add(Severity::Warning, "saut-long",
                    "Saut long (" + format_mm_fr(j.len) + " mm) : envisagez une coupe.", j.pos,
                    j.object,
                    "Réordonnez les objets pour réduire les sauts, ou ajoutez une coupe.");
            }
        }
        pendingJumps.clear();
    };

    for (const auto& cmd : sequence.commands) {
        switch (cmd.type) {
        case stitch::CommandType::Stitch: {
            flushJumps(trimmed); // le déplacement s'achève ici
            if (inMove && hasLastStitch && !trimmed) {
                const double moved = length_um(cmd.pos - lastStitch);
                if (moved > static_cast<double>(options.trim_threshold.value)) {
                    add(Severity::Warning, "saut-sans-coupe",
                        "Déplacement de " + format_mm_fr(moved) +
                            " mm sans coupe : le fil traîne sur le tissu.",
                        cmd.pos, cmd.source,
                        "Activez les coupes automatiques ou rapprochez les objets.");
                }
            }
            inMove = false;
            trimmed = false;
            hasLastStitch = true;
            lastStitch = cmd.pos;
            // Les points d'arrêt sont courts par construction : jamais signalés.
            if (hasPrevStitch && cmd.pass != stitch::StitchPass::Lock) {
                const double len = length_um(cmd.pos - prevStitch);
                if (len < nearZero) {
                    add(Severity::Warning, "point-court",
                        "Point quasi nul (moins de " + format_mm_fr(nearZero) +
                            " mm) : probable doublon, risque de casse du fil.",
                        cmd.pos, cmd.source,
                        "Augmentez l'espacement ou simplifiez le contour de l'objet.");
                } else if (len < static_cast<double>(options.min_stitch.value)) {
                    ShortGroup& g = shortGroups[cmd.source.value];
                    if (g.count == 0) {
                        g.first = cmd.pos;
                        g.shortest = len;
                    }
                    g.shortest = std::min(g.shortest, len);
                    ++g.count;
                } else if (len > static_cast<double>(options.max_stitch.value)) {
                    add(Severity::Warning, "point-long",
                        "Point long (" + format_mm_fr(len) + " mm) : risque d'accrochage.", cmd.pos,
                        cmd.source, "Réduisez la longueur de point ou découpez l'objet.");
                }
            }
            if (options.hoop && outside(cmd.pos, *options.hoop)) {
                add(Severity::Error, "hors-cadre", "Point hors du cadre de broderie.", cmd.pos,
                    cmd.source, "Déplacez l'objet dans le cadre ou agrandissez le canevas.");
            }
            prevStitch = cmd.pos;
            hasPrevStitch = true;
            break;
        }
        case stitch::CommandType::Jump: {
            inMove = true;
            if (hasPrevPos) {
                const double len = length_um(cmd.pos - prevPos);
                if (len > static_cast<double>(options.max_jump.value)) {
                    pendingJumps.push_back({len, cmd.pos, cmd.source});
                }
            }
            // Un saut lève l'aiguille : le fil n'est plus continu. Sans ce
            // reset, la prochaine couture (même à la position du saut, donc
            // distance réelle nulle) se comparait à `prevStitch` D'AVANT le
            // saut -- un « point-court »/« point-long » fantôme signalant la
            // distance du SAUT lui-même comme si c'était un unique point cousu
            // continu (défaut trouvé en usage réel : un saut long légitime
            // produisait systématiquement AUSSI un « point-long » redondant et
            // trompeur à la même distance, dès que la couture reprenait juste
            // après, ce qui est le cas normal — cf. `emit_polyline`).
            hasPrevStitch = false;
            break;
        }
        case stitch::CommandType::Trim:
            trimmed = true;
            break;
        default:
            break;
        }
        prevPos = cmd.pos;
        hasPrevPos = true;
    }
    flushJumps(trimmed); // déplacement non terminé en fin de séquence

    for (const auto& [object, g] : shortGroups) {
        add(Severity::Info, "points-courts",
            std::to_string(g.count) + " point(s) de " + format_mm_fr(nearZero) + " à " +
                format_mm_fr(static_cast<double>(options.min_stitch.value)) +
                " mm (le plus court : " + format_mm_fr(g.shortest) + " mm).",
            g.first, ObjectId{object},
            "Les finitions fusionnent les points courts quand elles le peuvent ; augmentez "
            "l'espacement de l'objet si la densité est excessive.");
    }

    if (stats.stitches > options.max_stitches) {
        add(Severity::Warning, "trop-de-points",
            "Le motif compte " + std::to_string(stats.stitches) +
                " points : temps de broderie très long.",
            {}, {}, "Augmentez l'espacement ou réduisez la taille du motif.");
    }

    if (options.max_layer_thickness > 0.0) {
        const double cell = static_cast<double>(std::max(500, options.layer_cell.value));
        const double width = static_cast<double>(std::max(50, options.thread_width.value));
        for (const auto& hot :
             find_layer_hotspots(sequence, options.max_layer_thickness, width, cell)) {
            const double areaMm2 = static_cast<double>(hot.cells) * cell * cell / 1e6;
            add(Severity::Warning, "couches-superposees",
                "Le fil s'épaissit jusqu'à " + format_mm_fr(hot.thickness * 1000.0) +
                    " couches de remplissage dense sur environ " +
                    std::to_string(std::lround(areaMm2)) + " mm² : le tissu se déforme.",
                hot.location, hot.object,
                "Réduisez le chevauchement des objets, retirez une sous-couche ou supprimez "
                "le remplissage caché sous un autre objet.");
        }
    }

    // --- HP-PROD-007 : règles par objet ---------------------------------------
    {
        struct ObjectInfo {
            bool hasBox{false};
            Vec2um min{};
            Vec2um max{};
            Vec2um first{};
            std::size_t stitches{0};
            std::size_t locks{0};
        };
        std::map<std::uint64_t, ObjectInfo> objects; // ordre d'identifiant : déterministe
        for (const auto& cmd : sequence.commands) {
            if (cmd.type != stitch::CommandType::Stitch || cmd.source.value == 0) {
                continue;
            }
            ObjectInfo& o = objects[cmd.source.value];
            if (o.stitches == 0) {
                o.first = cmd.pos;
            }
            ++o.stitches;
            if (cmd.pass == stitch::StitchPass::Lock) {
                ++o.locks;
                continue;
            }
            if (!o.hasBox) {
                o.min = o.max = cmd.pos;
                o.hasBox = true;
            } else {
                o.min.x = std::min(o.min.x, cmd.pos.x);
                o.min.y = std::min(o.min.y, cmd.pos.y);
                o.max.x = std::max(o.max.x, cmd.pos.x);
                o.max.y = std::max(o.max.y, cmd.pos.y);
            }
        }
        for (const auto& [id, o] : objects) {
            if (options.min_object_size.value > 0 && o.hasBox &&
                (o.max.x - o.min.x).value < options.min_object_size.value &&
                (o.max.y - o.min.y).value < options.min_object_size.value) {
                add(Severity::Info, "detail-trop-petit",
                    "Objet de " + format_mm_fr(static_cast<double>((o.max.x - o.min.x).value)) +
                        " x " + format_mm_fr(static_cast<double>((o.max.y - o.min.y).value)) +
                        " mm : moins de " +
                        format_mm_fr(static_cast<double>(options.min_object_size.value)) +
                        " mm, le détail risque de se perdre dans l'épaisseur du fil.",
                    o.first, ObjectId{id},
                    "Agrandissez l'objet ou supprimez-le s'il n'apporte rien au dessin.");
            }
            if (options.check_lock_stitches && o.locks == 0) {
                add(Severity::Info, "objet-sans-arret",
                    "Objet sans point d'arrêt : les fils du début et de la fin peuvent se défaire.",
                    o.first, ObjectId{id}, "Activez les points d'arrêt dans les finitions.");
            }
        }
    }

    if (options.max_trims > 0 && stats.trims > options.max_trims) {
        add(Severity::Warning, "trop-de-coupes",
            "Le motif compte " + std::to_string(stats.trims) + " coupes (plus de " +
                std::to_string(options.max_trims) + ") : temps de broderie et fils à rentrer.",
            {}, {},
            "Regroupez les objets de même couleur ou rapprochez-les pour limiter les coupes.");
    }

    if (options.max_penetrations_per_mm2 > 0.0) {
        DensityOptions density;
        density.max_penetrations_per_mm2 = options.max_penetrations_per_mm2;
        for (const auto& hot : find_density_hotspots(sequence, density)) {
            add(Severity::Warning, "densite-excessive",
                "Zone trop dense : jusqu'à " + format_mm_fr(hot.max_penetrations_per_mm2 * 1000.0) +
                    " pénétrations/mm² sur environ " + std::to_string(std::lround(hot.area_mm2)) +
                    " mm² : risque de casse du fil.",
                hot.center, hot.object,
                "Augmentez l'espacement des rangées, retirez une sous-couche ou un recouvrement.");
        }
    }

    // Tri par gravité décroissante (stable pour rester déterministe).
    std::stable_sort(findings.begin(), findings.end(),
                     [](const Finding& a, const Finding& b) { return a.severity > b.severity; });
    AnalysisReport report;
    report.findings = std::move(findings);
    for (const auto& [cat, count] : perCategory) {
        if (count > options.max_findings_per_category) {
            report.suppressed[cat] = count - options.max_findings_per_category;
        }
    }
    return report;
}

} // namespace openstitch::stitch_analysis
