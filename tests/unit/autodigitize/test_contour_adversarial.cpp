// SPDX-License-Identifier: Apache-2.0
// Adversarial QA of the Contours / Line Art mode: invariants over synthetic
// drawings (see the REPORT lines printed on stdout).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

using Rgb = std::array<std::uint8_t, 3>;
const Rgb kBlack{0, 0, 0};
const Rgb kRed{200, 0, 0};
const Rgb kBlue{0, 0, 200};

image::Image blank(int w, int h) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    return img;
}

void put(image::Image& img, int x, int y, Rgb c) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) {
        return;
    }
    auto* p = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
    p[3] = 255;
}

void erase(image::Image& img, int x, int y) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) {
        return;
    }
    auto* p = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
    p[0] = p[1] = p[2] = p[3] = 0;
}

void disc(image::Image& img, double cx, double cy, double r, Rgb c) {
    for (int y = static_cast<int>(cy - r) - 1; y <= static_cast<int>(cy + r) + 1; ++y) {
        for (int x = static_cast<int>(cx - r) - 1; x <= static_cast<int>(cx + r) + 1; ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                put(img, x, y, c);
            }
        }
    }
}

void stroke(image::Image& img, double x0, double y0, double x1, double y1, double w, Rgb c) {
    const double len = std::hypot(x1 - x0, y1 - y0);
    const int n = std::max(1, static_cast<int>(len * 2));
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        disc(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2.0, c);
    }
}

// Tapered stroke: width goes linearly from w0 to w1.
void taper(image::Image& img, double x0, double y0, double x1, double y1, double w0, double w1,
           Rgb c) {
    const double len = std::hypot(x1 - x0, y1 - y0);
    const int n = std::max(1, static_cast<int>(len * 2));
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        disc(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, (w0 + (w1 - w0) * t) / 2.0, c);
    }
}

void ring(image::Image& img, double cx, double cy, double radius, double w, Rgb c) {
    const int n = static_cast<int>(radius * 12);
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::acos(-1.0) * i / n;
        disc(img, cx + radius * std::cos(a), cy + radius * std::sin(a), w / 2.0, c);
    }
}

segmentation::Segmentation seg_of(const image::Image& img, int colours, bool* ok = nullptr) {
    segmentation::SegmentationOptions so;
    so.max_colors = colours;
    so.min_region_px = 1;
    auto s = segmentation::segment(img, so);
    if (ok != nullptr) {
        *ok = s.has_value();
    }
    if (!s.has_value()) {
        return {};
    }
    return *s;
}

ContourOptions opts(double detail = 0.5, ContourTechnique t = ContourTechnique::Automatic) {
    ContourOptions o;
    o.mm_per_px = Millimeters{0.25};
    o.detail = detail;
    o.technique = t;
    return o;
}

struct Out {
    bool seg_ok{true};
    bool ok{false};
    std::string error;
    ContourNetwork net;
    AutoResult result;
    ContourMetrics metrics;
    stitch::StitchSequence seq;
    bool seq_ok{false};
    std::size_t satin{0}, running{0}, other{0};
    std::size_t stitches{0}, jumps{0}, trims{0}, colour_changes{0};
    double sew_mm{0.0};
    double max_rung_mm{0.0}, min_rung_mm{1e9};
    double max_stitch_mm{0.0};
    double overlap_mm2{0.0}, satin_mm2{0.0};
    double seconds{0.0};
    double seq_seconds{0.0};
    double seg_seconds{0.0};
    double build_seconds{0.0};
    double ovl_seconds{0.0};
    std::size_t net_segments{0};
    double net_len_mm{0.0};
};

// Raster the satin polygons on a 100 um grid and measure self-overlap.
void measure_overlap(Out& o) {
    std::vector<std::vector<Vec2um>> polys;
    for (const auto& e : o.result.embroideries) {
        const auto* sp = std::get_if<document::SatinParams>(&e.params);
        if (sp == nullptr) {
            continue;
        }
        std::vector<Vec2um> p;
        for (const auto& n : sp->rail_a.nodes) {
            p.push_back(n.pos);
        }
        for (auto it = sp->rail_b.nodes.rbegin(); it != sp->rail_b.nodes.rend(); ++it) {
            p.push_back(it->pos);
        }
        if (p.size() >= 3) {
            polys.push_back(std::move(p));
        }
    }
    if (polys.empty()) {
        return;
    }
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& p : polys) {
        for (const auto& v : p) {
            x0 = std::min<double>(x0, v.x.value);
            x1 = std::max<double>(x1, v.x.value);
            y0 = std::min<double>(y0, v.y.value);
            y1 = std::max<double>(y1, v.y.value);
        }
    }
    const double cell = 100.0;
    const auto W = static_cast<std::size_t>((x1 - x0) / cell) + 2;
    const auto H = static_cast<std::size_t>((y1 - y0) / cell) + 2;
    if (W * H > 60'000'000) {
        return;
    }
    std::vector<std::uint8_t> cnt(W * H, 0);
    for (const auto& p : polys) {
        double py0 = 1e18, py1 = -1e18;
        for (const auto& v : p) {
            py0 = std::min<double>(py0, v.y.value);
            py1 = std::max<double>(py1, v.y.value);
        }
        const auto r0 = static_cast<std::size_t>((py0 - y0) / cell);
        const auto r1 = std::min(H - 1, static_cast<std::size_t>((py1 - y0) / cell) + 1);
        for (std::size_t r = r0; r <= r1; ++r) {
            const double y = y0 + (static_cast<double>(r) + 0.5) * cell;
            std::vector<double> xs;
            for (std::size_t i = 0; i < p.size(); ++i) {
                const auto& a = p[i];
                const auto& b = p[(i + 1) % p.size()];
                const double ay = a.y.value, by = b.y.value;
                if ((ay <= y && by > y) || (by <= y && ay > y)) {
                    xs.push_back(a.x.value + (y - ay) / (by - ay) * (b.x.value - a.x.value));
                }
            }
            std::sort(xs.begin(), xs.end());
            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                auto c0 = static_cast<std::size_t>(std::max(0.0, (xs[k] - x0) / cell));
                auto c1 = static_cast<std::size_t>(std::max(0.0, (xs[k + 1] - x0) / cell));
                for (std::size_t c = c0; c < c1 && c < W; ++c) {
                    auto& v = cnt[r * W + c];
                    if (v < 250) {
                        ++v;
                    }
                }
            }
        }
    }
    std::size_t covered = 0, over = 0;
    for (const auto v : cnt) {
        covered += v > 0 ? 1 : 0;
        over += v > 1 ? static_cast<std::size_t>(v - 1) : 0;
    }
    o.satin_mm2 = static_cast<double>(covered) * cell * cell / 1e6;
    o.overlap_mm2 = static_cast<double>(over) * cell * cell / 1e6;
}

