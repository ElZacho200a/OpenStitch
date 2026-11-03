// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/sheet_registration.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "openstitch/core/parallel.hpp"

namespace openstitch::stitch_analysis {

namespace {

using stitch::CommandType;

std::int32_t round_um(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

// Déplacement nodal du tissu dans un état donné ; vide = tissu au repos.
struct State {
    std::vector<double> ux;
    std::vector<double> uy;
};

struct Lattice {
    std::size_t columns{0};
    std::size_t rows{0};
    double h{1.0};  // côté d'un élément, mm
    double ox{0.0}; // coin bas-gauche, mm
    double oy{0.0};

    // Interpolation bilinéaire du champ nodal en (x, y) mm.
    void sample(const State& s, double x, double y, double& dx, double& dy) const {
        dx = 0.0;
        dy = 0.0;
        if (s.ux.empty()) {
            return;
        }
        const double fx = std::clamp((x - ox) / h, 0.0, static_cast<double>(columns));
        const double fy = std::clamp((y - oy) / h, 0.0, static_cast<double>(rows));
        const std::size_t i = std::min(static_cast<std::size_t>(fx), columns - 1);
        const std::size_t j = std::min(static_cast<std::size_t>(fy), rows - 1);
        const double tx = fx - static_cast<double>(i);
        const double ty = fy - static_cast<double>(j);
        const std::size_t stride = columns + 1;
        const std::array<std::size_t, 4> n{j * stride + i, j * stride + i + 1, (j + 1) * stride + i,
                                           (j + 1) * stride + i + 1};
        const std::array<double, 4> w{(1.0 - tx) * (1.0 - ty), tx * (1.0 - ty), (1.0 - tx) * ty,
                                      tx * ty};
        for (std::size_t k = 0; k < 4; ++k) {
            dx += w[k] * s.ux[n[k]];
            dy += w[k] * s.uy[n[k]];
        }
    }
};

struct BlockSpan {
    std::size_t first{0};
    std::size_t end{0};
};

bool ends_block(CommandType t) {
    return t == CommandType::ColorChange || t == CommandType::Stop;
}

// Coupe aux ColorChange / Stop (inclus dans le bloc qu'ils terminent). Un bloc sans point cousu est
// absorbé par le précédent ; au-delà de `max_blocks`, des blocs consécutifs sont regroupés.
std::vector<BlockSpan> split_blocks(const stitch::StitchSequence& sequence, int max_blocks) {
    const auto& cmds = sequence.commands;
    std::vector<BlockSpan> spans;
    std::size_t first = 0;
    bool has_stitch = false;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        has_stitch = has_stitch || cmds[i].type == CommandType::Stitch;
        if (ends_block(cmds[i].type) || i + 1 == cmds.size()) {
            if (has_stitch || spans.empty()) {
                spans.push_back({first, i + 1});
            } else {
                spans.back().end = i + 1;
            }
            first = i + 1;
            has_stitch = false;
        }
    }
    // Le dernier bloc, sans point cousu, rejoint le précédent.
    if (spans.size() > 1) {
        bool any = false;
        for (std::size_t i = spans.back().first; i < spans.back().end; ++i) {
            any = any || cmds[i].type == CommandType::Stitch;
        }
        if (!any) {
            spans[spans.size() - 2].end = spans.back().end;
            spans.pop_back();
        }
    }
    const std::size_t cap = static_cast<std::size_t>(std::max(1, max_blocks));
    if (spans.size() > cap) {
        const std::size_t stride = (spans.size() + cap - 1) / cap;
        std::vector<BlockSpan> merged;
        for (std::size_t i = 0; i < spans.size(); i += stride) {
            merged.push_back({spans[i].first, spans[std::min(i + stride, spans.size()) - 1].end});
        }
        spans = std::move(merged);
    }
    return spans;
}

struct Measured {
    SheetRegistration registration;
    // Écart du tissu seul (atterrissage - position de la séquence), mm, par commande.
    std::vector<double> fabric_dx;
    std::vector<double> fabric_dy;
};

// `nominal` : séquence visée (alignée commande par commande) ; null = la séquence elle-même.
Measured measure_impl(const stitch::StitchSequence& sequence, const stitch::StitchSequence* nominal,
                      const SheetParams& sheet, const RegistrationParams& reg,
                      const SheetDomain& domain, Micrometers cell, int max_cells) {
    Measured out;
    SheetRegistration& r = out.registration;
    r.calibrated = sheet.calibrated;
    r.visible_gap_mm = std::max(0.0, reg.visible_gap_mm);
    if (sequence.commands.empty()) {
        return out;
    }
    const std::vector<BlockSpan> spans = split_blocks(sequence, reg.max_blocks);

    // Un état par bloc : tissu après couture des blocs 1..k (séquence préfixe).
    // Les résolutions sont indépendantes (lecture seule de la séquence, une case de sortie chacune)
    // ; l'assemblage est séquentiel.
    std::vector<State> states(spans.size());
    std::vector<SheetDeformation> solved(spans.size());
    parallel_for(spans.size(), [&](std::size_t k) {
        stitch::StitchSequence prefix;
        prefix.commands.assign(sequence.commands.begin(),
                               sequence.commands.begin() +
                                   static_cast<std::ptrdiff_t>(spans[k].end));
        solved[k] = solve_sheet_deformation(prefix, sheet, domain, cell, max_cells);
    });
    for (std::size_t k = 0; k < spans.size(); ++k) {
        if (!solved[k].empty()) {
            states[k].ux = solved[k].ux_mm;
            states[k].uy = solved[k].uy_mm;
        }
    }
    r.final_state = std::move(solved.back());
    if (r.final_state.empty()) {
        return out;
    }
    const SheetDeformation& fin = r.final_state;
    Lattice lat;
    lat.columns = fin.columns;
    lat.rows = fin.rows;
    lat.h = static_cast<double>(fin.cell.value) / 1000.0;
    lat.ox = static_cast<double>(fin.origin.x.value) / 1000.0;
    lat.oy = static_cast<double>(fin.origin.y.value) / 1000.0;
    const State& final_state = states.back();
    const double f =
        std::clamp(std::isfinite(reg.own_block_fraction) ? reg.own_block_fraction : 0.5, 0.0, 1.0);

    // État du tissu pendant la couture du bloc k, au point (x, y).
    const auto sewing_state = [&](std::size_t k, double x, double y, double& dx, double& dy) {
        double ax = 0.0;
        double ay = 0.0;
        if (k > 0) {
            lat.sample(states[k - 1], x, y, ax, ay);
        }
        double bx = 0.0;
        double by = 0.0;
        lat.sample(states[k], x, y, bx, by);
        dx = (1.0 - f) * ax + f * bx;
        dy = (1.0 - f) * ay + f * by;
    };

    const auto& cmds = sequence.commands;
    const std::size_t n_cmds = cmds.size();
    out.fabric_dx.assign(n_cmds, 0.0);
    out.fabric_dy.assign(n_cmds, 0.0);
    r.command_offset_um.assign(n_cmds, Vec2um{});
    std::vector<double> res_x(n_cmds, 0.0);
    std::vector<double> res_y(n_cmds, 0.0);
    r.blocks.resize(spans.size());
    for (std::size_t k = 0; k < spans.size(); ++k) {
        ColorBlock& b = r.blocks[k];
        b.first_command = spans[k].first;
        b.end_command = spans[k].end;
        bool first_object = true;
        double sum_x = 0.0;
        double sum_y = 0.0;
        for (std::size_t i = spans[k].first; i < spans[k].end; ++i) {
            const double x = static_cast<double>(cmds[i].pos.x.value) / 1000.0;
            const double y = static_cast<double>(cmds[i].pos.y.value) / 1000.0;
            double fx = 0.0;
            double fy = 0.0;
            lat.sample(final_state, x, y, fx, fy);
            double sx = 0.0;
            double sy = 0.0;
            sewing_state(k, x, y, sx, sy);
            const double ex = fx - sx;
            const double ey = fy - sy;
            out.fabric_dx[i] = ex;
            out.fabric_dy[i] = ey;
            double rx = ex;
            double ry = ey;
            if (nominal != nullptr) {
                rx += x - static_cast<double>(nominal->commands[i].pos.x.value) / 1000.0;
                ry += y - static_cast<double>(nominal->commands[i].pos.y.value) / 1000.0;
            }
            res_x[i] = rx;
            res_y[i] = ry;
            r.command_offset_um[i] = {{round_um(rx * 1000.0)}, {round_um(ry * 1000.0)}};
            if (cmds[i].type == CommandType::Stitch) {
                ++b.stitch_count;
                sum_x += rx;
                sum_y += ry;
                b.max_offset_mm = std::max(b.max_offset_mm, std::hypot(rx, ry));
                if (first_object && cmds[i].source.value != 0) {
                    b.first_object = cmds[i].source;
                    first_object = false;
                }
            }
        }
        if (b.stitch_count > 0) {
            b.mean_offset_x_mm = sum_x / static_cast<double>(b.stitch_count);
            b.mean_offset_y_mm = sum_y / static_cast<double>(b.stitch_count);
        }
        r.max_offset_mm = std::max(r.max_offset_mm, b.max_offset_mm);
    }

    // Décalage entre blocs voisins : écart moyen de chaque bloc par élément, puis plus grande
    // différence entre deux blocs présents.
    const std::size_t element_count = fin.columns * fin.rows;
    const std::size_t block_n = spans.size();
    r.block_count.assign(element_count, 0);
    r.gap_mm.assign(element_count, 0.0F);
    if (block_n < 2) {
        return out;
    }
    struct Acc {
        float x{0.0F};
        float y{0.0F};
        float n{0.0F};
    };
    std::vector<std::vector<Acc>> acc(block_n, std::vector<Acc>(element_count));
    const double step = std::max(0.05, lat.h / 2.0);
    for (std::size_t k = 0; k < block_n; ++k) {
        for (std::size_t i = std::max<std::size_t>(spans[k].first, 1); i < spans[k].end; ++i) {
            if (cmds[i - 1].type != CommandType::Stitch || cmds[i].type != CommandType::Stitch) {
                continue;
            }
            const double ax = static_cast<double>(cmds[i - 1].pos.x.value) / 1000.0;
            const double ay = static_cast<double>(cmds[i - 1].pos.y.value) / 1000.0;
            const double bx = static_cast<double>(cmds[i].pos.x.value) / 1000.0;
            const double by = static_cast<double>(cmds[i].pos.y.value) / 1000.0;
            const double len = std::hypot(bx - ax, by - ay);
            const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
            for (int s = 0; s < n; ++s) {
                const double t = (s + 0.5) / n;
                const double x = ax + t * (bx - ax);
                const double y = ay + t * (by - ay);
                const double c = std::floor((x - lat.ox) / lat.h);
                const double rr = std::floor((y - lat.oy) / lat.h);
                if (c < 0.0 || rr < 0.0 || c >= static_cast<double>(fin.columns) ||
                    rr >= static_cast<double>(fin.rows)) {
                    continue;
                }
                Acc& a = acc[k][static_cast<std::size_t>(rr) * fin.columns +
                                static_cast<std::size_t>(c)];
                a.x += static_cast<float>((1.0 - t) * res_x[i - 1] + t * res_x[i]);
                a.y += static_cast<float>((1.0 - t) * res_y[i - 1] + t * res_y[i]);
                a.n += 1.0F;
            }
        }
    }
    std::vector<std::size_t> present;
    for (std::size_t e = 0; e < element_count; ++e) {
        present.clear();
        for (std::size_t k = 0; k < block_n; ++k) {
            if (acc[k][e].n > 0.0F) {
                present.push_back(k);
            }
        }
        r.block_count[e] = static_cast<std::uint8_t>(std::min<std::size_t>(present.size(), 255));
        double gap = 0.0;
        for (std::size_t a = 0; a < present.size(); ++a) {
            const Acc& pa = acc[present[a]][e];
            for (std::size_t b = a + 1; b < present.size(); ++b) {
                const Acc& pb = acc[present[b]][e];
                gap = std::max(gap, static_cast<double>(std::hypot(pa.x / pa.n - pb.x / pb.n,
                                                                   pa.y / pa.n - pb.y / pb.n)));
            }
        }
        r.gap_mm[e] = static_cast<float>(gap);
        if (gap > r.max_gap_mm) {
            r.max_gap_mm = gap;
            const std::size_t col = e % fin.columns;
            const std::size_t row = e / fin.columns;
            r.max_gap_at = {
                {fin.origin.x.value + round_um((static_cast<double>(col) + 0.5) * lat.h * 1000.0)},
                {fin.origin.y.value + round_um((static_cast<double>(row) + 0.5) * lat.h * 1000.0)}};
        }
        if (gap >= r.visible_gap_mm && present.size() >= 2) {
            r.gap_area_mm2 += lat.h * lat.h;
        }
    }
    return out;
}

} // namespace

SheetRegistration measure_registration(const stitch::StitchSequence& sequence,
                                       const SheetParams& sheet, const RegistrationParams& reg,
                                       const SheetDomain& domain, Micrometers cell,
                                       int max_cells_per_side) {
    return std::move(
        measure_impl(sequence, nullptr, sheet, reg, domain, cell, max_cells_per_side).registration);
}

RegistrationCompensation
compensate_registration(const stitch::StitchSequence& sequence, const SheetParams& sheet,
                        const RegistrationParams& reg, const SheetDomain& domain, Micrometers cell,
                        double strength, int iterations, int max_cells_per_side) {
    RegistrationCompensation out;
    out.sequence = sequence;
    out.calibrated = sheet.calibrated;
    strength = std::clamp(std::isfinite(strength) ? strength : 0.0, 0.0, 1.0);
    iterations = std::clamp(iterations, 0, 6);

    Measured m =
        measure_impl(out.sequence, &sequence, sheet, reg, domain, cell, max_cells_per_side);
    if (m.registration.empty()) {
        return out;
    }
    out.max_offset_before_mm = m.registration.max_offset_mm;
    out.max_gap_before_mm = m.registration.max_gap_mm;
    for (int it = 0; it < iterations; ++it) {
        // C <- N - strength × e(C) : l'écart du tissu seul est mesuré sur la séquence courante.
        for (std::size_t i = 0; i < sequence.commands.size(); ++i) {
            const Vec2um nominal = sequence.commands[i].pos;
            out.sequence.commands[i].pos = {
                {nominal.x.value - round_um(strength * m.fabric_dx[i] * 1000.0)},
                {nominal.y.value - round_um(strength * m.fabric_dy[i] * 1000.0)}};
        }
        m = measure_impl(out.sequence, &sequence, sheet, reg, domain, cell, max_cells_per_side);
        if (m.registration.empty()) {
            out.sequence = sequence;
            return out;
        }
        ++out.iterations;
    }
    out.max_offset_after_mm = m.registration.max_offset_mm;
    out.max_gap_after_mm = m.registration.max_gap_mm;
    return out;
}

SheetRaster rasterize_registration_gap(const SheetRegistration& registration, double scale_mm) {
    SheetRaster out;
    const SheetDeformation& d = registration.final_state;
    if (registration.empty()) {
        return out;
    }
    if (!(scale_mm > 0.0)) {
        scale_mm = 1.0;
    }
    out.width = static_cast<int>(d.columns);
    out.height = static_cast<int>(d.rows);
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    constexpr std::array<std::array<double, 3>, 4> palette = {{{{60.0, 135.0, 230.0}},
                                                               {{65.0, 195.0, 120.0}},
                                                               {{250.0, 215.0, 65.0}},
                                                               {{215.0, 45.0, 45.0}}}};
    for (int y = 0; y < out.height; ++y) {
        const std::size_t row = d.rows - 1 - static_cast<std::size_t>(y);
        for (int x = 0; x < out.width; ++x) {
            const std::size_t e = row * d.columns + static_cast<std::size_t>(x);
            if (registration.block_count[e] < 2) {
                continue;
            }
            const double pos = std::clamp(registration.gap_mm[e] / scale_mm * 3.0, 0.0, 3.0);
            const int lo = std::min(2, static_cast<int>(pos));
            const double t = pos - lo;
            std::uint8_t* dst = out.rgba.data() + (static_cast<std::size_t>(y) * out.width + x) * 4;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                dst[ch] = static_cast<std::uint8_t>(
                    palette[static_cast<std::size_t>(lo)][ch] * (1.0 - t) +
                    palette[static_cast<std::size_t>(lo) + 1][ch] * t);
            }
            dst[3] = 210;
        }
    }
    return out;
}

} // namespace openstitch::stitch_analysis
