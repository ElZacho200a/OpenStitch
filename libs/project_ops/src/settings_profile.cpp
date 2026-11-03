// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/settings_profile.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include <nlohmann/json.hpp>

namespace openstitch::project_ops {

namespace {

using json = nlohmann::json;

std::string known_list(std::span<const ProfileKey> known) {
    std::vector<std::string> names;
    for (const auto& k : known) {
        names.push_back(k.name);
    }
    std::ranges::sort(names);
    std::string out;
    for (const auto& n : names) {
        out += (out.empty() ? "" : ", ") + n;
    }
    return out;
}

// Nom connu le plus proche (distance d'édition <= 2) pour la piste « vouliez-vous dire ».
std::string closest(std::string_view key, std::span<const ProfileKey> known) {
    std::string best;
    std::size_t bestDist = 3;
    for (const auto& k : known) {
        const std::size_t n = key.size();
        const std::size_t m = k.name.size();
        std::vector<std::size_t> prev(m + 1);
        std::vector<std::size_t> cur(m + 1);
        for (std::size_t j = 0; j <= m; ++j) {
            prev[j] = j;
        }
        for (std::size_t i = 1; i <= n; ++i) {
            cur[0] = i;
            for (std::size_t j = 1; j <= m; ++j) {
                cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                                   prev[j - 1] + (key[i - 1] == k.name[j - 1] ? 0U : 1U)});
            }
            std::swap(prev, cur);
        }
        if (prev[m] < bestDist) {
            bestDist = prev[m];
            best = k.name;
        }
    }
    return best;
}

} // namespace

Result<std::vector<std::string>>
settings_profile_args(std::string_view json_text, std::span<const ProfileKey> known,
                      const std::set<std::string>& explicit_names) {
    const json doc = json::parse(json_text.begin(), json_text.end(), nullptr, false);
    if (doc.is_discarded()) {
        return fail(ErrorCategory::UserInput, "profil de réglages : JSON invalide",
                    kSettingsProfileSchemaHint);
    }
    if (!doc.is_object()) {
        return fail(ErrorCategory::UserInput, "profil de réglages : un objet JSON est attendu",
                    kSettingsProfileSchemaHint);
    }
    std::map<std::string, const ProfileKey*> byName;
    for (const auto& k : known) {
        byName.emplace(k.name, &k);
    }
    // Clés triées : sortie déterministe quel que soit l'ordre du fichier.
    std::map<std::string, const json*> entries;
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        entries.emplace(it.key(), &it.value());
    }
    std::vector<std::string> args;
    for (const auto& [key, value] : entries) {
        const auto found = byName.find(key);
        if (found == byName.end()) {
            const std::string near = closest(key, known);
            const std::string guess =
                near.empty() ? std::string() : std::format("vouliez-vous dire « {} » ? ", near);
            return fail(ErrorCategory::UserInput,
                        std::format("profil de réglages : clé inconnue « {} »", key),
                        guess + "clés admises : " + known_list(known));
        }
        if (explicit_names.contains(key)) {
            continue;
        }
        if (found->second->flag) {
            if (!value->is_boolean()) {
                return fail(ErrorCategory::UserInput,
                            std::format("profil de réglages : « {} » est un drapeau, "
                                        "true ou false attendu",
                                        key),
                            kSettingsProfileSchemaHint);
            }
            if (value->get<bool>()) {
                args.push_back("--" + key);
            }
            continue;
        }
        std::string text;
        if (value->is_string()) {
            text = value->get<std::string>();
        } else if (value->is_number_integer()) {
            text = std::to_string(value->get<long long>());
        } else if (value->is_number_float()) {
            const double d = value->get<double>();
            if (!std::isfinite(d)) {
                return fail(
                    ErrorCategory::UserInput,
                    std::format("profil de réglages : « {} » n'est pas un nombre fini", key));
            }
            text = std::format("{}", d);
        } else if (value->is_array() &&
                   std::ranges::all_of(*value, [](const json& v) { return v.is_string(); })) {
            for (const auto& v : *value) {
                text += (text.empty() ? "" : ",") + v.get<std::string>();
            }
        } else {
            return fail(ErrorCategory::UserInput,
                        std::format("profil de réglages : valeur inattendue pour « {} »", key),
                        kSettingsProfileSchemaHint);
        }
        args.push_back("--" + key);
        args.push_back(std::move(text));
    }
    return args;
}

} // namespace openstitch::project_ops