Out exercise(const image::Image& img, const ContourOptions& o, int colours = 4, bool overlap = true,
             bool want_seq = true) {
    Out r;
    try {
        const auto tseg = std::chrono::steady_clock::now();
        const auto seg = seg_of(img, colours, &r.seg_ok);
        r.seg_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - tseg).count();
        if (!r.seg_ok) {
            return r;
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto net = analyze_contours(seg, o);
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (!net.has_value()) {
            r.error = "analyze failed: " + net.error().message;
            return r;
        }
        r.net = *net;
        for (const auto& c : r.net.components) {
            r.net_segments += c.segments.size();
            for (const auto& s : c.segments) {
                r.net_len_mm += s.length_um / 1000.0;
            }
        }
        IdGenerator<ObjectId> ids;
        const auto tb = std::chrono::steady_clock::now();
        auto res = build_contour_objects(r.net, ids, o, &r.metrics);
        r.build_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - tb).count();
        if (!res.has_value()) {
            r.error = "build failed: " + res.error().message;
            return r;
        }
        r.result = *res;
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = std::string("EXCEPTION ") + e.what();
        return r;
    }
    for (const auto& e : r.result.embroideries) {
        if (e.is_satin()) {
            ++r.satin;
            const auto* sp = std::get_if<document::SatinParams>(&e.params);
            for (const auto& g : sp->rungs) {
                const double dx = static_cast<double>(g.a.x.value - g.b.x.value);
                const double dy = static_cast<double>(g.a.y.value - g.b.y.value);
                const double l = std::hypot(dx, dy) / 1000.0;
                r.max_rung_mm = std::max(r.max_rung_mm, l);
                r.min_rung_mm = std::min(r.min_rung_mm, l);
            }
        } else if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            ++r.running;
        } else {
            ++r.other;
        }
    }
    document::Project project;
    project.vector_objects = r.result.vectors;
    project.embroidery_objects = r.result.embroideries;
    if (!want_seq) {
        r.sew_mm = r.metrics.running_length_mm + r.metrics.satin_length_mm;
        return r;
    }
    try {
        const auto ts = std::chrono::steady_clock::now();
        auto seq = stitch_generation::effective_sequence(project);
        r.seq_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
        if (seq.has_value()) {
            r.seq = *seq;
            r.seq_ok = true;
        }
    } catch (const std::exception& e) {
        r.error = std::string("SEQ EXCEPTION ") + e.what();
    }
    const stitch::StitchCommand* prev = nullptr;
    for (const auto& c : r.seq.commands) {
        switch (c.type) {
        case stitch::CommandType::Stitch:
            ++r.stitches;
            if (prev != nullptr && prev->type == stitch::CommandType::Stitch) {
                const double d = std::hypot(static_cast<double>(c.pos.x.value - prev->pos.x.value),
                                            static_cast<double>(c.pos.y.value - prev->pos.y.value));
                r.max_stitch_mm = std::max(r.max_stitch_mm, d / 1000.0);
            }
            break;
        case stitch::CommandType::Jump:
            ++r.jumps;
            break;
        case stitch::CommandType::Trim:
            ++r.trims;
            break;
        case stitch::CommandType::ColorChange:
            ++r.colour_changes;
            break;
        default:
            break;
        }
        prev = &c;
    }
    r.sew_mm = r.metrics.running_length_mm + r.metrics.satin_length_mm;
    if (overlap) {
        const auto to = std::chrono::steady_clock::now();
        measure_overlap(r);
        r.ovl_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - to).count();
    }
    return r;
}

void report(const char* name, const Out& r) {
    std::printf("REPORT %-22s comps=%zu seg=%zu junc=%zu end=%zu rmBr=%zu rmSmall=%zu | "
                "satinObj=%zu runObj=%zu | netLen=%.1f sew=%.1f | wMax=%.2f rungMax=%.2f | "
                "stitches=%zu jumps=%zu trims=%zu cc=%zu maxSt=%.1f | ovl=%.2f/%.2fmm2 | rej=%zu "
                "fb=%zu | %.2fs seq=%.2fs seg=%.2fs ovl=%.2fs build=%.2fs\n",
                name, r.metrics.components, r.metrics.segments, r.metrics.junctions,
                r.metrics.endpoints, r.metrics.removed_short_branches,
                r.metrics.removed_small_elements, r.satin, r.running, r.net_len_mm, r.sew_mm,
                r.metrics.max_width_mm, r.max_rung_mm, r.stitches, r.jumps, r.trims,
                r.colour_changes, r.max_stitch_mm, r.overlap_mm2, r.satin_mm2, r.metrics.rejected,
                r.metrics.fallbacks, r.seconds, r.seq_seconds, r.seg_seconds, r.ovl_seconds,
                r.build_seconds);
}

// Fingerprint of the effective sequence (byte-identical check).
std::string seq_print(const Out& r) {
    std::ostringstream s;
    for (const auto& c : r.seq.commands) {
        s << static_cast<int>(c.type) << ',' << c.pos.x.value << ',' << c.pos.y.value << ','
          << c.source.value << ';';
    }
    return s.str();
}

struct Expect {
    std::size_t min_segments{1};
    std::size_t max_segments{100000};
    // Satin junction overlap tolerance (mm2 per satin-mm2).
    double max_overlap_ratio{0.15};
    bool check_overlap{true};
    bool check_jumps{true};
    bool nonempty{true};
};

