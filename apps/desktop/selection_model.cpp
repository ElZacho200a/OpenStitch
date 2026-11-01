// SPDX-License-Identifier: Apache-2.0
#include "selection_model.hpp"

#include <algorithm>
#include <cstddef>

namespace openstitch::desktop {

Selection normalize_selection(Selection selection, const RegionExists& regionExists) {
    // Doublons : la DERNIÈRE occurrence est gardée (le dernier id reste le principal).
    std::vector<ObjectId> objects;
    objects.reserve(selection.objects.size());
    for (std::size_t i = 0; i < selection.objects.size(); ++i) {
        const bool later =
            std::find(selection.objects.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                      selection.objects.end(), selection.objects[i]) != selection.objects.end();
        if (!later) {
            objects.push_back(selection.objects[i]);
        }
    }
    // Sélectionner une région ou une broderie vide la multi-sélection (seul le
    // principal peut subsister, ex. forme source d'une broderie choisie au dock).
    if ((selection.region || selection.embroidery) && objects.size() > 1) {
        objects = {objects.back()};
    }
    std::vector<RegionId> extra;
    if (selection.region) {
        for (const RegionId r : selection.extraRegions) {
            const bool known = regionExists && regionExists(r);
            const bool dup =
                r == *selection.region || std::find(extra.begin(), extra.end(), r) != extra.end();
            if (known && !dup) {
                extra.push_back(r);
            }
        }
    }
    selection.objects = std::move(objects);
    selection.extraRegions = std::move(extra);
    return selection;
}

bool selection_state_is_consistent(const std::optional<ObjectId>& principal,
                                   const std::vector<ObjectId>& multi,
                                   bool regionOrEmbroiderySelected) {
    if (multi.empty()) {
        return true;
    }
    if (multi.size() < 2) {
        return false; // un seul objet = cas legacy (multi vide)
    }
    if (!principal || multi.back() != *principal) {
        return false; // principal = dernier
    }
    if (regionOrEmbroiderySelected) {
        return false; // région/broderie sélectionnée => multi vide
    }
    for (std::size_t i = 0; i < multi.size(); ++i) {
        if (std::find(multi.begin() + static_cast<std::ptrdiff_t>(i) + 1, multi.end(), multi[i]) !=
            multi.end()) {
            return false;
        }
    }
    return true;
}

} // namespace openstitch::desktop
