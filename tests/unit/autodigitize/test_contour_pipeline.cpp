// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

using Rgb = std::array<std::uint8_t, 3>;

image::Image blank(int w, int h) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    return img;
}

void disc(image::Image& img, double cx, double cy, double r, Rgb c) {
    for (int y = static_cast<int>(cy - r) - 1; y <= static_cast<int>(cy + r) + 1; ++y) {
        for (int x = static_cast<int>(cx - r) - 1; x <= static_cast<int>(cx + r) + 1; ++x) {
            if (x < 0 || y < 0 || x >= img.width || y >= img.height) {
                continue;
            }
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                auto* p = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
                p[0] = c[0];
                p[1] = c[1];
                p[2] = c[2];
                p[3] = 255;
            }
        }
    }
}

// Trait de largeur `w` pixels.
void stroke(image::Image& img, double x0, double y0, double x1, double y1, double w, Rgb c) {
    const double len = std::hypot(x1 - x0, y1 - y0);
    const int n = std::max(1, static_cast<int>(len * 2));
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        disc(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2.0, c);
    }
}

void ring(image::Image& img, double cx, double cy, double radius, double w, Rgb c) {
    const int n = static_cast<int>(radius * 12);
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * 3.14159265358979 * i / n;
        disc(img, cx + radius * std::cos(a), cy + radius * std::sin(a), w / 2.0, c);
    }
}

segmentation::Segmentation seg_of(const image::Image& img, int colours = 4) {
    segmentation::SegmentationOptions so;
    so.max_colors = colours;
    so.min_region_px = 1;
    auto s = segmentation::segment(img, so);
    REQUIRE(s.has_value());
    return *s;
}

const Rgb kBlack{0, 0, 0};

ContourOptions opts(double detail = 0.5, ContourTechnique t = ContourTechnique::Automatic) {
    ContourOptions o;
    o.mm_per_px = Millimeters{0.25};
    o.detail = detail;
    o.technique = t;
    return o;
}

struct Run {
    ContourNetwork net;
    AutoResult result;
    ContourMetrics metrics;
    std::size_t satin{0}, running{0};
};

Run run(const image::Image& img, const ContourOptions& o, int colours = 4) {
    Run r;
    const auto seg = seg_of(img, colours);
    auto net = analyze_contours(seg, o);
    REQUIRE(net.has_value());
    r.net = *net;
    IdGenerator<ObjectId> ids;
    auto res = build_contour_objects(r.net, ids, o, &r.metrics);
    REQUIRE(res.has_value());
    r.result = *res;
    for (const auto& e : r.result.embroideries) {
        r.satin += e.is_satin() ? 1 : 0;
        r.running += std::holds_alternative<document::RunningStitchParams>(e.params) ? 1 : 0;
    }
    return r;
}