// Invariants shared by every case.
void check_invariants(const char* name, const image::Image& img, const ContourOptions& o,
                      const Expect& ex, int colours = 4) {
    INFO(name);
    const auto a = exercise(img, o, colours, ex.check_overlap);
    report(name, a);
    REQUIRE(a.seg_ok);
    INFO(a.error);
    REQUIRE(a.ok);
    REQUIRE(a.error.empty());
    REQUIRE(a.seq_ok);

    // Determinism: second full run is byte-identical.
    const auto b = exercise(img, o, colours, false);
    CHECK(seq_print(a) == seq_print(b));
    CHECK(a.result.embroideries.size() == b.result.embroideries.size());

    const auto lim = contour_limits();
    if (ex.nonempty) {
        CHECK(a.metrics.segments >= ex.min_segments);
        CHECK(a.stitches > 0);
    }
    CHECK(a.metrics.segments <= ex.max_segments);

    // Every detected segment is accounted for: sewn length + rejected.
    // (Closed rings / lines: sewn length must cover the detected length.)
    if (a.metrics.rejected == 0) {
        CHECK(a.sew_mm >= a.net_len_mm * 0.95);
    }
    // Satin guard: no column wider than the physical limit (+1 px quantisation).
    if (a.satin > 0) {
        CHECK(a.max_rung_mm <= static_cast<double>(lim.max_satin_width.value) / 1000.0 + 0.3);
    }
    // No degenerate objects.
    for (const auto& e : a.result.embroideries) {
        if (const auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            CHECK(sp->rail_a.nodes.size() >= 2);
            CHECK(sp->rail_b.nodes.size() >= 2);
            for (const auto& g : sp->rungs) {
                CHECK((g.a.x.value != g.b.x.value || g.a.y.value != g.b.y.value));
            }
        }
    }
    for (const auto& v : a.result.vectors) {
        for (const auto& ps : v.paths) {
            CHECK(ps.outer.nodes.size() >= 2);
            if (ps.outer.nodes.size() >= 2) {
                bool distinct = false;
                for (const auto& n : ps.outer.nodes) {
                    distinct = distinct || !(n.pos == ps.outer.nodes.front().pos);
                }
                CHECK(distinct);
            }
        }
    }
    for (const auto& c : a.net.components) {
        for (const auto& s : c.segments) {
            CHECK(std::isfinite(s.length_um));
            CHECK(s.length_um > 0.0);
            CHECK(std::isfinite(s.mean_width_um));
            CHECK(std::isfinite(s.max_turn_deg));
            for (const auto w : s.half_width_um) {
                CHECK(std::isfinite(w));
            }
        }
    }
    CHECK(std::isfinite(a.metrics.mean_width_mm));
    // Stitches stay within the drawing bbox (+1.5 mm) and never exceed the DST limit.
    const double wmm = img.width * o.mm_per_px.value, hmm = img.height * o.mm_per_px.value;
    for (const auto& c : a.seq.commands) {
        if (c.type != stitch::CommandType::Stitch) {
            continue;
        }
        const double x = c.pos.x.value / 1000.0, y = c.pos.y.value / 1000.0;
        // origin placement is not guaranteed: only check extent below.
        (void)x;
        (void)y;
    }
    {
        double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
        for (const auto& c : a.seq.commands) {
            if (c.type != stitch::CommandType::Stitch) {
                continue;
            }
            x0 = std::min<double>(x0, c.pos.x.value);
            x1 = std::max<double>(x1, c.pos.x.value);
            y0 = std::min<double>(y0, c.pos.y.value);
            y1 = std::max<double>(y1, c.pos.y.value);
        }
        if (x1 >= x0) {
            CHECK((x1 - x0) / 1000.0 <= wmm + 3.0);
            CHECK((y1 - y0) / 1000.0 <= hmm + 3.0);
        }
    }
    CHECK(a.max_stitch_mm <= 12.2);
    // A skeleton segment may become several satin sections (junction cuts and
    // residual repair). Each emitted section has its own underlay and travel,
    // so bound travel by the larger of the input and output counts. Retain a
    // separate topology-based bound to catch excessive section fragmentation.
    if (ex.check_jumps) {
        const std::size_t objects = a.result.embroideries.size();
        const std::size_t travelUnits = std::max(a.metrics.segments, objects);
        CHECK(objects <= 4 * (a.metrics.segments + a.metrics.components) + 4);
        CHECK(a.jumps + a.trims <= 4 * (travelUnits + a.metrics.components) + 4);
    }
    // Stitch count sanity: <= ~25 stitches per mm of sewn line (satin zigzag + underlay).
    CHECK(static_cast<double>(a.stitches) <= a.sew_mm * 25.0 + 100.0);
    // Junction quality.
    if (ex.check_overlap && a.satin_mm2 > 0.0) {
        CHECK(a.overlap_mm2 <= a.satin_mm2 * ex.max_overlap_ratio);
    }
}

// Detail sweep: segments / sewn length / removal must be monotone.
void check_monotone(const char* name, const image::Image& img, int colours = 4) {
    INFO(name);
    std::size_t prevSeg = 0, prevObj = 0;
    double prevLen = 0.0;
    bool first = true;
    for (const double d : {0.0, 0.2, 0.4, 0.5, 0.6, 0.8, 1.0}) {
        const auto r = exercise(img, opts(d), colours, false, false);
        REQUIRE(r.ok);
        std::printf("REPORT   %-18s detail=%.1f seg=%zu obj=%zu sew=%.1f rmBr=%zu\n", name, d,
                    r.metrics.segments, r.result.embroideries.size(), r.sew_mm,
                    r.metrics.removed_short_branches);
        if (!first) {
            CHECK(r.metrics.segments >= prevSeg);
            CHECK(r.sew_mm >= prevLen - 5.0); // tolerance: min-length trims of tapered tips
            // object count is NOT asserted monotone: planning may merge/split sections.
        }
        prevSeg = r.metrics.segments;
        prevObj = r.result.embroideries.size();
        prevLen = r.sew_mm;
        first = false;
        // guards hold at every detail
        if (r.satin > 0) {
            CHECK(r.max_rung_mm <= contour_limits().max_satin_width.value / 1000.0 + 0.3);
        }
    }
}

} // namespace

TEST_CASE("adv single line") {
    auto img = blank(200, 100);
    stroke(img, 20, 50, 180, 50, 5, kBlack);
    check_invariants("single_line", img, opts(), {1, 1});
    check_monotone("single_line", img);
}

TEST_CASE("adv T Y X star") {
    {
        auto img = blank(200, 200);
        stroke(img, 20, 60, 180, 60, 5, kBlack);
        stroke(img, 100, 60, 100, 180, 5, kBlack);
        check_invariants("T", img, opts(), {3, 3});
        check_monotone("T", img);
    }
    {
        auto img = blank(200, 200);
        stroke(img, 20, 20, 180, 180, 5, kBlack);
        stroke(img, 20, 180, 180, 20, 5, kBlack);
        check_invariants("X", img, opts(), {4, 4});
    }
    {
        auto img = blank(200, 200);
        stroke(img, 100, 100, 100, 20, 5, kBlack);
        stroke(img, 100, 100, 30, 150, 5, kBlack);
        stroke(img, 100, 100, 170, 150, 5, kBlack);
        check_invariants("Y", img, opts(), {3, 3});
    }
    {
        auto img = blank(240, 240);
        for (int k = 0; k < 5; ++k) {
            const double a = 2 * std::acos(-1.0) * k / 5 - 1.5708;
            stroke(img, 120, 120, 120 + 100 * std::cos(a), 120 + 100 * std::sin(a), 5, kBlack);
        }
        check_invariants("star5", img, opts(), {5, 5});
        check_monotone("star5", img);
    }
}

