// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/auto_satin.hpp"

#include <cstdint>
#include <deque>
#include <memory>

#include "openstitch/auto_satin/skeleton.hpp"

namespace openstitch::auto_satin {

namespace detail {

// Étape « squelette » d'`analyze_region` : ne dépend QUE des positions des
// nœuds de la région (`rasterize` n'utilise que `nodes[].pos`) et des
// paramètres de rasterisation -- jamais de `cleanup`/`thresholds`.
struct SkeletonStage {
    RasterMask mask;
    DistanceField distance;
    RasterMask skeleton;
    SkeletonGraph raw_graph;
};

struct CacheKey {
    std::vector<std::vector<Vec2um>> rings; // extérieur puis trous, dans l'ordre
    Micrometers pixel_size{};
    int max_dimension{0};
    int margin_px{0};
    std::uint64_t hash{0};

    bool operator==(const CacheKey& o) const {
        return hash == o.hash && pixel_size == o.pixel_size && max_dimension == o.max_dimension &&
               margin_px == o.margin_px && rings == o.rings;
    }
};

CacheKey make_key(const geometry::PathSet& region, const SkeletonRasterParameters& raster) {
    CacheKey key;
    key.pixel_size = raster.pixel_size;
    key.max_dimension = raster.max_dimension;
    key.margin_px = raster.margin_px;
    std::uint64_t h = 1469598103934665603ull; // FNV-1a 64 bits
    const auto mix = [&h](std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v);
        h *= 1099511628211ull;
    };
    const auto addRing = [&](const geometry::Path& path) {
        std::vector<Vec2um> ring;
        ring.reserve(path.nodes.size());
        mix(static_cast<std::int64_t>(path.nodes.size()));
        for (const auto& n : path.nodes) {
            ring.push_back(n.pos);
            mix(n.pos.x.value);
            mix(n.pos.y.value);
        }
        key.rings.push_back(std::move(ring));
    };
    addRing(region.outer);
    for (const auto& hole : region.holes) {
        addRing(hole);
    }
    mix(raster.pixel_size.value);
    mix(raster.max_dimension);
    mix(raster.margin_px);
    key.hash = h;
    return key;
}

std::size_t entry_bytes(const CacheKey& key, const SkeletonStage& stage) {
    std::size_t bytes = sizeof(CacheKey) + sizeof(SkeletonStage);
    bytes += key.rings.capacity() * sizeof(std::vector<Vec2um>);
    for (const auto& ring : key.rings) {
        bytes += ring.capacity() * sizeof(Vec2um);
    }
    bytes += stage.mask.pixels.capacity() * sizeof(std::uint8_t);
    bytes += stage.skeleton.pixels.capacity() * sizeof(std::uint8_t);
    bytes += stage.distance.distance_um.capacity() * sizeof(float);
    bytes += stage.raw_graph.nodes.capacity() * sizeof(SkeletonNode);
    bytes += stage.raw_graph.edges.capacity() * sizeof(SkeletonEdge);
    for (const auto& edge : stage.raw_graph.edges) {
        bytes += edge.centerline.capacity() * sizeof(Vec2um);
        bytes += edge.local_radii_um.capacity() * sizeof(double);
    }
    return bytes;
}

// Cache local à UN thread, actif seulement pendant la vie d'un
// `SkeletonCacheScope` (cf. auto_satin.hpp). Borné en mémoire : au-delà de
// `kMaxBytes`, les entrées les plus anciennes sont évincées (effet sur le
// temps de calcul uniquement, jamais sur le résultat).
struct SkeletonCache {
    static constexpr std::size_t kMaxBytes = 64u * 1024u * 1024u;
    struct Entry {
        CacheKey key;
        std::shared_ptr<const SkeletonStage> stage;
        std::size_t bytes{0};
    };
    std::deque<Entry> entries;
    std::size_t bytes{0};

    std::shared_ptr<const SkeletonStage> find(const CacheKey& key) const {
        for (const auto& e : entries) {
            if (e.key == key) {
                return e.stage;
            }
        }
        return nullptr;
    }
    void insert(CacheKey key, std::shared_ptr<const SkeletonStage> stage) {
        const std::size_t b = entry_bytes(key, *stage);
        if (b > kMaxBytes) {
            return;
        }
        while (!entries.empty() && bytes + b > kMaxBytes) {
            bytes -= entries.front().bytes;
            entries.pop_front();
        }
        bytes += b;
        entries.push_back({std::move(key), std::move(stage), b});
    }
};

} // namespace detail

namespace {

using detail::CacheKey;
using detail::SkeletonStage;

thread_local detail::SkeletonCache* tActiveCache = nullptr;

Result<std::shared_ptr<const SkeletonStage>> compute_stage(const geometry::PathSet& region,
                                                           const SkeletonRasterParameters& raster) {
    auto mask = rasterize(region, raster);
    if (!mask) {
        return std::unexpected(mask.error());
    }
    auto stage = std::make_shared<SkeletonStage>();
    stage->distance = distance_transform(*mask);
    stage->skeleton = thin_zhang_suen(*mask);
    stage->raw_graph = build_skeleton_graph(stage->skeleton, stage->distance);
    stage->mask = std::move(*mask);
    return std::shared_ptr<const SkeletonStage>(std::move(stage));
}

} // namespace

SkeletonCacheScope::SkeletonCacheScope() {
    if (tActiveCache == nullptr) {
        owned_ = new detail::SkeletonCache();
        tActiveCache = owned_;
    }
}

SkeletonCacheScope::~SkeletonCacheScope() {
    if (owned_ != nullptr) {
        tActiveCache = nullptr;
        delete owned_;
    }
}

Result<AutoSatinAnalysis> analyze_region(const geometry::PathSet& region,
                                         const AutoSatinParameters& params) {
    std::shared_ptr<const SkeletonStage> stage;
    if (tActiveCache != nullptr && region.outer.nodes.size() >= 3) {
        CacheKey key = detail::make_key(region, params.raster);
        stage = tActiveCache->find(key);
        if (stage == nullptr) {
            auto computed = compute_stage(region, params.raster);
            if (!computed) {
                return std::unexpected(computed.error());
            }
            stage = *computed;
            tActiveCache->insert(std::move(key), stage);
        }
    } else {
        auto computed = compute_stage(region, params.raster);
        if (!computed) {
            return std::unexpected(computed.error());
        }
        stage = *computed;
    }

    AutoSatinAnalysis out;
    out.debug.mask = stage->mask;
    out.debug.distance = stage->distance;
    out.debug.skeleton = stage->skeleton;
    out.debug.raw_graph = stage->raw_graph;
    // Élagage des branches parasites (§11) avant analyse.
    auto cleaned = prune_graph(out.debug.raw_graph, params.cleanup);
    out.debug.graph = std::move(cleaned.graph);
    out.debug.removed_branches = std::move(cleaned.removed);
    out.report =
        evaluate_satinability(region, out.debug.graph, out.debug.distance, params.thresholds);
    return out;
}

} // namespace openstitch::auto_satin
