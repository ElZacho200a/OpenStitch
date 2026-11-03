// SPDX-License-Identifier: Apache-2.0
// Mutations DÉTERMINISTES de documents JSON (HP-QA-014) : même graine -> mêmes mutations, sur
// toute plateforme (générateur maison, pas de distribution de la bibliothèque standard).
// Utilisé par les tests de fuzzing des lots `apply` (project_ops) et des trames MCP.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace openstitch::qa {

// SplitMix64 : petit, rapide, entièrement spécifié.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // Entier dans [0, n[ (n > 0).
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
    bool coin() { return (next() & 1U) != 0; }

private:
    std::uint64_t state_;
};

// Valeurs « méchantes » substituées à une valeur du document.
inline nlohmann::json hostile_value(Rng& rng) {
    using nlohmann::json;
    json deep = json::array();
    json* cursor = &deep;
    for (int i = 0; i < 200; ++i) {
        cursor->push_back(json::array());
        cursor = &cursor->back();
    }
    switch (rng.below(26)) {
    case 0:
        return nullptr;
    case 1:
        return true;
    case 2:
        return false;
    case 3:
        return 0;
    case 4:
        return -1;
    case 5:
        return 1;
    case 6:
        return 2147483647;
    case 7:
        return 4294967296LL;
    case 8:
        return -2147483648LL;
    case 9:
        return 1e308;
    case 10:
        return -1e308;
    case 11:
        return 1e-308;
    case 12:
        return 0.5;
    case 13:
        return "";
    case 14:
        return " ";
    case 15:
        return "#GGGGGG";
    case 16:
        return "$inconnu";
    case 17:
        return "$";
    case 18:
        return std::string(5000, 'a');
    case 19:
        return "\xC3\xA9\xE4\xB8\xAD";
    case 20:
        return "../x";
    case 21:
        return json::array();
    case 22:
        return json::object();
    case 23:
        return deep;
    case 24:
        return json::array({nullptr, 1, "a"});
    default:
        return json{{"a", 1}};
    }
}

namespace detail {

inline void collect(nlohmann::json& node, std::vector<nlohmann::json*>& out) {
    out.push_back(&node);
    if (node.is_structured()) {
        for (auto& child : node) {
            collect(child, out);
        }
    }
}

} // namespace detail

// Mutation structurelle : 1 à 3 modifications de nœuds du document.
inline nlohmann::json mutate_value(nlohmann::json doc, Rng& rng) {
    using nlohmann::json;
    const std::size_t edits = 1 + rng.below(3);
    for (std::size_t e = 0; e < edits; ++e) {
        std::vector<json*> nodes;
        detail::collect(doc, nodes);
        json* target = nodes[rng.below(nodes.size())];
        switch (rng.below(6)) {
        case 0:
        case 1:
            *target = hostile_value(rng);
            break;
        case 2:
            if (target->is_object() && !target->empty()) {
                auto it = target->begin();
                std::advance(it, static_cast<std::ptrdiff_t>(rng.below(target->size())));
                target->erase(it);
            } else if (target->is_array() && !target->empty()) {
                target->erase(target->begin() +
                              static_cast<std::ptrdiff_t>(rng.below(target->size())));
            }
            break;
        case 3:
            if (target->is_object()) {
                (*target)["zz_" + std::to_string(rng.below(100))] = hostile_value(rng);
            } else if (target->is_array()) {
                target->push_back(hostile_value(rng));
            }
            break;
        case 4:
            if (target->is_array() && !target->empty()) {
                const auto i = rng.below(target->size());
                json copy = (*target)[i];
                target->push_back(std::move(copy));
            } else if (target->is_object() && !target->empty()) {
                // Renomme une clé (la valeur est conservée).
                auto it = target->begin();
                std::advance(it, static_cast<std::ptrdiff_t>(rng.below(target->size())));
                json value = it.value();
                const std::string key = it.key();
                target->erase(it);
                (*target)[key + "_"] = std::move(value);
            }
            break;
        default:
            if (target->is_number()) {
                *target = target->is_number_integer()
                              ? json(target->get<double>() * 1000.5)
                              : json(static_cast<long long>(target->get<double>()));
            } else if (target->is_string()) {
                *target = target->get<std::string>() + target->get<std::string>();
            } else if (target->is_boolean()) {
                *target = !target->get<bool>();
            }
            break;
        }
    }
    return doc;
}

// Mutation de texte : 1 à 4 modifications d'octets (corruption de syntaxe incluse).
inline std::string mutate_text(std::string text, Rng& rng) {
    static const char kJunk[] = {'{', '}', '[', ']', '"', ',', ':', '\\', '0', '-', 'e', '.'};
    const std::size_t edits = 1 + rng.below(4);
    for (std::size_t e = 0; e < edits; ++e) {
        if (text.empty()) {
            text.push_back(kJunk[rng.below(sizeof kJunk)]);
            continue;
        }
        const std::size_t pos = rng.below(text.size());
        switch (rng.below(6)) {
        case 0:
            text[pos] = static_cast<char>(rng.below(256));
            break;
        case 1:
            text.erase(pos, 1 + rng.below(std::min<std::size_t>(16, text.size() - pos)));
            break;
        case 2:
            text.insert(pos, 1, kJunk[rng.below(sizeof kJunk)]);
            break;
        case 3: {
            const std::size_t len = 1 + rng.below(std::min<std::size_t>(32, text.size() - pos));
            text.insert(pos, text.substr(pos, len));
            break;
        }
        case 4:
            text.resize(pos); // troncature
            break;
        default:
            text[pos] = kJunk[rng.below(sizeof kJunk)];
            break;
        }
    }
    constexpr std::size_t kMax = 1U << 20;
    if (text.size() > kMax) {
        text.resize(kMax);
    }
    return text;
}

// Mutation d'un document JSON valide : structurelle (le résultat reste du JSON, ce qui exerce la
// validation métier) ou textuelle (le résultat est souvent invalide, ce qui exerce l'analyseur).
inline std::string mutate(const std::string& seed_text, Rng& rng) {
    const auto parsed = nlohmann::json::parse(seed_text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || rng.below(4) == 0) {
        return mutate_text(seed_text, rng);
    }
    const std::string text = mutate_value(parsed, rng).dump();
    return rng.below(5) == 0 ? mutate_text(text, rng) : text;
}

} // namespace openstitch::qa