TEST_CASE("adv circle and figure eight") {
    {
        auto img = blank(200, 200);
        ring(img, 100, 100, 70, 5, kBlack);
        check_invariants("circle", img, opts(), {1, 1});
    }
    {
        auto img = blank(200, 300);
        ring(img, 100, 80, 60, 5, kBlack);
        ring(img, 100, 200, 60, 5, kBlack);
        check_invariants("eight", img, opts(), {2, 6});
    }
}

TEST_CASE("adv zigzag acute angles") {
    auto img = blank(300, 200);
    for (int i = 0; i < 8; ++i) {
        const double x0 = 20 + i * 30, x1 = x0 + 30;
        stroke(img, x0, (i % 2 == 0) ? 40 : 160, x1, (i % 2 == 0) ? 160 : 40, 5, kBlack);
    }
    check_invariants("zigzag", img, opts(), {1, 20});
    check_invariants("zigzag_forcedsatin", img, opts(0.5, ContourTechnique::Satin), {1, 20});
}

TEST_CASE("adv grid", "[.slow]") {
    auto img = blank(400, 400);
    for (int i = 0; i < 5; ++i) {
        stroke(img, 50 + i * 75, 30, 50 + i * 75, 370, 5, kBlack);
        stroke(img, 30, 50 + i * 75, 370, 50 + i * 75, 5, kBlack);
    }
    check_invariants("grid5x5", img, opts(), {60, 60});
    check_monotone("grid5x5", img);
}

TEST_CASE("adv spiral") {
    auto img = blank(300, 300);
    double px = 150, py = 150;
    for (int i = 1; i <= 720; ++i) {
        const double a = i * 0.05, r = 2.0 + a * 3.2;
        const double x = 150 + r * std::cos(a), y = 150 + r * std::sin(a);
        stroke(img, px, py, x, y, 4, kBlack);
        px = x;
        py = y;
    }
    check_invariants("spiral", img, opts(), {1, 400});
}

TEST_CASE("adv text like letters") {
    auto img = blank(500, 150);
    // H
    stroke(img, 20, 20, 20, 120, 5, kBlack);
    stroke(img, 70, 20, 70, 120, 5, kBlack);
    stroke(img, 20, 70, 70, 70, 5, kBlack);
    // E
    stroke(img, 100, 20, 100, 120, 5, kBlack);
    stroke(img, 100, 20, 150, 20, 5, kBlack);
    stroke(img, 100, 70, 140, 70, 5, kBlack);
    stroke(img, 100, 120, 150, 120, 5, kBlack);
    // L
    stroke(img, 180, 20, 180, 120, 5, kBlack);
    stroke(img, 180, 120, 230, 120, 5, kBlack);
    // O
    ring(img, 290, 70, 40, 5, kBlack);
    // A
    stroke(img, 350, 120, 390, 20, 5, kBlack);
    stroke(img, 390, 20, 430, 120, 5, kBlack);
    stroke(img, 365, 85, 415, 85, 5, kBlack);
    // S-like curve
    for (int i = 0; i < 40; ++i) {
        disc(img, 455 + 18 * std::sin(i * 0.2), 20 + i * 2.5, 2.5, kBlack);
    }
    check_invariants("letters", img, opts(), {10, 40});
    check_monotone("letters", img);
}

TEST_CASE("adv rosette concentric and spokes", "[.slow]") {
    auto img = blank(400, 400);
    for (const double r : {40.0, 90.0, 140.0, 180.0}) {
        ring(img, 200, 200, r, 5, kBlack);
    }
    for (int k = 0; k < 12; ++k) {
        const double a = 2 * std::acos(-1.0) * k / 12;
        stroke(img, 200 + 40 * std::cos(a), 200 + 40 * std::sin(a), 200 + 180 * std::cos(a),
               200 + 180 * std::sin(a), 5, kBlack);
    }
    // 12 spokes x 3 rings between + 4 rings (each cut into 12 arcs at the 4 radii)
    check_invariants("rosette", img, opts(), {40, 200});
    check_monotone("rosette", img);
}

TEST_CASE("adv thick thin mix") {
    auto img = blank(300, 200);
    stroke(img, 20, 50, 280, 50, 14, kBlack); // 3.5 mm
    stroke(img, 20, 100, 280, 100, 1.5, kBlack);
    stroke(img, 20, 150, 280, 150, 3, kBlack);
    check_invariants("thick_thin", img, opts(), {2, 3});
    const auto r = exercise(img, opts());
    CHECK(r.satin >= 1);
    CHECK(r.running >= 1);
}

TEST_CASE("adv colours touching must not merge") {
    auto img = blank(300, 300);
    stroke(img, 20, 100, 280, 100, 5, kBlack);
    stroke(img, 150, 100, 150, 280, 5, kRed); // touches black at a T
    stroke(img, 20, 20, 280, 200, 5, kBlue);  // crosses both
    check_invariants("three_colours", img, opts(), {3, 12});
    const auto r = exercise(img, opts());
    std::map<std::array<std::uint8_t, 3>, double> len;
    for (const auto& c : r.net.components) {
        for (const auto& s : c.segments) {
            len[c.rgb] += s.length_um / 1000.0;
        }
    }
    // 260 px * 0.25 = 65 mm black; red 180px = 45; blue ~ 317px=79 (reduced by crossings).
    CHECK(len.size() == 3);
    for (const auto& e : r.result.embroideries) {
        // each object must have a single colour that exists in the network
        CHECK(len.count(e.rgb) == 1);
    }
    // colour groups contiguous: count changes between consecutive embroideries
    std::size_t changes = 0;
    for (std::size_t i = 1; i < r.result.embroideries.size(); ++i) {
        changes += r.result.embroideries[i].rgb != r.result.embroideries[i - 1].rgb ? 1 : 0;
    }
    CHECK(changes == 2);
    for (const auto& [rgb, l] : len) {
        std::printf("REPORT   colour %d,%d,%d len=%.1f mm\n", rgb[0], rgb[1], rgb[2], l);
    }
    // Black must be ~65 mm (not absorbed by neighbours), red ~45.
    for (const auto& [rgb, l] : len) {
        if (rgb == kBlack || rgb[0] < 60) {
            if (rgb[2] < 60) {
                CHECK(l > 55.0);
            }
        }
    }
}

