// SPDX-License-Identifier: Apache-2.0
//
// R02 (docs/performance/gpu-compute/) : PullField a ete reecrit pour la vitesse (grandeurs par
// segment precalculees, index CSR, rejet anticipe des sources lointaines) et ses appelants
// evaluent les points en parallele. Ces tests comparent la nouvelle implementation, bit a bit,
// a l'evaluation d'origine recopiee ci-dessous, et verifient que la simulation, la carte et la
// compensation predite ne dependent pas du nombre de fils.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <utility>
#include <vector>

#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/pull_field.hpp"
#include "openstitch/stitch_generation/pull_map.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

// --- Implementation de reference : copie de PullField avant R02 (ne pas modifier). ---
namespace reference {

constexpr double kUmPerMm = 1000.0;
constexpr double kCutoffSigmas = 3.0;
constexpr double kOverlapPenalty = 0.15;

std::int32_t round_um(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

FabricResponse sanitized(FabricResponse f) {
    f.gain = std::clamp(f.gain, 0.0, 0.2);
    f.poisson = std::clamp(f.poisson, 0.0, 1.0);
    f.sigma_along_mm = std::max(f.sigma_along_mm, 0.05);
    f.sigma_across_mm = std::max(f.sigma_across_mm, 0.05);
    f.grain_anisotropy = std::clamp(f.grain_anisotropy, 0.25, 4.0);
    f.compression = std::clamp(f.compression, 0.0, 2.0);
    f.max_displacement_mm = std::clamp(f.max_displacement_mm, 0.0, 3.0);
    f.calibrated_confidence = std::clamp(f.calibrated_confidence, 0.0, 1.0);
    f.uncalibrated_confidence = std::clamp(f.uncalibrated_confidence, 0.0, 1.0);
    f.compensation_strength = std::clamp(f.compensation_strength, 0.0, 6.0);
    f.compensation_passes = std::clamp(f.compensation_passes, 1, 6);
    return f;
}

class Field {
public:
    Field(std::vector<PullSource> sources, const FabricResponse& fabric)
        : sources_(std::move(sources)), fabric_(sanitized(fabric)) {
        cutoff_um_ =
            kCutoffSigmas * std::max(fabric_.sigma_along_mm, fabric_.sigma_across_mm) * kUmPerMm;
        cell_um_ = cutoff_um_;
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            const PullSource& s = sources_[i];
            if (s.a == s.b || !(s.weight > 0.0)) {
                continue;
            }
            const double x0 = std::min(s.a.x.value, s.b.x.value) - cutoff_um_;
            const double x1 = std::max(s.a.x.value, s.b.x.value) + cutoff_um_;
            const double y0 = std::min(s.a.y.value, s.b.y.value) - cutoff_um_;
            const double y1 = std::max(s.a.y.value, s.b.y.value) + cutoff_um_;
            const auto cx0 = static_cast<std::int64_t>(std::floor(x0 / cell_um_));
            const auto cx1 = static_cast<std::int64_t>(std::floor(x1 / cell_um_));
            const auto cy0 = static_cast<std::int64_t>(std::floor(y0 / cell_um_));
            const auto cy1 = static_cast<std::int64_t>(std::floor(y1 / cell_um_));
            for (std::int64_t cy = cy0; cy <= cy1; ++cy) {
                for (std::int64_t cx = cx0; cx <= cx1; ++cx) {
                    grid_[key_of(cx, cy)].push_back(static_cast<std::uint32_t>(i));
                }
            }
        }
    }

    [[nodiscard]] PullSample sample(Vec2um p, std::uint32_t min_order) const {
        PullSample out;
        const auto cx = static_cast<std::int64_t>(std::floor(p.x.value / cell_um_));
        const auto cy = static_cast<std::int64_t>(std::floor(p.y.value / cell_um_));
        const auto it = grid_.find(key_of(cx, cy));
        if (it == grid_.end()) {
            return out;
        }
        const double sig_a = fabric_.sigma_along_mm * kUmPerMm;
        const double sig_n = fabric_.sigma_across_mm * kUmPerMm;
        double ux = 0.0;
        double uy = 0.0;
        double overlap = 0.0;
        std::uint32_t count = 0;
        const double gx = std::cos(fabric_.grain_angle_rad);
        const double gy = std::sin(fabric_.grain_angle_rad);
        for (const std::uint32_t idx : it->second) {
            const PullSource& s = sources_[idx];
            const double ax = s.a.x.value;
            const double ay = s.a.y.value;
            const double dx = s.b.x.value - ax;
            const double dy = s.b.y.value - ay;
            const double len = std::hypot(dx, dy);
            const double h = len * 0.5;
            const double dirx = dx / len;
            const double diry = dy / len;
            const double rx = p.x.value - (ax + dx * 0.5);
            const double ry = p.y.value - (ay + dy * 0.5);
            const double along = rx * dirx + ry * diry;
            const double across = -rx * diry + ry * dirx;
            const double over = std::max(std::abs(along) - h, 0.0);
            const double w = s.weight * std::exp(-over * over / (2.0 * sig_a * sig_a)) *
                             std::exp(-across * across / (2.0 * sig_n * sig_n));
            if (w < 1e-4) {
                continue;
            }
            overlap += w;
            if (s.order < min_order) {
                continue;
            }
            const double along_cos = dirx * gx + diry * gy;
            const double c2 = along_cos * along_cos;
            const double k_axial = c2 + fabric_.grain_anisotropy * (1.0 - c2);
            const double k_lateral = (1.0 - c2) + fabric_.grain_anisotropy * c2;
            const double axial = -fabric_.gain * k_axial * std::clamp(along, -h, h) * w;
            const double lateral =
                fabric_.poisson * fabric_.gain * k_lateral * h * std::tanh(across / sig_n) * w;
            ux += axial * dirx - lateral * diry;
            uy += axial * diry + lateral * dirx;
            ++count;
        }
        if (count == 0) {
            return out;
        }
        const double packing = 1.0 / (1.0 + fabric_.compression * overlap);
        ux *= packing;
        uy *= packing;
        const double cap = fabric_.max_displacement_mm * kUmPerMm;
        const double norm = std::hypot(ux, uy);
        if (norm > 0.0) {
            const double soft = cap > 0.0 ? cap * std::tanh(norm / cap) : 0.0;
            ux *= soft / norm;
            uy *= soft / norm;
        }
        const double base =
            fabric_.calibrated ? fabric_.calibrated_confidence : fabric_.uncalibrated_confidence;
        out.displacement = {{round_um(ux)}, {round_um(uy)}};
        out.confidence = base / (1.0 + kOverlapPenalty * overlap);
        out.contributors = count;
        return out;
    }

private:
    std::vector<PullSource> sources_;
    FabricResponse fabric_;
    double cell_um_{1.0};
    double cutoff_um_{1.0};
    std::map<std::int64_t, std::vector<std::uint32_t>> grid_;

    [[nodiscard]] static std::int64_t key_of(std::int64_t cx, std::int64_t cy) {
        return (cy + (std::int64_t{1} << 30)) * (std::int64_t{1} << 31) +
               (cx + (std::int64_t{1} << 30));
    }
};

} // namespace reference

