// SPDX-License-Identifier: Apache-2.0
#include "selection_model.hpp"

#include <algorithm>

namespace openstitch::desktop_v2 {

namespace {
// Dernière occurrence conservée, ordre relatif préservé.
std::vector<Target> normalized(std::vector<Target> in) {
    std::vector<Target> out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const bool laterDuplicate =
            std::find(in.begin() + static_cast<std::ptrdiff_t>(i) + 1, in.end(), in[i]) != in.end();
        if (!laterDuplicate && in[i].id != 0) {
            out.push_back(in[i]);
        }
    }
    return out;
}
} // namespace

void SelectionModel::commit(std::vector<Target> next) {
    next = normalized(std::move(next));
    if (next == targets_) {
        return;
    }
    targets_ = std::move(next);
    ++revision_;
    // Notification ré-entrante : un écouteur qui modifie la sélection ne déclenche pas de cascade ;
    // la diffusion est rejouée tant que l'état a bougé (borne : 8 passes), si bien que chaque
    // écouteur finit sur l'état final.
    if (notifying_) {
        return;
    }
    notifying_ = true;
    for (int pass = 0; pass < 8; ++pass) {
        const auto seen = revision_;
        const auto snapshot = listeners_; // un écouteur peut se désabonner pendant l'appel
        for (const auto& [token, listener] : snapshot) {
            (void)token;
            listener(*this);
        }
        if (seen == revision_) {
            break;
        }
    }
    notifying_ = false;
}

void SelectionModel::replace(std::vector<Target> targets) {
    commit(std::move(targets));
}

void SelectionModel::add(Target target) {
    auto next = targets_;
    next.push_back(target);
    commit(std::move(next));
}

void SelectionModel::toggle(Target target) {
    auto next = targets_;
    if (const auto it = std::find(next.begin(), next.end(), target); it != next.end()) {
        next.erase(it);
    } else {
        next.push_back(target);
    }
    commit(std::move(next));
}

void SelectionModel::clear() {
    commit({});
}

void SelectionModel::prune(const std::function<bool(const Target&)>& exists) {
    auto next = targets_;
    std::erase_if(next, [&](const Target& t) { return !exists(t); });
    commit(std::move(next));
}

std::optional<Target> SelectionModel::primary() const {
    if (targets_.empty()) {
        return std::nullopt;
    }
    return targets_.back();
}

bool SelectionModel::contains(Target target) const {
    return std::find(targets_.begin(), targets_.end(), target) != targets_.end();
}

SelectionModel::Token SelectionModel::subscribe(Listener listener) {
    const Token token = nextToken_++;
    listeners_.emplace_back(token, std::move(listener));
    return token;
}

void SelectionModel::unsubscribe(Token token) {
    std::erase_if(listeners_, [token](const auto& entry) { return entry.first == token; });
}

} // namespace openstitch::desktop_v2