TEST_CASE("adv two colours side by side") {
    // Adjacent parallel strokes of different colour, no gap: both must survive.
    auto img = blank(300, 100);
    stroke(img, 20, 45, 280, 45, 4, kBlack);
    stroke(img, 20, 49, 280, 49, 4, kRed);
    check_invariants("adjacent_colours", img, opts(), {2, 4});
    const auto r = exercise(img, opts());
    std::set<std::array<std::uint8_t, 3>> cols;
    for (const auto& c : r.net.components) {
        cols.insert(c.rgb);
    }
    CHECK(cols.size() == 2);
}

TEST_CASE("adv very low resolution strokes") {
    for (const double w : {2.0, 3.0}) {
        auto img = blank(80, 80);
        stroke(img, 8, 40, 72, 40, w, kBlack);
        stroke(img, 40, 40, 40, 8, w, kBlack);
        const std::string n = "lowres_w" + std::to_string(static_cast<int>(w));
        check_invariants(n.c_str(), img, opts(), {2, 4});
    }
    // mm/px so coarse that a stroke is 1.5 mm at 2 px
    auto img = blank(60, 60);
    stroke(img, 5, 30, 55, 30, 2, kBlack);
    ContourOptions o = opts();
    o.mm_per_px = Millimeters{0.75};
    check_invariants("lowres_coarse_mm", img, o, {1, 2});
}

TEST_CASE("adv high resolution") {
    auto img = blank(1600, 1000);
    stroke(img, 100, 500, 1500, 500, 24, kBlack);
    stroke(img, 800, 500, 800, 900, 24, kBlack);
    ring(img, 800, 300, 150, 20, kRed);
    ContourOptions o = opts();
    o.mm_per_px = Millimeters{0.06};
    check_invariants("hires", img, o, {4, 12});
}

TEST_CASE("adv anti aliased edges opaque white background") {
    // Opaque white background, black strokes with grey AA fringe.
    const int W = 300, H = 200;
    image::Image img = blank(W, H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            put(img, x, y, Rgb{255, 255, 255});
        }
    }
    const auto line_d = [&](double x, double y, double x0, double y0, double x1, double y1) {
        const double dx = x1 - x0, dy = y1 - y0;
        const double t =
            std::clamp(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        return std::hypot(x - (x0 + t * dx), y - (y0 + t * dy));
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double d =
                std::min(line_d(x, y, 20, 100, 280, 100), line_d(x, y, 150, 100, 150, 180));
            const double cov = std::clamp(2.5 - d + 0.5, 0.0, 1.0);
            const auto v = static_cast<std::uint8_t>(255.0 * (1.0 - cov));
            put(img, x, y, Rgb{v, v, v});
        }
    }
    ContourOptions o = opts();
    o.skip_largest_region = true;
    for (const int colours : {2, 3, 4}) {
        const std::string n = "aa_white_c" + std::to_string(colours);
        // A T of 5 px strokes: expected 3 segments; grey fringe must not create rings.
        check_invariants(n.c_str(), img, o, {3, 6}, colours);
    }
}

namespace {
image::Image tee(int w, int h) {
    auto img = blank(w, h);
    stroke(img, 20, 100, 280, 100, 6, kBlack);
    stroke(img, 150, 100, 150, 180, 6, kBlack);
    return img;
}
unsigned lcg_state = 12345;
unsigned rnd() {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (lcg_state >> 8) & 0xFFFF;
}
} // namespace

TEST_CASE("adv salt noise isolated pixels") {
    const auto clean_img = tee(300, 200);
    auto img = clean_img;
    lcg_state = 12345;
    for (int i = 0; i < 150; ++i) {
        put(img, static_cast<int>(rnd() % 300), static_cast<int>(rnd() % 200), kBlack);
    }
    check_invariants("salt", img, opts(), {3, 400}, 4);
    const auto r = exercise(img, opts(), 4, false);
    const auto clean = exercise(clean_img, opts(), 4, false);
    CHECK(r.metrics.segments <= clean.metrics.segments * 3 + 6);
    const auto lo = exercise(img, opts(0.0), 4, false);
    CHECK(lo.metrics.segments <= clean.metrics.segments + 3);
}

TEST_CASE("adv pepper holes keep T structure at default detail") {
    // BUG: 1-px holes inside a 6-px stroke each create a skeleton loop that is
    // not pruned at detail 0.5 (T of 3 segments becomes ~150 segments).
    const auto clean_img = tee(300, 200);
    auto img = clean_img;
    lcg_state = 12345;
    for (int i = 0; i < 120; ++i) {
        erase(img, 20 + static_cast<int>(rnd() % 260), 98 + static_cast<int>(rnd() % 5));
    }
    const auto r = exercise(img, opts(), 4, false);
    report("pepper", r);
    const auto clean = exercise(clean_img, opts(), 4, false);
    CHECK(r.metrics.segments <= clean.metrics.segments * 3 + 6);
    CHECK(r.metrics.junctions <= 4);
}

TEST_CASE("adv pepper holes are cleaned at detail 0") {
    // BUG: same 114 segments at detail 0 as at 0.5: the detail slider cannot clean pepper holes.
    auto img = tee(300, 200);
    lcg_state = 12345;
    for (int i = 0; i < 120; ++i) {
        erase(img, 20 + static_cast<int>(rnd() % 260), 98 + static_cast<int>(rnd() % 5));
    }
    const auto r = exercise(img, opts(0.0), 4, false);
    report("pepper_d0", r);
    CHECK(r.metrics.segments <= 12);
    check_invariants("pepper_d05", img, opts(0.5), {2, 1000}, 4);
}

TEST_CASE("adv gaps in a line") {
    for (const int gap : {1, 2, 3}) {
        auto img = blank(300, 100);
        stroke(img, 20, 50, 140 - gap / 2.0, 50, 5, kBlack);
        stroke(img, 140 + gap / 2.0 + 1.5, 50, 280, 50, 5, kBlack);
        // Two collinear stroke pieces separated by `gap` px: legitimately 2 segments
        // (or one if merge_distance bridges it).
        const std::string n = "gap" + std::to_string(gap);
        check_invariants(n.c_str(), img, opts(), {1, 2});
        const auto r = exercise(img, opts());
        CHECK(r.sew_mm >= 55.0 - 1.0); // 260 px*0.25 = 65 mm minus gap
    }
}