bool any_warning(const Run& r, const std::string& needle) {
    return std::any_of(r.result.warnings.begin(), r.result.warnings.end(),
                       [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}

std::string fingerprint(const AutoResult& r) {
    std::ostringstream s;
    for (const auto& v : r.vectors) {
        s << "V" << v.id.value << v.name << int(v.rgb[0]) << ':';
        for (const auto& ps : v.paths) {
            s << ps.outer.closed;
            for (const auto& n : ps.outer.nodes) {
                s << '(' << n.pos.x.value << ',' << n.pos.y.value << ')';
            }
            s << '|' << ps.holes.size();
        }
    }
    for (const auto& e : r.embroideries) {
        s << "E" << e.id.value << e.source_vector.value << e.name << e.params.index();
        if (const auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            for (const auto& n : sp->rail_a.nodes) {
                s << '(' << n.pos.x.value << ',' << n.pos.y.value << ')';
            }
            for (const auto& n : sp->rail_b.nodes) {
                s << '(' << n.pos.x.value << ',' << n.pos.y.value << ')';
            }
        }
        if (const auto* rp = std::get_if<document::RunningStitchParams>(&e.params)) {
            s << rp->repeats;
        }
    }
    for (const auto& w : r.warnings) {
        s << w;
    }
    return s.str();
}

void dump(const char* name, const Run& r) {
    std::printf("[%s] comps=%zu seg=%zu junc=%zu end=%zu removedBr=%zu satinObjs=%zu runObjs=%zu "
                "runLen=%.1f satinLen=%.1f w=%.2f/%.2f/%.2f fb=%zu rej=%zu\n",
                name, r.metrics.components, r.metrics.segments, r.metrics.junctions,
                r.metrics.endpoints, r.metrics.removed_short_branches, r.satin, r.running,
                r.metrics.running_length_mm, r.metrics.satin_length_mm, r.metrics.min_width_mm,
                r.metrics.mean_width_mm, r.metrics.max_width_mm, r.metrics.fallbacks,
                r.metrics.rejected);
    for (const auto& w : r.result.warnings) {
        std::printf("   warn: %s\n", w.c_str());
    }
}

// Dump SVG de diagnostic (hors suite normale : tag [.svg]).
void write_svg(const std::string& file, const Run& r) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& c : r.net.components) {
        for (const auto& n : c.region.outer.nodes) {
            x0 = std::min<double>(x0, n.pos.x.value);
            x1 = std::max<double>(x1, n.pos.x.value);
            y0 = std::min<double>(y0, n.pos.y.value);
            y1 = std::max<double>(y1, n.pos.y.value);
        }
    }
    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='" << (x0 - 2000) << ' ' << (-y1 - 2000)
      << ' ' << (x1 - x0 + 4000) << ' ' << (y1 - y0 + 4000) << "' width='900'>\n";
    const auto pts = [&](const geometry::Path& p) {
        std::ostringstream q;
        for (const auto& n : p.nodes) {
            q << n.pos.x.value << ',' << -n.pos.y.value << ' ';
        }
        return q.str();
    };
    for (const auto& c : r.net.components) {
        o << "<polygon points='" << pts(c.region.outer)
          << "' fill='#ddd' stroke='#999' stroke-width='40'/>\n";
        for (const auto& h : c.region.holes) {
            o << "<polygon points='" << pts(h)
              << "' fill='white' stroke='#999' stroke-width='40'/>\n";
        }
    }
    for (const auto& e : r.result.embroideries) {
        if (const auto* sp = std::get_if<document::SatinParams>(&e.params)) {
            o << "<polyline points='" << pts(sp->rail_a)
              << "' fill='none' stroke='red' stroke-width='60'/>\n";
            o << "<polyline points='" << pts(sp->rail_b)
              << "' fill='none' stroke='orange' stroke-width='60'/>\n";
            for (const auto& g : sp->rungs) {
                o << "<line x1='" << g.a.x.value << "' y1='" << -g.a.y.value << "' x2='"
                  << g.b.x.value << "' y2='" << -g.b.y.value
                  << "' stroke='purple' stroke-width='20'/>\n";
            }
        }
    }
    for (const auto& v : r.result.vectors) {
        if (v.name.find("ligne") == std::string::npos) {
            continue;
        }
        for (const auto& ps : v.paths) {
            o << "<polyline points='" << pts(ps.outer)
              << (ps.outer.closed ? pts(ps.outer).substr(0, pts(ps.outer).find(' ')) : "")
              << "' fill='none' stroke='green' stroke-width='70'/>\n";
        }
    }
    for (const auto& c : r.net.components) {
        for (const auto& s : c.segments) {
            std::ostringstream q;
            for (const auto& p : s.centerline) {
                q << p.x.value << ',' << -p.y.value << ' ';
            }
            o << "<polyline points='" << q.str()
              << "' fill='none' stroke='blue' stroke-width='25'/>\n";
        }
        for (const auto& n : c.nodes) {
            o << "<circle cx='" << n.position.x.value << "' cy='" << -n.position.y.value
              << "' r='150' fill='" << (n.kind == ContourNodeKind::Junction ? "magenta" : "cyan")
              << "'/>\n";
        }
    }
    o << "</svg>\n";
    if (std::FILE* f = std::fopen(file.c_str(), "w")) {
        std::fputs(o.str().c_str(), f);
        std::fclose(f);
    }
}

} // namespace

