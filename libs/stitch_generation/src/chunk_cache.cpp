// SPDX-License-Identifier: Apache-2.0
#include "chunk_cache.hpp"

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>

#include "openstitch/stitch_generation/generate.hpp"

namespace openstitch::stitch_generation {

namespace {

// Budget mémoire du cache (octets, estimation). Au-delà, les objets les moins récemment
// utilisés sont évincés.
constexpr std::size_t kBudgetBytes = 64U * 1024U * 1024U;

// Clé de la table : identifiant du premier objet + nature de l'entrée (objet indépendant ou
// unité routée). Les deux espaces ne se mélangent donc jamais ; la comparaison exacte du
// contenu reste, dans tous les cas, la seule garantie de validité.
struct Key {
    std::uint64_t id{0};
    bool routed{false};

    bool operator==(const Key&) const = default;
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        return std::hash<std::uint64_t>{}(k.id * 2U + (k.routed ? 1U : 0U));
    }
};

struct Entry {
    std::vector<ObjectId> ids;
    std::vector<document::StitchParams> params;
    std::vector<geometry::PathSet> paths;
    std::optional<RoutedTail> tail; // unités routées seulement
    std::shared_ptr<const stitch::StitchSequence> chunk;
    std::size_t bytes{0};
    std::list<Key>::iterator order; // position dans la liste LRU (front = récent)
};

struct Cache {
    std::mutex mutex;
    std::unordered_map<Key, Entry, KeyHash> entries;
    std::list<Key> lru;
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
                           const stitch::StitchSequence& chunk, std::size_t memberCount) {
    std::size_t bytes = sizeof(Entry) + chunk.commands.size() * sizeof(stitch::StitchCommand) +
                        memberCount * sizeof(document::StitchParams);
    for (const geometry::PathSet& set : paths) {
        bytes += (set.outer.nodes.size() + 1) * sizeof(geometry::PathNode);
        for (const geometry::Path& hole : set.holes) {
            bytes += hole.nodes.size() * sizeof(geometry::PathNode);
        }
    }
    return bytes;
}

const std::vector<geometry::PathSet>& no_paths() {
    static const std::vector<geometry::PathSet> empty;
    return empty;
}

bool matches(const Entry& e, std::span<const RoutedMember> members,
             const std::vector<geometry::PathSet>& paths, const std::optional<RoutedTail>& tail) {
    if (e.ids.size() != members.size() || !(e.tail == tail) || !(e.paths == paths)) {
        return false;
    }
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (e.ids[i] != members[i].id || !(e.params[i] == *members[i].params)) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<const stitch::StitchSequence> find_impl(bool routed,
                                                        std::span<const RoutedMember> members,
                                                        const std::vector<geometry::PathSet>& paths,
                                                        const std::optional<RoutedTail>& tail) {
    if (members.empty() || !enabled_flag().load(std::memory_order_relaxed)) {
        return nullptr;
    }
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    const auto it = c.entries.find(Key{members.front().id.value, routed});
    if (it == c.entries.end() || !matches(it->second, members, paths, tail)) {
        ++c.misses;
        return nullptr;
    }
    ++c.hits;
    c.lru.splice(c.lru.begin(), c.lru, it->second.order);
    return it->second.chunk;
}

void store_impl(bool routed, std::span<const RoutedMember> members,
                const std::vector<geometry::PathSet>& paths, const std::optional<RoutedTail>& tail,
                std::shared_ptr<const stitch::StitchSequence> chunk) {
    if (members.empty() || !enabled_flag().load(std::memory_order_relaxed) || !chunk) {
        return;
    }
    Cache& c = cache();
    const std::size_t bytes = estimate_bytes(paths, *chunk, members.size());
    if (bytes > kBudgetBytes) {
        return; // un seul objet plus gros que le budget : inutile de tout évincer pour lui
    }
    const Key key{members.front().id.value, routed};
    Entry entry;
    entry.ids.reserve(members.size());
    entry.params.reserve(members.size());
    for (const RoutedMember& m : members) {
        entry.ids.push_back(m.id);
        entry.params.push_back(*m.params);
    }
    entry.paths = paths;
    entry.tail = tail;
    entry.chunk = std::move(chunk);
    entry.bytes = bytes;
    const std::lock_guard<std::mutex> lock(c.mutex);
    if (const auto old = c.entries.find(key); old != c.entries.end()) {
        c.total_bytes -= old->second.bytes;
        c.lru.erase(old->second.order);
        c.entries.erase(old);
    }
    c.lru.push_front(key);
    entry.order = c.lru.begin();
    c.entries.emplace(key, std::move(entry));
    c.total_bytes += bytes;
    while (c.total_bytes > kBudgetBytes && c.lru.size() > 1) {
        const Key victim = c.lru.back();
        const auto v = c.entries.find(victim);
        c.total_bytes -= v->second.bytes;
        c.entries.erase(v);
        c.lru.pop_back();
    }
}

} // namespace

std::shared_ptr<const stitch::StitchSequence>
chunk_cache_find(ObjectId id, const document::StitchParams& params,
                 const std::vector<geometry::PathSet>& paths) {
    const RoutedMember member{id, &params};
    return find_impl(false, std::span<const RoutedMember>(&member, 1), paths, std::nullopt);
}

void chunk_cache_store(ObjectId id, const document::StitchParams& params,
                       const std::vector<geometry::PathSet>& paths,
                       std::shared_ptr<const stitch::StitchSequence> chunk) {
    const RoutedMember member{id, &params};
    store_impl(false, std::span<const RoutedMember>(&member, 1), paths, std::nullopt,
               std::move(chunk));
}

std::shared_ptr<const stitch::StitchSequence>
routed_cache_find(std::span<const RoutedMember> members,
                  const std::vector<geometry::PathSet>* paths,
                  const std::optional<RoutedTail>& tail) {
    return find_impl(true, members, paths != nullptr ? *paths : no_paths(), tail);
}

void routed_cache_store(std::span<const RoutedMember> members,
                        const std::vector<geometry::PathSet>* paths,
                        const std::optional<RoutedTail>& tail,
                        std::shared_ptr<const stitch::StitchSequence> added) {
    store_impl(true, members, paths != nullptr ? *paths : no_paths(), tail, std::move(added));
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
    clear_router_cache();
    clear_length_bounds_cache();
    clear_knockout_cache();
}

bool chunk_cache_enabled() {
    return enabled_flag().load(std::memory_order_relaxed);
}

void set_chunk_cache_enabled(bool enabled) {
    enabled_flag().store(enabled, std::memory_order_relaxed);
}

} // namespace openstitch::stitch_generation
