// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/boundary_blend_proposals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <utility>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/boundary_blend.hpp"
#include "thread_model.hpp"

namespace openstitch::project_ops {

namespace {

using Rgb = std::array<double, 3>;

Error coded(ErrorCategory category, std::string code, std::string message, std::string hint) {
    return Error{category, std::move(message), {}, std::move(code), std::move(hint)};
}

// Couleur de l'image au point modèle (µm, Y vers le haut), bilinéaire ; nullopt hors image.
// Même repère que `autodigitize::sample_region_colors` : image centrée sur l'origine.
std::optional<Rgb> color_at(const image::Image& img, double pxUm, double xUm, double yUm) {
    const double fx = xUm / pxUm + img.width / 2.0 - 0.5;
    const double fy = img.height / 2.0 - yUm / pxUm - 0.5;
    if (fx < 0.0 || fy < 0.0 || fx > img.width - 1.0 || fy > img.height - 1.0) {
        return std::nullopt;
    }
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(img.width - 1, x0 + 1);
    const int y1 = std::min(img.height - 1, y0 + 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    Rgb out{};
    for (int c = 0; c < 3; ++c) {
        const auto px = [&](int x, int y) {
            return static_cast<double>(
                img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                          static_cast<std::size_t>(x)) *
                             4 +
                         static_cast<std::size_t>(c)]);
        };
        const double top = px(x0, y0) * (1.0 - tx) + px(x1, y0) * tx;
        const double bottom = px(x0, y1) * (1.0 - tx) + px(x1, y1) * tx;
        out[static_cast<std::size_t>(c)] = top * (1.0 - ty) + bottom * ty;
    }
    return out;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end()); // total-order-ok: doubles finis, tri complet
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Transition {
    double lo{0.0}; // abscisse (µm, le long de la normale A -> B) où 10 % du passage est fait
    double hi{0.0}; // abscisse où 90 % du passage est fait
};

// Profil de couleur en travers de la frontière en `p`, normale `n` (unitaire, de A vers B).
// nullopt si le profil sort de l'image, si les deux côtés ont presque la même couleur, ou si la
// transition n'est pas monotone à 10 %-90 % près (bruit, troisième zone).
std::optional<Transition> transition_at(const image::Image& img, double pxUm, Vec2um p, double nx,
                                        double ny, const BoundaryBlendProposalOptions& o) {
    const double reach = static_cast<double>(o.reach.value);
    const double step = std::max(pxUm * 0.5, 100.0);
    std::vector<double> ts;
    std::vector<Rgb> colors;
    for (double t = -reach; t <= reach + 1e-9; t += step) {
        const auto c = color_at(img, pxUm, p.x.value + nx * t, p.y.value + ny * t);
        if (!c) {
            return std::nullopt;
        }
        ts.push_back(t);
        colors.push_back(*c);
    }
    // Couleurs de référence : moyennes du dernier tiers de chaque côté.
    const auto mean = [&](double from, double to) {
        Rgb m{};
        int n = 0;
        for (std::size_t i = 0; i < ts.size(); ++i) {
            if (ts[i] >= from && ts[i] <= to) {
                for (int c = 0; c < 3; ++c) {
                    m[static_cast<std::size_t>(c)] += colors[i][static_cast<std::size_t>(c)];
                }
                ++n;
            }
        }
        for (auto& v : m) {
            v /= std::max(1, n);
        }
        return m;
    };
    const Rgb ca = mean(-reach, -reach * 2.0 / 3.0);
    const Rgb cb = mean(reach * 2.0 / 3.0, reach);
    const Rgb axis{cb[0] - ca[0], cb[1] - ca[1], cb[2] - ca[2]};
    const double len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    if (std::sqrt(len2) < o.min_contrast) {
        return std::nullopt;
    }
    // Avancement u(t) ∈ [0 ; 1] du passage de A vers B (projection sur l'axe des couleurs).
    std::optional<double> lo;
    std::optional<double> hi;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        const double u = ((colors[i][0] - ca[0]) * axis[0] + (colors[i][1] - ca[1]) * axis[1] +
                          (colors[i][2] - ca[2]) * axis[2]) /
                         len2;
        if (!lo && u >= 0.1) {
            lo = ts[i];
        }
        if (!hi && u >= 0.9) {
            hi = ts[i];
        }
    }
    if (!lo || !hi || *hi < *lo) {
        return std::nullopt;
    }
    return Transition{*lo, *hi};
}

} // namespace