TEST_CASE("contour svg dump", "[.svg]") {
    const char* dir = std::getenv("CONTOUR_SVG_DIR");
    REQUIRE(dir != nullptr);
    const std::string d = dir;
    {
        auto img = blank(200, 200);
        ring(img, 100, 55, 45, 5, kBlack);
        ring(img, 100, 145, 45, 5, kBlack);
        write_svg(d + "/eight.svg", run(img, opts()));
    }
    {
        auto img = blank(260, 200);
        stroke(img, 10, 100, 250, 100, 5, kBlack);
        stroke(img, 40, 100, 40, 112, 5, kBlack);
        stroke(img, 90, 100, 90, 124, 5, kBlack);
        stroke(img, 140, 100, 140, 140, 5, kBlack);
        stroke(img, 200, 100, 200, 164, 5, kBlack);
        for (const double dt : {0.0, 0.5, 1.0}) {
            write_svg(d + "/spurs_" + std::to_string(static_cast<int>(dt * 10)) + ".svg",
                      run(img, opts(dt)));
        }
    }
    {
        auto img = blank(200, 200);
        stroke(img, 20, 100, 110, 100, 7, kBlack);
        stroke(img, 110, 100, 180, 100, 2, kBlack);
        const auto mixRun = run(img, opts());
        write_svg(d + "/mix.svg", mixRun);
        document::Project project;
        project.vector_objects = mixRun.result.vectors;
        project.embroidery_objects = mixRun.result.embroideries;
        const auto seq = stitch_generation::effective_sequence(project);
        REQUIRE(seq.has_value());
        std::ostringstream o;
        o << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='-25000 -4000 50000 "
             "8000'>\n<polyline fill='none' stroke='black' stroke-width='25' points='";
        for (const auto& c : seq->commands) {
            if (c.type == stitch::CommandType::Stitch) {
                o << c.pos.x.value << ',' << -c.pos.y.value << ' ';
            }
        }
        o << "'/>\n</svg>\n";
        if (std::FILE* f = std::fopen((d + "/mix_stitches.svg").c_str(), "w")) {
            std::fputs(o.str().c_str(), f);
            std::fclose(f);
        }
    }
    {
        auto img = blank(200, 200);
        stroke(img, 20, 20, 180, 180, 5, kBlack);
        stroke(img, 20, 180, 180, 20, 5, kBlack);
        write_svg(d + "/x.svg", run(img, opts()));
    }
    {
        auto img = blank(200, 200);
        stroke(img, 20, 60, 180, 60, 5, kBlack);
        stroke(img, 100, 60, 100, 180, 5, kBlack);
        write_svg(d + "/t.svg", run(img, opts()));
    }
}

TEST_CASE("contour straight line is one satin segment") {
    auto img = blank(200, 200);
    stroke(img, 20, 100, 180, 100, 5, kBlack);
    const auto r = run(img, opts());
    dump("line", r);
    CHECK(r.metrics.components == 1);
    CHECK(r.metrics.segments == 1);
    CHECK(r.metrics.endpoints == 2);
    CHECK(r.metrics.junctions == 0);
    CHECK(r.satin >= 1);
    CHECK(r.running == 0);
    CHECK(r.metrics.mean_width_mm > 1.0);
    CHECK(r.metrics.mean_width_mm < 1.5);
    CHECK(r.metrics.satin_length_mm > 35.0);
}

TEST_CASE("contour T junction is routed as one satin network") {
    auto img = blank(200, 200);
    stroke(img, 20, 60, 180, 60, 5, kBlack);
    stroke(img, 100, 60, 100, 180, 5, kBlack);
    const auto r = run(img, opts());
    dump("T", r);
    CHECK(r.metrics.components == 1);
    CHECK(r.metrics.junctions == 1);
    CHECK(r.metrics.endpoints == 3);
    CHECK(r.metrics.segments == 3);
    CHECK(r.satin >= 2);
    CHECK(r.running == 0);
}

TEST_CASE("contour X crossing has a degree 4 junction") {
    auto img = blank(200, 200);
    stroke(img, 20, 20, 180, 180, 5, kBlack);
    stroke(img, 20, 180, 180, 20, 5, kBlack);
    const auto r = run(img, opts());
    dump("X", r);
    CHECK(r.metrics.components == 1);
    CHECK(r.metrics.junctions == 1);
    CHECK(r.metrics.endpoints == 4);
    CHECK(r.metrics.segments == 4);
}

TEST_CASE("contour Y junction") {
    auto img = blank(200, 200);
    stroke(img, 100, 100, 100, 180, 5, kBlack);
    stroke(img, 100, 100, 40, 30, 5, kBlack);
    stroke(img, 100, 100, 160, 30, 5, kBlack);
    const auto r = run(img, opts());
    dump("Y", r);
    CHECK(r.metrics.junctions == 1);
    CHECK(r.metrics.endpoints == 3);
    CHECK(r.metrics.segments == 3);
}

