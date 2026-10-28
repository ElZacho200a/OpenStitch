// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include "openstitch/agent/agent.hpp"

namespace openstitch::agent {

std::optional<std::string> MemorySecretStore::read(const std::string& key) {
    for (const auto& [k, v] : items_) {
        if (k == key) {
            return v;
        }
    }
    return std::nullopt;
}

bool MemorySecretStore::write(const std::string& key, const std::string& secret) {
    for (auto& [k, v] : items_) {
        if (k == key) {
            v = secret;
            return true;
        }
    }
    items_.emplace_back(key, secret);
    return true;
}

bool MemorySecretStore::remove(const std::string& key) {
    const auto it = std::remove_if(items_.begin(), items_.end(),
                                   [&](const auto& item) { return item.first == key; });
    const bool found = it != items_.end();
    items_.erase(it, items_.end());
    return found;
}

std::string redact(std::string text, const std::vector<std::string>& secrets) {
    for (const auto& secret : secrets) {
        if (secret.size() < 8) {
            continue; // trop court pour être un vrai secret : éviterait de masquer du texte courant
        }
        for (const std::string& needle : {secret, secret.substr(0, 8)}) {
            std::size_t pos = 0;
            while ((pos = text.find(needle, pos)) != std::string::npos) {
                text.replace(pos, needle.size(), "***");
                pos += 3;
            }
        }
    }
    return text;
}

} // namespace openstitch::agent