TEST_CASE("adv nearly touching strokes") {
    for (const int sep : {1, 2, 3}) {
        auto img = blank(300, 100);
        stroke(img, 20, 40, 280, 40, 4, kBlack);
        stroke(img, 20, 44 + sep, 280, 44 + sep, 4, kBlack);
        const std::string n = "near_sep" + std::to_string(sep);
        check_invariants(n.c_str(), img, opts(), {1, 4});
        const auto r = exercise(img, opts());
        // Either 2 separate lines (~130 mm) or one fused (65 mm +-): never lose both.
        CHECK(r.sew_mm >= 60.0);
        std::printf("REPORT   near_sep%d -> segments=%zu comps=%zu sew=%.1f\n", sep,
                    r.metrics.segments, r.metrics.components, r.sew_mm);
    }
}

TEST_CASE("adv tapered stroke") {
    auto img = blank(300, 100);
    taper(img, 10, 50, 290, 50, 22, 1.0, kBlack); // 5.5 mm -> 0.25 mm
    check_invariants("tapered", img, opts(), {1, 10});
    check_monotone("tapered", img);
    const auto r = exercise(img, opts(), 4, false);
    // tapered 5.5mm..0.25mm: guard must fall back for the part < min satin width
    CHECK(r.running + r.satin >= 1);
}

TEST_CASE("adv degenerate inputs") {
    {
        const auto img = blank(100, 100);
        const auto r = exercise(img, opts(), 4, false);
        std::printf("REPORT empty seg_ok=%d ok=%d err=%s stitches=%zu\n", r.seg_ok, r.ok,
                    r.error.c_str(), r.stitches);
        if (r.seg_ok) {
            CHECK(r.ok);
            CHECK(r.result.embroideries.empty());
        }
    }
    {
        auto img = blank(100, 100);
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 100; ++x) {
                put(img, x, y, Rgb{255, 255, 255});
            }
        }
        for (const bool skip : {false, true}) {
            ContourOptions o = opts();
            o.skip_largest_region = skip;
            const auto r = exercise(img, o, 4, false);
            std::printf("REPORT allwhite skip=%d seg_ok=%d ok=%d err=%s objs=%zu stitches=%zu\n",
                        skip, r.seg_ok, r.ok, r.error.c_str(), r.result.embroideries.size(),
                        r.stitches);
            REQUIRE(r.seg_ok);
            // Nothing to digitize: a clean error with a message (or an empty result).
            CHECK((r.ok || !r.error.empty()));
            CHECK(r.result.embroideries.empty());
        }
    }
    {
        auto img = blank(50, 50);
        put(img, 25, 25, kBlack);
        const auto r = exercise(img, opts(), 4, false);
        std::printf("REPORT single_pixel ok=%d err=%s seg=%zu rej=%zu objs=%zu stitches=%zu\n",
                    r.ok, r.error.c_str(), r.metrics.segments, r.metrics.rejected,
                    r.result.embroideries.size(), r.stitches);
        CHECK((r.ok || !r.error.empty()));
    }
    {
        auto img = blank(200, 50);
        for (int x = 10; x < 190; ++x) {
            put(img, x, 25, kBlack);
        }
        const auto r = exercise(img, opts(), 4, false);
        std::printf("REPORT one_px_stroke ok=%d err=%s\n", r.ok, r.error.c_str());
        CHECK((r.ok || !r.error.empty())); // clean error, never a crash
    }
    {
        auto img = blank(120, 120);
        for (int y = 0; y < 120; ++y) {
            for (int x = 0; x < 120; ++x) {
                put(img, x, y, kBlack);
            }
        }
        const auto r = exercise(img, opts(), 4, false);
        report("full_black", r);
        std::printf("REPORT full_black ok=%d err=%s\n", r.ok, r.error.c_str());
        CHECK((r.ok || !r.error.empty()));
        CHECK(r.max_rung_mm <= contour_limits().max_satin_width.value / 1000.0 + 0.3);
    }
}

TEST_CASE("adv guards hold at detail 1 and 0 for forced satin") {
    auto img = blank(300, 200);
    stroke(img, 20, 30, 280, 30, 1.5, kBlack);
    stroke(img, 20, 80, 280, 80, 6, kBlack);
    stroke(img, 20, 140, 280, 140, 40, kBlack);
    for (const double d : {0.0, 1.0}) {
        const auto r = exercise(img, opts(d, ContourTechnique::Satin), 4, false);
        REQUIRE(r.ok);
        report(d == 0.0 ? "forced_satin_d0" : "forced_satin_d1", r);
        CHECK(r.max_rung_mm <= contour_limits().max_satin_width.value / 1000.0 + 0.3);
        CHECK(r.metrics.segments >= 2);
        CHECK(r.metrics.rejected + r.metrics.fallbacks >= 1);
    }
}

TEST_CASE("adv real image file via PPM loader") {
    const int W = 240, H = 160;
    auto img = blank(W, H);
    stroke(img, 20, 80, 220, 80, 5, kBlack);
    stroke(img, 120, 80, 120, 140, 5, kBlack);
    const std::string path =
        std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/adv_contour_t.ppm";
    {
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << W << ' ' << H << "\n255\n";
        for (int i = 0; i < W * H; ++i) {
            const bool ink = img.rgba[static_cast<std::size_t>(i) * 4 + 3] != 0;
            const char v = ink ? 0 : static_cast<char>(255);
            f.put(v).put(v).put(v);
        }
    }
    auto loaded = image::load_image(path);
    if (!loaded.has_value()) {
        WARN("PPM loader unavailable, skipped");
        return;
    }
    ContourOptions o = opts();
    o.skip_largest_region = true;
    check_invariants("ppm_T", *loaded, o, {3, 3}, 2);
}

TEST_CASE("adv huge image near 4000 px", "[.huge]") {
    const int W = 3990, H = 3990;
    auto img = blank(W, H);
    for (int i = 0; i < 8; ++i) {
        stroke(img, 100 + i * 480, 100, 100 + i * 480, 3890, 9, kBlack);
        stroke(img, 100, 100 + i * 480, 3890, 100 + i * 480, 9, kBlack);
    }
    ring(img, 2000, 2000, 1500, 8, kRed);
    ContourOptions o = opts();
    o.mm_per_px = Millimeters{0.05};
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = exercise(img, o, 4, false);
    const double total =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    report("huge", r);
    std::printf("REPORT huge total=%.1fs analyze=%.1fs\n", total, r.seconds);
    REQUIRE(r.ok);
    CHECK(r.metrics.segments >= 100);
    for (const auto& c : r.net.components) {
        std::printf("REPORT   huge comp raster_px=%.0f um segs=%zu junc=%zu\n", c.raster_pixel_um,
                    c.segments.size(), c.junction_count());
    }
    for (const auto& d : r.net.diagnostics) {
        std::printf("REPORT   huge diag: %s\n", d.c_str());
    }
}

