// SPDX-License-Identifier: Apache-2.0
#pragma once

// CX05, lot 1 : stockage en mémoire de préréglages de numérisation.
// Les réglages sont exactement ceux du moteur CLI/MCP ; aucun second pipeline.
// L'import de l'image ne consulte jamais ce catalogue : la numérisation reste explicite.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openstitch/project_ops/factory.hpp"

namespace openstitch::desktop_v2::studio {

struct AutodigitizePreset {
    std::string name;
    project_ops::DigitizeSettings settings;
    bool builtIn{false};
};

class AutodigitizePresetStore {
public:
    AutodigitizePresetStore() {
        project_ops::DigitizeSettings standard;
        presets_.push_back({"Standard", standard, true});

        auto details = standard;
        details.max_colors = 10;
        details.min_region_px = 8;
        details.smoothing_px = 1;
        presets_.push_back({"Détails fins", details, true});

        auto simple = standard;
        simple.max_colors = 5;
        simple.min_region_px = 32;
        simple.smoothing_px = 5;
        presets_.push_back({"Aplats simples", simple, true});
    }

    [[nodiscard]] const std::vector<AutodigitizePreset>& all() const noexcept { return presets_; }

    [[nodiscard]] std::optional<project_ops::DigitizeSettings> apply(std::string_view name) const {
        const auto it = find(name);
        if (it == presets_.end()) {
            return std::nullopt;
        }
        return it->settings;
    }

    // Refuse les noms vides, les doublons et les noms de préréglages intégrés.
    // Les valeurs ne sont pas interprétées ni appauvries : les options avancées
    // restent transmises à project_ops::DigitizeSettings à l'identique.
    [[nodiscard]] bool save(std::string name, const project_ops::DigitizeSettings& settings) {
        if (name.empty() || name.size() > 64 ||
            std::all_of(name.begin(), name.end(), [](char c) { return c == ' '; })) {
            return false;
        }
        if (find(name) != presets_.end()) {
            return false;
        }
        presets_.push_back({std::move(name), settings, false});
        return true;
    }

    // Édite les options d'un profil utilisateur sans en changer le nom ni
    // l'ordre. Une tentative de modifier un preset intégré reste sans effet.
    [[nodiscard]] bool update(std::string_view name,
                              const project_ops::DigitizeSettings& settings) {
        const auto it =
            std::find_if(presets_.begin(), presets_.end(),
                         [name](const AutodigitizePreset& entry) { return entry.name == name; });
        if (it == presets_.end() || it->builtIn) {
            return false;
        }
        it->settings = settings;
        return true;
    }

    [[nodiscard]] bool remove(std::string_view name) {
        const auto it = find(name);
        if (it == presets_.end() || it->builtIn) {
            return false;
        }
        presets_.erase(it);
        return true;
    }

private:
    using Iterator = std::vector<AutodigitizePreset>::const_iterator;

    [[nodiscard]] Iterator find(std::string_view name) const {
        return std::find_if(presets_.cbegin(), presets_.cend(),
                            [name](const AutodigitizePreset& entry) { return entry.name == name; });
    }

    std::vector<AutodigitizePreset> presets_;
};

} // namespace openstitch::desktop_v2::studio