bool same_bits(double a, double b) {
    std::uint64_t ua = 0;
    std::uint64_t ub = 0;
    std::memcpy(&ua, &a, sizeof a);
    std::memcpy(&ub, &b, sizeof b);
    return ua == ub;
}

// Sources aleatoires mais reproductibles : traits courts et longs (satins), points d'arret
// (segments tres courts), segments nuls et poids varies (dont des poids sous le seuil de 1e-4
// ou tout proches, pour exercer la borne du rejet anticipe).
std::vector<PullSource> random_sources(std::uint32_t seed, std::size_t count) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> coord(-40'000, 40'000);
    std::uniform_int_distribution<int> length(-9'000, 9'000);
    std::uniform_int_distribution<int> tiny(-60, 60);
    std::uniform_int_distribution<int> kind(0, 9);
    const std::vector<double> weights{1.0, 0.5, 2.5, 1e-4, 1.0001e-4, 9.999e-5, 0.0, 1e-3, 7.0};
    std::uniform_int_distribution<std::size_t> pick(0, weights.size() - 1);
    std::vector<PullSource> sources;
    sources.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Vec2um a{{coord(rng)}, {coord(rng)}};
        const int k = kind(rng);
        Vec2um b = a;
        if (k == 0) {
            b = a; // segment nul : ignore par les deux implementations
        } else if (k <= 2) {
            b = {{a.x.value + tiny(rng)}, {a.y.value + tiny(rng)}};
        } else {
            b = {{a.x.value + length(rng)}, {a.y.value + length(rng)}};
        }
        const double w = kind(rng) < 7 ? 1.0 : weights[pick(rng)];
        sources.push_back({a, b, w, static_cast<std::uint32_t>(i)});
    }
    return sources;
}

std::vector<FabricResponse> fabrics() {
    std::vector<FabricResponse> out;
    out.push_back(FabricResponse{});
    out.push_back(fabric_preset("linen"));
    out.push_back(fabric_preset("stable"));
    FabricResponse rotated = fabric_preset("linen");
    rotated.grain_angle_rad = 0.7;
    rotated.grain_anisotropy = 2.5;
    rotated.calibrated = true;
    out.push_back(rotated);
    FabricResponse narrow{};
    narrow.sigma_along_mm = 0.05;
    narrow.sigma_across_mm = 0.3;
    narrow.compression = 0.0;
    narrow.max_displacement_mm = 0.0;
    out.push_back(narrow);
    return out;
}

struct ThreadCountOverride {
    explicit ThreadCountOverride(const char* value) {
#ifdef _WIN32
        _putenv_s("OPENSTITCH_THREADS", value);
#else
        setenv("OPENSTITCH_THREADS", value, 1);
#endif
    }
    ~ThreadCountOverride() {
#ifdef _WIN32
        _putenv_s("OPENSTITCH_THREADS", "");
#else
        unsetenv("OPENSTITCH_THREADS");
#endif
    }
    ThreadCountOverride(const ThreadCountOverride&) = delete;
    ThreadCountOverride& operator=(const ThreadCountOverride&) = delete;
};