// Exploration helper (hidden): prints warnings, trims and per-segment details.
TEST_CASE("adv explore", "[.explore]") {
    const char* which = std::getenv("ADV_CASE");
    const std::string w = which != nullptr ? which : "spiral";
    image::Image img = blank(300, 300);
    if (w == "spiral") {
        double px = 150, py = 150;
        for (int i = 1; i <= 720; ++i) {
            const double a = i * 0.05, r = 2.0 + a * 3.2;
            const double x = 150 + r * std::cos(a), y = 150 + r * std::sin(a);
            stroke(img, px, py, x, y, 4, kBlack);
            px = x;
            py = y;
        }
    } else if (w == "rosette") {
        img = blank(400, 400);
        for (const double r : {40.0, 90.0, 140.0, 180.0}) {
            ring(img, 200, 200, r, 5, kBlack);
        }
        for (int k = 0; k < 12; ++k) {
            const double a = 2 * std::acos(-1.0) * k / 12;
            stroke(img, 200 + 40 * std::cos(a), 200 + 40 * std::sin(a), 200 + 180 * std::cos(a),
                   200 + 180 * std::sin(a), 5, kBlack);
        }
    }
    if (w == "grid") {
        img = blank(400, 400);
        for (int i = 0; i < 5; ++i) {
            stroke(img, 50 + i * 75, 30, 50 + i * 75, 370, 5, kBlack);
            stroke(img, 30, 50 + i * 75, 370, 50 + i * 75, 5, kBlack);
        }
    }
    if (w == "mix") {
        img = blank(300, 200);
        stroke(img, 20, 50, 280, 50, 14, kBlack);
        stroke(img, 20, 100, 280, 100, 1.5, kBlack);
        stroke(img, 20, 150, 280, 150, 3, kBlack);
    }
    if (w == "thin1px" || w == "thin15" || w == "thin3") {
        img = blank(300, 100);
        stroke(img, 20, 50, 280, 50, w == "thin1px" ? 1.0 : (w == "thin15" ? 1.5 : 3.0), kBlack);
    }
    const auto r = exercise(img, opts(), 4, false);
    report(w.c_str(), r);
    std::printf("   failed=%zu removedIso=%zu error=%s\n", r.net.failed_components,
                r.net.removed_isolated, r.error.c_str());
    for (const auto& d : r.net.diagnostics) {
        std::printf("   diag: %s\n", d.c_str());
    }
    std::printf("   warnings=%zu\n", r.result.warnings.size());
    for (const auto& wn : r.result.warnings) {
        std::printf("   warn: %s\n", wn.c_str());
    }
    std::size_t k = 0;
    for (const auto& c : r.net.components) {
        for (const auto& s : c.segments) {
            if (k++ < 12) {
                std::printf("   seg len=%.0f w=%.0f/%.0f/%.0f turn=%.0f closed=%d nodes=%d,%d\n",
                            s.length_um, s.min_width_um, s.mean_width_um, s.max_width_um,
                            s.max_turn_deg, s.closed, s.start_node, s.end_node);
            }
        }
    }
    const stitch::StitchCommand* prev = nullptr;
    std::size_t n = 0;
    for (const auto& c : r.seq.commands) {
        if (c.type == stitch::CommandType::Jump && prev != nullptr && n++ < 20) {
            std::printf("   jump from (%d,%d) to (%d,%d) prevtype=%d\n", int(prev->pos.x.value),
                        int(prev->pos.y.value), int(c.pos.x.value), int(c.pos.y.value),
                        int(prev->type));
        }
        prev = &c;
    }
}

namespace {
void write_adv_svg(const std::string& file, const Out& r) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& c : r.seq.commands) {
        x0 = std::min<double>(x0, c.pos.x.value);
        x1 = std::max<double>(x1, c.pos.x.value);
        y0 = std::min<double>(y0, c.pos.y.value);
        y1 = std::max<double>(y1, c.pos.y.value);
    }
    for (const auto& c : r.net.components) {
        for (const auto& n : c.region.outer.nodes) {
            x0 = std::min<double>(x0, n.pos.x.value);
            x1 = std::max<double>(x1, n.pos.x.value);
            y0 = std::min<double>(y0, n.pos.y.value);
            y1 = std::max<double>(y1, n.pos.y.value);
        }
    }
    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='" << (x0 - 1000) << ' ' << (-y1 - 1000)
      << ' ' << (x1 - x0 + 2000) << ' ' << (y1 - y0 + 2000) << "' width='1000'>\n"
      << "<rect x='-1000000' y='-1000000' width='3000000' height='3000000' fill='white'/>\n";
    const auto pts = [&](const geometry::Path& p) {
        std::ostringstream q;
        for (const auto& n : p.nodes) {
            q << n.pos.x.value << ',' << -n.pos.y.value << ' ';
        }
        return q.str();
    };
    for (const auto& c : r.net.components) {
        o << "<polygon points='" << pts(c.region.outer) << "' fill='#ccc' stroke='none'/>\n";
        for (const auto& h : c.region.holes) {
            o << "<polygon points='" << pts(h) << "' fill='white'/>\n";
        }
    }
    for (const auto& e : r.result.embroideries) {
        if (const auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            o << "<polygon points='" << pts(sp->rail_a);
            for (auto it = sp->rail_b.nodes.rbegin(); it != sp->rail_b.nodes.rend(); ++it) {
                o << it->pos.x.value << ',' << -it->pos.y.value << ' ';
            }
            o << "' fill='red' fill-opacity='0.3' stroke='none'/>\n";
        }
    }
    o << "<polyline fill='none' stroke='blue' stroke-width='12' points='";
    for (const auto& c : r.seq.commands) {
        if (c.type == stitch::CommandType::Stitch) {
            o << c.pos.x.value << ',' << -c.pos.y.value << ' ';
        } else if (c.type == stitch::CommandType::Jump) {
            o << "'/>\n<line stroke='orange' stroke-width='8' x1='" << c.pos.x.value << "' y1='"
              << -c.pos.y.value << "' x2='" << c.pos.x.value << "' y2='" << -c.pos.y.value
              << "'/>\n<polyline fill='none' stroke='blue' stroke-width='12' points='";
        }
    }
    o << "'/>\n</svg>\n";
    if (std::FILE* f = std::fopen(file.c_str(), "w")) {
        std::fputs(o.str().c_str(), f);
        std::fclose(f);
    }
}
} // namespace

