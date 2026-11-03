// SPDX-License-Identifier: Apache-2.0
#include "openstitch/lettering/gpos.hpp"

#include <algorithm>
#include <bit>

namespace openstitch::lettering {

namespace {

struct Reader {
    const std::vector<std::uint8_t>& d;

    // Lecture bornée : hors table -> 0 (jamais d'accès invalide).
    [[nodiscard]] std::uint32_t u16(std::size_t off) const {
        if (off + 2 > d.size()) {
            return 0;
        }
        return (static_cast<std::uint32_t>(d[off]) << 8) | d[off + 1];
    }
    [[nodiscard]] std::int32_t s16(std::size_t off) const {
        return static_cast<std::int16_t>(u16(off));
    }
    [[nodiscard]] std::uint32_t u32(std::size_t off) const {
        return (u16(off) << 16) | u16(off + 2);
    }
    [[nodiscard]] std::uint32_t tag(std::size_t off) const { return u32(off); }
};

constexpr std::uint32_t kKernTag = (static_cast<std::uint32_t>('k') << 24) |
                                   (static_cast<std::uint32_t>('e') << 16) |
                                   (static_cast<std::uint32_t>('r') << 8) | 'n';

// Index de couverture du glyphe, ou -1.
int coverage_index(const Reader& r, std::size_t cov, std::uint16_t glyph) {
    const std::uint32_t format = r.u16(cov);
    const std::uint32_t count = r.u16(cov + 2);
    if (format == 1) {
        // Glyphes triés : recherche dichotomique.
        std::uint32_t lo = 0;
        std::uint32_t hi = count;
        while (lo < hi) {
            const std::uint32_t mid = (lo + hi) / 2;
            const std::uint32_t g = r.u16(cov + 4 + 2 * mid);
            if (g == glyph) {
                return static_cast<int>(mid);
            }
            if (g < glyph) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return -1;
    }
    if (format == 2) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t rec = cov + 4 + 6 * static_cast<std::size_t>(i);
            const std::uint32_t start = r.u16(rec);
            const std::uint32_t end = r.u16(rec + 2);
            if (glyph >= start && glyph <= end) {
                return static_cast<int>(r.u16(rec + 4) + (glyph - start));
            }
        }
    }
    return -1;
}

std::uint32_t class_of(const Reader& r, std::size_t def, std::uint16_t glyph) {
    const std::uint32_t format = r.u16(def);
    if (format == 1) {
        const std::uint32_t start = r.u16(def + 2);
        const std::uint32_t count = r.u16(def + 4);
        if (glyph >= start && glyph < start + count) {
            return r.u16(def + 6 + 2 * static_cast<std::size_t>(glyph - start));
        }
        return 0;
    }
    if (format == 2) {
        const std::uint32_t count = r.u16(def + 2);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t rec = def + 4 + 6 * static_cast<std::size_t>(i);
            if (glyph >= r.u16(rec) && glyph <= r.u16(rec + 2)) {
                return r.u16(rec + 4);
            }
        }
    }
    return 0;
}

std::size_t value_size(std::uint32_t format) {
    return 2u * static_cast<std::size_t>(std::popcount(format & 0xFFu));
}

// Avancement horizontal (bit 0x4) d'un enregistrement de valeur ; les champs qui le
// précèdent (X/Y de placement) sont sautés.
std::int32_t x_advance(const Reader& r, std::size_t off, std::uint32_t format) {
    if ((format & 0x4u) == 0) {
        return 0;
    }
    std::size_t skip = 0;
    if ((format & 0x1u) != 0) {
        skip += 2;
    }
    if ((format & 0x2u) != 0) {
        skip += 2;
    }
    return r.s16(off + skip);
}

// Résultat d'une sous-table : trouvé (la consultation s'arrête là) + valeur.
struct PairResult {
    bool found{false};
    std::int32_t value{0};
};