TEST_CASE("contour circle is one closed segment") {
    auto img = blank(200, 200);
    ring(img, 100, 100, 70, 5, kBlack);
    const auto r = run(img, opts());
    dump("circle", r);
    CHECK(r.metrics.components == 1);
    CHECK(r.metrics.segments == 1);
    CHECK(r.metrics.junctions == 0);
    REQUIRE(!r.net.components.empty());
    CHECK(r.net.components[0].segments[0].closed);
    CHECK(r.satin + r.running >= 1);
}

TEST_CASE("contour thin circle is a closed running path") {
    auto img = blank(200, 200);
    ring(img, 100, 100, 70, 2, kBlack);
    const auto r = run(img, opts());
    dump("thin circle", r);
    REQUIRE(r.running == 1);
    CHECK(r.satin == 0);
    const auto& v = r.result.vectors.front();
    REQUIRE(v.paths.size() == 1);
    CHECK(v.paths[0].outer.closed);
}

TEST_CASE("contour figure eight keeps both loops around the pinch") {
    auto img = blank(200, 200);
    ring(img, 100, 55, 45, 5, kBlack);
    ring(img, 100, 145, 45, 5, kBlack);
    const auto r = run(img, opts());
    dump("eight", r);
    CHECK(r.metrics.components == 1);
    CHECK(r.metrics.junctions >= 1);
    CHECK(r.metrics.junctions <= 2); // pinch may keep two nodes joined by a short bridge
    CHECK(r.metrics.segments >= 2);
    // Both loops are stitched somehow (satin or running), none silently lost.
    CHECK(r.metrics.satin_length_mm + r.metrics.running_length_mm > 120.0);
}

TEST_CASE("contour zigzag with sharp turns falls back to running with a reason") {
    auto img = blank(200, 200);
    stroke(img, 20, 40, 180, 40, 5, kBlack);
    stroke(img, 180, 40, 180, 80, 5, kBlack);
    stroke(img, 180, 80, 20, 80, 5, kBlack);
    stroke(img, 20, 80, 20, 120, 5, kBlack);
    stroke(img, 20, 120, 180, 120, 5, kBlack);
    const auto r = run(img, opts());
    dump("zigzag", r);
    CHECK(r.satin == 0);
    CHECK(r.running >= 1);
    CHECK(r.metrics.fallbacks >= 1);
    CHECK(any_warning(r, "virage brusque"));
}

TEST_CASE("contour thick and thin mix uses satin and running") {
    auto img = blank(200, 200);
    stroke(img, 20, 100, 110, 100, 7, kBlack);
    stroke(img, 110, 100, 180, 100, 2, kBlack);
    const auto r = run(img, opts());
    dump("mix", r);
    CHECK(r.metrics.satin_length_mm > 5.0);
    CHECK(r.metrics.running_length_mm > 5.0);
    CHECK(r.satin >= 1);
    CHECK(r.running >= 1);
}

TEST_CASE("contour two colours are separate contiguous groups") {
    auto img = blank(200, 200);
    stroke(img, 20, 50, 180, 50, 2, Rgb{200, 0, 0});
    stroke(img, 20, 150, 180, 150, 2, Rgb{0, 0, 200});
    stroke(img, 20, 100, 180, 100, 2, Rgb{200, 0, 0});
    const auto r = run(img, opts());
    dump("two colours", r);
    CHECK(r.metrics.components == 3);
    std::vector<Rgb> seq;
    for (const auto& e : r.result.embroideries) {
        if (seq.empty() || seq.back() != e.rgb) {
            seq.push_back(e.rgb);
        }
    }
    CHECK(seq.size() == 2); // one contiguous group per colour
}

TEST_CASE("contour detail monotonicity on spurs") {
    auto img = blank(260, 200);
    stroke(img, 10, 100, 250, 100, 5, kBlack);
    // spurs of 3, 6, 10, 16 mm hanging below the main line
    stroke(img, 40, 100, 40, 112, 5, kBlack);
    stroke(img, 90, 100, 90, 124, 5, kBlack);
    stroke(img, 140, 100, 140, 140, 5, kBlack);
    stroke(img, 200, 100, 200, 164, 5, kBlack);
    std::size_t prevSeg = 0;
    std::size_t prevRemoved = 1000;
    bool first = true;
    for (const double d : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const auto r = run(img, opts(d));
        dump("spurs", r);
        std::printf("   detail=%.2f\n", d);
        if (!first) {
            CHECK(r.metrics.segments >= prevSeg);
            CHECK(r.metrics.removed_short_branches <= prevRemoved);
        }
        first = false;
        prevSeg = r.metrics.segments;
        prevRemoved = r.metrics.removed_short_branches;
    }
    const auto lo = run(img, opts(0.0));
    const auto hi = run(img, opts(1.0));
    CHECK(lo.metrics.segments < hi.metrics.segments);
}

