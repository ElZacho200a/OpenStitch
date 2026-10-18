// SPDX-License-Identifier: Apache-2.0
#include "chunk_cache.hpp"

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

#include "openstitch/stitch_generation/generate.hpp"

namespace openstitch::stitch_generation {

namespace {

// Budget mémoire du cache (octets, estimation). Au-delà, les objets les moins récemment
// utilisés sont évincés.
constexpr std::size_t kBudgetBytes = 64U * 1024U * 1024U;

struct Entry {
    document::StitchParams params;
    std::vector<geometry::PathSet> paths;
    std::shared_ptr<const stitch::StitchSequence> chunk;
    std::size_t bytes{0};
    std::list<std::uint64_t>::iterator order; // position dans la liste LRU (front = récent)
};

struct Cache {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, Entry> entries;
    std::list<std::uint64_t> lru;
    std::size_t total_bytes{0};
    std::size_t hits{0};
    std::size_t misses{0};
};

Cache& cache() {
    static Cache instance;
    return instance;
}

std::atomic<bool>& enabled_flag() {
    static std::atomic<bool> enabled{true};
    return enabled;
}

std::size_t estimate_bytes(const std::vector<geometry::PathSet>& paths,
                           const stitch::StitchSequence& chunk) {
    std::size_t bytes = sizeof(Entry) + chunk.commands.size() * sizeof(stitch::StitchCommand);
    for (const geometry::PathSet& set : paths) {
        bytes += (set.outer.nodes.size() + 1) * sizeof(geometry::PathNode);
        for (const geometry::Path& hole : set.holes) {
            bytes += hole.nodes.size() * sizeof(geometry::PathNode);
        }
    }
    return bytes;
}

} // namespace

std::shared_ptr<const stitch::StitchSequence>
chunk_cache_find(ObjectId id, const document::StitchParams& params,
                 const std::vector<geometry::PathSet>& paths) {
    if (!enabled_flag().load(std::memory_order_relaxed)) {
        return nullptr;
    }
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    const auto it = c.entries.find(id.value);
    if (it == c.entries.end() || !(it->second.params == params) || !(it->second.paths == paths)) {
        ++c.misses;
        return nullptr;
    }
    ++c.hits;
    c.lru.splice(c.lru.begin(), c.lru, it->second.order);
    return it->second.chunk;
}

void chunk_cache_store(ObjectId id, const document::StitchParams& params,
                       const std::vector<geometry::PathSet>& paths,
                       std::shared_ptr<const stitch::StitchSequence> chunk) {
    if (!enabled_flag().load(std::memory_order_relaxed) || !chunk) {
        return;
    }
    Cache& c = cache();
    const std::size_t bytes = estimate_bytes(paths, *chunk);
    if (bytes > kBudgetBytes) {
        return; // un seul objet plus gros que le budget : inutile de tout évincer pour lui
    }
    const std::lock_guard<std::mutex> lock(c.mutex);
    if (const auto old = c.entries.find(id.value); old != c.entries.end()) {
        c.total_bytes -= old->second.bytes;
        c.lru.erase(old->second.order);
        c.entries.erase(old);
    }
    c.lru.push_front(id.value);
    Entry entry{params, paths, std::move(chunk), bytes, c.lru.begin()};
    c.entries.emplace(id.value, std::move(entry));
    c.total_bytes += bytes;
    while (c.total_bytes > kBudgetBytes && c.lru.size() > 1) {
        const std::uint64_t victim = c.lru.back();
        const auto v = c.entries.find(victim);
        c.total_bytes -= v->second.bytes;
        c.entries.erase(v);
        c.lru.pop_back();
    }
}

ChunkCacheStats chunk_cache_stats() {
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    return {c.entries.size(), c.total_bytes, c.hits, c.misses};
}

void clear_chunk_cache() {
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    c.entries.clear();
    c.lru.clear();
    c.total_bytes = 0;
    c.hits = 0;
    c.misses = 0;
}

void set_chunk_cache_enabled(bool enabled) {
    enabled_flag().store(enabled, std::memory_order_relaxed);
}

} // namespace openstitch::stitch_generation