PairResult pair_pos(const Reader& r, std::size_t sub, std::uint16_t left, std::uint16_t right) {
    // L'indirection Extension (type 9) est résolue à l'analyse : `sub` est un PairPos.
    const std::uint32_t format = r.u16(sub);
    const std::size_t cov = sub + r.u16(sub + 2);
    const int ci = coverage_index(r, cov, left);
    if (ci < 0) {
        return {};
    }
    const std::uint32_t vf1 = r.u16(sub + 4);
    const std::uint32_t vf2 = r.u16(sub + 6);
    const std::size_t s1 = value_size(vf1);
    const std::size_t s2 = value_size(vf2);
    if (format == 1) {
        const std::uint32_t set_count = r.u16(sub + 8);
        if (static_cast<std::uint32_t>(ci) >= set_count) {
            return {};
        }
        const std::size_t set = sub + r.u16(sub + 10 + 2 * static_cast<std::size_t>(ci));
        const std::uint32_t n = r.u16(set);
        const std::size_t rec_size = 2 + s1 + s2;
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::size_t rec = set + 2 + rec_size * i;
            if (r.u16(rec) == right) {
                return {true, x_advance(r, rec + 2, vf1)};
            }
        }
        return {};
    }
    if (format == 2) {
        const std::size_t def1 = sub + r.u16(sub + 8);
        const std::size_t def2 = sub + r.u16(sub + 10);
        const std::uint32_t c1n = r.u16(sub + 12);
        const std::uint32_t c2n = r.u16(sub + 14);
        const std::uint32_t c1 = class_of(r, def1, left);
        const std::uint32_t c2 = class_of(r, def2, right);
        if (c1 >= c1n || c2 >= c2n) {
            return {};
        }
        const std::size_t rec_size = s1 + s2;
        const std::size_t rec = sub + 16 + rec_size * (static_cast<std::size_t>(c1) * c2n + c2);
        // Une paire de classes à valeur nulle (hors couverture de la classe 0) compte
        // comme « trouvée » : la consultation ne continue pas (règle OpenType).
        return {true, x_advance(r, rec, vf1)};
    }
    return {};
}

} // namespace

std::unique_ptr<GposKerning> GposKerning::parse(std::vector<std::uint8_t> table) {
    std::unique_ptr<GposKerning> out(new GposKerning());
    out->data_ = std::move(table);
    const Reader r{out->data_};
    if (out->data_.size() < 10 || r.u16(0) != 1) {
        return nullptr;
    }
    const std::size_t feature_list = r.u16(6);
    const std::size_t lookup_list = r.u16(8);
    if (feature_list == 0 || lookup_list == 0) {
        return nullptr;
    }
    // Consultations référencées par les fonctionnalités « kern » (ensemble trié :
    // ordre d'application déterministe, sans doublon).
    std::vector<std::uint32_t> indices;
    const std::uint32_t feature_count = r.u16(feature_list);
    for (std::uint32_t f = 0; f < feature_count; ++f) {
        const std::size_t rec = feature_list + 2 + 6 * static_cast<std::size_t>(f);
        if (r.tag(rec) != kKernTag) {
            continue;
        }
        const std::size_t feature = feature_list + r.u16(rec + 4);
        const std::uint32_t n = r.u16(feature + 2);
        for (std::uint32_t i = 0; i < n; ++i) {
            indices.push_back(r.u16(feature + 4 + 2 * static_cast<std::size_t>(i)));
        }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    const std::uint32_t lookup_count = r.u16(lookup_list);
    for (const std::uint32_t index : indices) {
        if (index >= lookup_count) {
            continue;
        }
        const std::size_t lookup = lookup_list + r.u16(lookup_list + 2 + 2 * std::size_t{index});
        const std::uint32_t type = r.u16(lookup);
        const std::uint32_t sub_count = r.u16(lookup + 4);
        std::vector<std::uint32_t> subs;
        for (std::uint32_t s = 0; s < sub_count; ++s) {
            std::size_t sub = lookup + r.u16(lookup + 6 + 2 * static_cast<std::size_t>(s));
            std::uint32_t real_type = type;
            if (type == 9) {
                if (r.u16(sub) != 1) {
                    continue;
                }
                real_type = r.u16(sub + 2);
                sub += r.u32(sub + 4);
            }
            if (real_type == 2 && sub < out->data_.size()) {
                subs.push_back(static_cast<std::uint32_t>(sub));
            }
        }
        if (!subs.empty()) {
            out->lookups_.push_back(std::move(subs));
        }
    }
    if (out->lookups_.empty()) {
        return nullptr;
    }
    return out;
}

double GposKerning::pair_adjustment(std::uint16_t left, std::uint16_t right) const {
    const Reader r{data_};
    std::int64_t total = 0;
    for (const auto& subs : lookups_) {
        for (const std::uint32_t sub : subs) {
            const PairResult res = pair_pos(r, sub, left, right);
            if (res.found) {
                total += res.value;
                break; // première sous-table qui couvre la paire, par consultation
            }
        }
    }
    return static_cast<double>(total);
}

} // namespace openstitch::lettering