TEST_CASE("contour output is deterministic") {
    auto img = blank(200, 200);
    stroke(img, 20, 60, 180, 60, 5, kBlack);
    stroke(img, 100, 60, 100, 180, 5, kBlack);
    ring(img, 100, 130, 25, 2, Rgb{200, 0, 0});
    const auto a = run(img, opts());
    const auto b = run(img, opts());
    CHECK(fingerprint(a.result) == fingerprint(b.result));
}

TEST_CASE("contour physical guards hold at detail 1") {
    // A forced-satin line thinner than the minimum satin width must degrade to
    // running with a diagnostic, however high the detail.
    auto img = blank(200, 200);
    stroke(img, 20, 100, 180, 100, 2, kBlack);
    const auto r = run(img, opts(1.0, ContourTechnique::Satin));
    dump("forced satin thin", r);
    CHECK(r.satin == 0);
    CHECK(r.running == 1);
    CHECK(r.metrics.fallbacks == 1);
    CHECK(any_warning(r, "minimum satin"));

    // A stroke wider than the maximum satin width is never satin either.
    auto wide = blank(200, 200);
    stroke(wide, 20, 100, 180, 100, 40, kBlack); // 10 mm
    const auto w = run(wide, opts(1.0, ContourTechnique::Satin));
    dump("forced satin wide", w);
    CHECK(w.satin == 0);
    CHECK(any_warning(w, "maximum satin"));

    // A dot has no centerline: rejected with a diagnostic, not silently lost.
    auto dotImg = blank(200, 200);
    stroke(dotImg, 20, 100, 180, 100, 2, kBlack);
    disc(dotImg, 100, 20, 2.5, kBlack);
    const auto dt = run(dotImg, opts(1.0));
    dump("dot", dt);
    CHECK(dt.metrics.removed_small_elements >= 1);
    CHECK((any_warning(dt, "aucune ligne mediane") || any_warning(dt, "trop fin") ||
           any_warning(dt, "element isole")));
}

TEST_CASE("contour classify_segment guards are independent of technique") {
    ContourSegment s;
    s.length_um = 300.0;
    s.mean_width_um = 1500.0;
    const auto lim = contour_limits();
    for (const auto t :
         {ContourTechnique::Automatic, ContourTechnique::Running, ContourTechnique::Satin}) {
        const auto p = classify_segment(s, t, lim);
        CHECK(p.strategy == ContourStrategy::Rejected);
        CHECK(!p.reason.empty());
    }
}

TEST_CASE("contour objects generate real stitches through effective_sequence") {
    // Thin red open line + thick black T: running (open path) and satin.
    auto img = blank(200, 200);
    stroke(img, 20, 30, 180, 30, 2, Rgb{200, 0, 0});
    stroke(img, 20, 100, 180, 100, 5, kBlack);
    stroke(img, 100, 100, 100, 180, 5, kBlack);
    const auto r = run(img, opts());
    document::Project project;
    project.vector_objects = r.result.vectors;
    project.embroidery_objects = r.result.embroideries;
    auto seq = stitch_generation::effective_sequence(project);
    REQUIRE(seq.has_value());
    std::size_t stitches = 0;
    std::size_t redStitches = 0;
    std::int64_t redMinY = 1 << 30, redMaxY = -(1 << 30);
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Stitch) {
            ++stitches;
        }
    }
    const auto stats = stitch::compute_stats(*seq);
    CHECK(stats.color_changes >= 1);
    CHECK(stitches > 200);
    // The red running line is 40 mm long, tripled or single: stitches along y = const.
    for (const auto& e : project.embroidery_objects) {
        if (e.rgb != Rgb{200, 0, 0}) {
            continue;
        }
        REQUIRE(std::holds_alternative<document::RunningStitchParams>(e.params));
    }
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Stitch) {
            const auto* obj = [&]() -> const document::EmbroideryObject* {
                for (const auto& e : project.embroidery_objects) {
                    if (e.id == c.source) {
                        return &e;
                    }
                }
                return nullptr;
            }();
            if (obj != nullptr && obj->rgb == Rgb{200, 0, 0}) {
                ++redStitches;
                redMinY = std::min<std::int64_t>(redMinY, c.pos.y.value);
                redMaxY = std::max<std::int64_t>(redMaxY, c.pos.y.value);
            }
        }
    }
    CHECK(redStitches >= 13);       // 40 mm / 3 mm
    CHECK(redMaxY - redMinY < 600); // follows the centerline, no excursion
}