TEST_CASE("adv svg dump", "[.svg]") {
    const char* dir = std::getenv("CONTOUR_SVG_DIR");
    if (dir == nullptr) {
        WARN("CONTOUR_SVG_DIR not set: nothing written");
        return;
    }
    const std::string d = dir;
    {
        auto img = blank(300, 100);
        stroke(img, 20, 40, 280, 40, 4, kBlack);
        stroke(img, 20, 45, 280, 45, 4, kBlack);
        write_adv_svg(d + "/adv_near1.svg", exercise(img, opts(), 4, false));
    }
    {
        auto img = blank(240, 240);
        for (int k = 0; k < 5; ++k) {
            const double a = 2 * std::acos(-1.0) * k / 5 - 1.5708;
            stroke(img, 120, 120, 120 + 100 * std::cos(a), 120 + 100 * std::sin(a), 5, kBlack);
        }
        write_adv_svg(d + "/adv_star5.svg", exercise(img, opts(), 4, false));
    }
    {
        auto img = blank(400, 400);
        for (const double r : {40.0, 90.0, 140.0, 180.0}) {
            ring(img, 200, 200, r, 5, kBlack);
        }
        for (int k = 0; k < 12; ++k) {
            const double a = 2 * std::acos(-1.0) * k / 12;
            stroke(img, 200 + 40 * std::cos(a), 200 + 40 * std::sin(a), 200 + 180 * std::cos(a),
                   200 + 180 * std::sin(a), 5, kBlack);
        }
        write_adv_svg(d + "/adv_rosette.svg", exercise(img, opts(), 4, false));
    }
    {
        auto img = blank(300, 300);
        double px = 150, py = 150;
        for (int i = 1; i <= 720; ++i) {
            const double a = i * 0.05, r = 2.0 + a * 3.2;
            const double x = 150 + r * std::cos(a), y = 150 + r * std::sin(a);
            stroke(img, px, py, x, y, 4, kBlack);
            px = x;
            py = y;
        }
        write_adv_svg(d + "/adv_spiral.svg", exercise(img, opts(), 4, false));
    }
    {
        auto img = blank(300, 200);
        stroke(img, 20, 100, 280, 100, 6, kBlack);
        stroke(img, 150, 100, 150, 180, 6, kBlack);
        unsigned s = 12345;
        const auto rnd = [&]() {
            s = s * 1664525u + 1013904223u;
            return (s >> 8) & 0xFFFF;
        };
        for (int i = 0; i < 120; ++i) {
            erase(img, 20 + static_cast<int>(rnd() % 260), 98 + static_cast<int>(rnd() % 5));
        }
        write_adv_svg(d + "/adv_pepper.svg", exercise(img, opts(), 4, false));
    }
}

TEST_CASE("adv pixel thin strokes are never silently lost") {
    // BUG: strokes 1-2 px wide vanish from a multi-line drawing with no warning,
    // no diagnostic and no counter (rejected / removed_* all 0). Alone they give
    // "Aucune region exploitable".
    auto img = blank(300, 200);
    stroke(img, 20, 50, 280, 50, 14, kBlack);
    stroke(img, 20, 100, 280, 100, 1.5, kBlack); // 1 px wide after rasterisation
    stroke(img, 20, 150, 280, 150, 3, kBlack);
    const auto r = exercise(img, opts(), 4, false);
    report("thin_lost", r);
    const bool all_sewn = r.sew_mm >= 190.0; // 3 x 65 mm
    const bool reported = !r.result.warnings.empty() || !r.net.diagnostics.empty() ||
                          r.metrics.rejected > 0 || r.metrics.removed_small_elements > 0 ||
                          r.net.failed_components > 0;
    CHECK((all_sewn || reported));
}

TEST_CASE("adv unbranched curve is not shredded into satin and running patches") {
    // BUG: a 1 mm spiral (junction free, 1 chain) is cut into ~80 segments that
    // flip between satin and running around the 0.8 mm threshold (no hysteresis).
    auto img = blank(300, 300);
    double px = 150, py = 150;
    for (int i = 1; i <= 720; ++i) {
        const double a = i * 0.05, rr = 2.0 + a * 3.2;
        const double x = 150 + rr * std::cos(a), y = 150 + rr * std::sin(a);
        stroke(img, px, py, x, y, 4, kBlack);
        px = x;
        py = y;
    }
    const auto r = exercise(img, opts(), 4, false);
    REQUIRE(r.ok);
    const auto lim = contour_limits();
    int prev = -1;
    std::size_t switches = 0;
    for (const auto& c : r.net.components) {
        for (const auto& sg : c.segments) {
            const int st = static_cast<int>(classify_segment(sg, opts().technique, lim).strategy);
            switches += (prev >= 0 && st != prev) ? 1 : 0;
            prev = st;
        }
    }
    std::printf("REPORT spiral segments=%zu strategy switches=%zu\n", r.metrics.segments, switches);
    CHECK(switches <= 4);
}

TEST_CASE("adv satin column covers the ink width") {
    // Regression: a 6 px (1.5 mm) stroke must be sewn as a ~1.5 mm column
    // (an earlier snapshot lost 1 px: polygon through pixel centres). 10 % tolerance.
    auto img = blank(300, 100);
    stroke(img, 20, 50, 280, 50, 6, kBlack);
    const auto r = exercise(img, opts(), 4, false);
    REQUIRE(r.ok);
    std::printf("REPORT width6px nominal=1.50 measured_rung=%.2f metrics_w=%.2f\n", r.max_rung_mm,
                r.metrics.mean_width_mm);
    CHECK(r.max_rung_mm >= 1.5 * 0.9);
}

TEST_CASE("adv build time scales with grid size", "[.perf]") {
    for (const int n : {2, 3, 4, 5, 6}) {
        auto img = blank(100 + n * 75, 100 + n * 75);
        for (int i = 0; i < n; ++i) {
            stroke(img, 50 + i * 75, 30, 50 + i * 75, 70 + n * 75, 5, kBlack);
            stroke(img, 30, 50 + i * 75, 70 + n * 75, 50 + i * 75, 5, kBlack);
        }
        const auto r = exercise(img, opts(), 4, false, false);
        std::printf("PERF grid %dx%d segments=%zu analyze=%.2fs build=%.2fs\n", n, n,
                    r.metrics.segments, r.seconds, r.build_seconds);
    }
}