stitch::StitchSequence random_sequence(std::uint32_t seed, std::size_t count) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> step(-2'500, 2'500);
    std::uniform_int_distribution<int> kind(0, 39);
    stitch::StitchSequence seq;
    Vec2um p{{0}, {0}};
    ObjectId source{1};
    for (std::size_t i = 0; i < count; ++i) {
        p = {{p.x.value + step(rng)}, {p.y.value + step(rng)}};
        stitch::StitchCommand c{};
        c.pos = p;
        c.type = stitch::CommandType::Stitch;
        c.source = source;
        const int k = kind(rng);
        if (k == 0) {
            c.type = stitch::CommandType::Jump;
        } else if (k == 1) {
            source = ObjectId{source.value + 1};
            c.source = source;
        }
        seq.commands.push_back(c);
    }
    return seq;
}

} // namespace

TEST_CASE("pull_field equivalence : identique bit a bit a l evaluation d origine",
          "[pull_field][r02]") {
    for (const std::uint32_t seed : {1u, 7u, 2024u}) {
        const auto sources = random_sources(seed, 3'000);
        std::mt19937 rng(seed * 31u + 5u);
        std::uniform_int_distribution<int> coord(-52'000, 52'000);
        std::uniform_int_distribution<std::size_t> near(0, sources.size() - 1);
        std::uniform_int_distribution<int> jitter(-4'000, 4'000);
        std::uniform_int_distribution<std::uint32_t> order(0, 3'200);
        for (const FabricResponse& fabric : fabrics()) {
            const PullField field(sources, fabric);
            const reference::Field ref(sources, fabric);
            std::size_t influenced = 0;
            for (int i = 0; i < 6'000; ++i) {
                // Moitie au hasard, moitie au voisinage d'une source (zone ou le poids franchit
                // le seuil de 1e-4).
                Vec2um p{{coord(rng)}, {coord(rng)}};
                if (i % 2 == 1) {
                    const PullSource& s = sources[near(rng)];
                    p = {{s.a.x.value + jitter(rng)}, {s.a.y.value + jitter(rng)}};
                }
                const std::uint32_t min_order = i % 3 == 0 ? 0u : order(rng);
                const PullSample got = field.sample(p, min_order);
                const PullSample want = ref.sample(p, min_order);
                REQUIRE(got.displacement == want.displacement);
                REQUIRE(got.contributors == want.contributors);
                REQUIRE(same_bits(got.confidence, want.confidence));
                influenced += want.contributors > 0 ? 1 : 0;
            }
            CHECK(influenced > 1'000); // le jeu exerce bien le champ, pas seulement le vide
        }
    }
}

TEST_CASE("pull_field equivalence : simulation, carte et compensation independantes du nombre "
          "de fils",
          "[pull_field][r02]") {
    const auto seq = random_sequence(99u, 12'000);
    const FabricResponse linen = fabric_preset("linen");
    stitch::StitchSequence single;
    stitch::StitchSequence many;
    PullMap mapSingle;
    PullMap mapMany;
    {
        const ThreadCountOverride one("1");
        single = simulate_fabric_pull(seq, linen);
        mapSingle = compute_pull_map(seq, linen, Micrometers{500});
    }
    {
        const ThreadCountOverride eight("8");
        many = simulate_fabric_pull(seq, linen);
        mapMany = compute_pull_map(seq, linen, Micrometers{500});
    }
    REQUIRE(single.commands.size() == many.commands.size());
    std::size_t moved = 0;
    for (std::size_t i = 0; i < single.commands.size(); ++i) {
        REQUIRE(single.commands[i].pos == many.commands[i].pos);
        moved += single.commands[i].pos != seq.commands[i].pos ? 1 : 0;
    }
    CHECK(moved > 1'000);

    // La simulation doit rester celle de l'evaluation d'origine, point par point.
    const reference::Field ref(collect_pull_sources(seq), linen);
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        if (seq.commands[i].type == stitch::CommandType::Stitch) {
            REQUIRE(
                single.commands[i].pos ==
                seq.commands[i].pos +
                    ref.sample(seq.commands[i].pos, static_cast<std::uint32_t>(i)).displacement);
        }
    }

    REQUIRE(mapSingle.columns == mapMany.columns);
    REQUIRE(mapSingle.rows == mapMany.rows);
    REQUIRE(mapSingle.samples.size() == mapMany.samples.size());
    for (std::size_t i = 0; i < mapSingle.samples.size(); ++i) {
        REQUIRE(mapSingle.samples[i].displacement == mapMany.samples[i].displacement);
        REQUIRE(mapSingle.samples[i].contributors == mapMany.samples[i].contributors);
        REQUIRE(same_bits(mapSingle.samples[i].confidence, mapMany.samples[i].confidence));
    }
}