namespace {
image::Image grid_image(int w, int h, int spacing, double width) {
    auto img = blank(w, h);
    for (int x = 100; x < w - 50; x += spacing) {
        stroke(img, x, 50, x, h - 50, width, kBlack);
    }
    for (int y = 100; y < h - 50; y += spacing) {
        stroke(img, 50, y, w - 50, y, width, kBlack);
    }
    return img;
}
} // namespace

TEST_CASE("contour grid keeps every crossing despite skeleton 2x2 blocks") {
    // Even stroke widths leave 2x2 skeleton blocks at crossings: without the
    // repair, build_skeleton_graph sees no junction and traces through them.
    for (const double width : {6.0, 7.0, 8.0}) {
        const auto img = grid_image(1000, 800, 300, width);
        ContourOptions o = opts();
        o.mm_per_px = Millimeters{0.2};
        const auto net = analyze_contours(seg_of(img, 2), o);
        REQUIRE(net.has_value());
        REQUIRE(net->components.size() == 1);
        const auto& c = net->components[0];
        CHECK(c.junction_count() == 9);
        CHECK(c.endpoint_count() == 12);
        CHECK(c.segments.size() == 24);
        for (const auto& n : c.nodes) {
            if (n.kind == ContourNodeKind::Junction) {
                CHECK(n.degree == 4);
            }
        }
    }
}

TEST_CASE("contour scale probe", "[.scale]") {
    // Large single-component drawing (360 x 240 mm): measures what the raster
    // cap does to junction detection and widths.
    const auto img = grid_image(1800, 1200, 300, 8);
    ContourOptions o = opts();
    o.mm_per_px = Millimeters{0.2};
    const auto seg = seg_of(img, 2);
    const auto t0 = std::chrono::steady_clock::now();
    auto net = analyze_contours(seg, o);
    const auto t1 = std::chrono::steady_clock::now();
    REQUIRE(net.has_value());
    std::printf("seconds=%.2f\n", std::chrono::duration<double>(t1 - t0).count());
    for (const auto& c : net->components) {
        std::printf(" raster_px=%.0f um segs=%zu junc=%zu end=%zu\n", c.raster_pixel_um,
                    c.segments.size(), c.junction_count(), c.endpoint_count());
    }
    for (const auto& d : net->diagnostics) {
        std::printf(" diag: %s\n", d.c_str());
    }
}

TEST_CASE("contour isolated speck is counted as removed") {
    auto img = blank(260, 260);
    stroke(img, 20, 130, 240, 130, 2, kBlack);
    disc(img, 130, 30, 2.0, kBlack);
    for (const double d : {0.1, 0.5}) {
        ContourOptions o;
        o.detail = d;
        const auto r = run(img, o);
        CHECK(r.metrics.removed_small_elements >= 1);
    }
}

TEST_CASE("contour closed ring is split by width regime") {
    // Thick ring (5 px) whose upper third narrows to 2 px: the narrow arc must
    // be running, never satin-sectioned where it narrows.
    auto img = blank(260, 260);
    const int n = 1100;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n;
        const bool narrow = a > 0.3 && a < 2.4;
        disc(img, 130 + 90 * std::cos(a), 130 + 90 * std::sin(a), narrow ? 1.0 : 3.5, kBlack);
    }
    for (const double d : {0.5, 0.9}) {
        ContourOptions o;
        o.detail = d;
        const auto r = run(img, o);
        dump("regime ring", r);
        CHECK(r.metrics.segments >= 2);
        CHECK(r.running >= 1);
        CHECK(r.satin >= 1);
        CHECK_FALSE(any_warning(r, "colonne refusee"));
        CHECK_FALSE(any_warning(r, "branche ignoree"));
        CHECK_FALSE(any_warning(r, "croisement local"));
        CHECK_FALSE(any_warning(r, "interpolee"));

        CHECK(r.metrics.running_length_mm > 40.0);
        CHECK(r.metrics.running_length_mm < 60.0);
        CHECK(r.metrics.satin_length_mm > 90.0);
    }
}