Result<std::vector<BoundaryBlendProposal>>
propose_boundary_blends(const document::Project& project,
                        const BoundaryBlendProposalOptions& options) {
    const image::Image img = detail::working_image(project);
    const double pxUm = project.imageSource().mm_per_px.value * 1000.0;
    if (img.empty() || !(pxUm > 0.0)) {
        return std::unexpected(
            coded(ErrorCategory::UserInput, "E_BLEND_NO_IMAGE", "Ce projet n'a pas d'image source",
                  "Importer l'image à reproduire avant de chercher des fondus."));
    }
    std::vector<const document::EmbroideryObject*> zones;
    for (const auto& object : project.embroidery_objects) {
        if (object.visible && document::boundary_blend_fill_supported(object) &&
            !document::find_boundary_blend(project, object.id)) {
            zones.push_back(&object);
        }
    }
    std::vector<BoundaryBlendProposal> found;
    for (std::size_t i = 0; i < zones.size(); ++i) {
        for (std::size_t j = i + 1; j < zones.size(); ++j) {
            const auto& za = *zones[i];
            const auto& zb = *zones[j];
            if (boundary_blend_refusal(project, za.id, zb.id)) {
                continue; // pas de frontière, même couleur, même forme…
            }
            stitch_generation::BoundaryBlendSpec spec;
            const auto plan = stitch_generation::plan_boundary_blend(
                stitch_generation::boundary_blend_zone(project, za),
                stitch_generation::boundary_blend_zone(project, zb), spec);
            if (!plan) {
                continue;
            }
            std::vector<double> widthsA;
            std::vector<double> widthsB;
            std::vector<double> spans;
            std::size_t total = 0;
            for (const auto& line : plan->frontier) {
                const auto cumulative = geometry::cumulative_lengths(line.points);
                const double length = cumulative.empty() ? 0.0 : cumulative.back();
                for (double s = 0.5 * options.step.value; s < length; s += options.step.value) {
                    ++total;
                    const Vec2um p = geometry::point_at_length(line.points, cumulative, s);
                    const Vec2um tangent = geometry::tangent_at_length(line.points, cumulative, s);
                    double nx = -static_cast<double>(tangent.y.value);
                    double ny = static_cast<double>(tangent.x.value);
                    const double nlen = std::hypot(nx, ny);
                    if (nlen <= 0.0) {
                        continue;
                    }
                    nx /= nlen;
                    ny /= nlen;
                    // Orienter la normale de A vers B (distance signée positive côté B).
                    const Vec2um probe{
                        Micrometers{static_cast<std::int32_t>(p.x.value + nx * 500)},
                        Micrometers{static_cast<std::int32_t>(p.y.value + ny * 500)}};
                    if (plan->field->signed_distance(probe) < 0.0) {
                        nx = -nx;
                        ny = -ny;
                    }
                    const auto t = transition_at(img, pxUm, p, nx, ny, options);
                    if (!t) {
                        continue;
                    }
                    widthsA.push_back(std::max(0.0, -t->lo));
                    widthsB.push_back(std::max(0.0, t->hi));
                    spans.push_back(t->hi - t->lo);
                }
            }
            if (spans.empty() || static_cast<double>(spans.size()) <
                                     options.min_valid_share * static_cast<double>(total)) {
                continue;
            }
            const double span = median(spans);
            if (span < static_cast<double>(options.min_transition.value)) {
                continue; // bord net dans l'image
            }
            const auto clampWidth = [](double um) {
                return Micrometers{static_cast<std::int32_t>(std::lround(
                    std::clamp(um, static_cast<double>(document::kBoundaryBlendMinWidth),
                               static_cast<double>(document::kBoundaryBlendMaxWidth))))};
            };
            BoundaryBlendProposal proposal;
            proposal.a = za.id;
            proposal.b = zb.id;
            proposal.options.width_a = clampWidth(median(widthsA));
            proposal.options.width_b = clampWidth(median(widthsB));
            proposal.transition_mm = span / 1000.0;
            proposal.profiles = spans.size();
            proposal.profiles_total = total;
            found.push_back(proposal);
        }
    }
    // Les transitions les plus larges d'abord ; une zone n'entre que dans une proposition.
    std::sort(found.begin(), found.end(), [](const auto& l, const auto& r) {
        if (l.transition_mm != r.transition_mm) {
            return l.transition_mm > r.transition_mm;
        }
        return std::pair{l.a.value, l.b.value} < std::pair{r.a.value, r.b.value};
    });
    std::vector<BoundaryBlendProposal> out;
    std::set<std::uint64_t> used;
    for (const auto& p : found) {
        if (used.count(p.a.value) != 0 || used.count(p.b.value) != 0) {
            continue;
        }
        used.insert(p.a.value);
        used.insert(p.b.value);
        out.push_back(p);
    }
    return out;
}

Result<std::unique_ptr<commands::ICommand>>
make_apply_boundary_blend_proposals_command(const document::Project& project,
                                            const std::vector<BoundaryBlendProposal>& proposals) {
    if (proposals.empty()) {
        return std::unexpected(
            coded(ErrorCategory::OperationImpossible, "E_BLEND_NO_PROPOSAL",
                  "Aucun fondu à la frontière à proposer",
                  "Aucune limite douce entre deux zones voisines dans l'image."));
    }
    auto composite = std::make_unique<commands::CompositeCommand>("Fondus à la frontière proposés");
    for (const auto& p : proposals) {
        auto cmd = make_boundary_blend_command(project, p.a, p.b, p.options);
        if (!cmd) {
            return std::unexpected(cmd.error());
        }
        composite->add(std::move(*cmd));
    }
    return composite;
}

} // namespace openstitch::project_ops
