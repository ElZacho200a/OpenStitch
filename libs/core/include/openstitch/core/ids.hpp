// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <compare>
#include <cstdint>

namespace openstitch {

// Identifiant fort, distinct par étiquette de type. 0 = invalide.
template <typename Tag>
struct Id {
    std::uint64_t value{0};

    constexpr auto operator<=>(const Id&) const = default;
    [[nodiscard]] constexpr bool valid() const { return value != 0; }
};

using ObjectId = Id<struct ObjectIdTag>;
using RegionId = Id<struct RegionIdTag>;
using ColorId  = Id<struct ColorIdTag>;
using ThreadId = Id<struct ThreadIdTag>;

// Compteur monotone (un par document) : les identifiants ne sont jamais
// réutilisés, ce qui garantit des références stables (undo, fichiers projet).
template <typename IdType>
class IdGenerator {
public:
    [[nodiscard]] IdType next() { return IdType{++last_}; }

    // Pour la désérialisation : reprendre après le plus grand id connu.
    void reset(std::uint64_t last) { last_ = last; }
    [[nodiscard]] std::uint64_t last() const { return last_; }

private:
    std::uint64_t last_{0};
};

}  // namespace openstitch
