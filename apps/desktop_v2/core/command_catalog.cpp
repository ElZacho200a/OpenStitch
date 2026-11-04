// SPDX-License-Identifier: Apache-2.0
#include "command_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace openstitch::desktop_v2 {

namespace {

struct Accent {
    std::string_view utf8;
    char base;
};
// Lettres accentuées usuelles du français (minuscules et majuscules), sur 2 octets UTF-8.
constexpr Accent kAccents[] = {
    {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'},
    {"î", 'i'}, {"ï", 'i'}, {"ô", 'o'}, {"ö", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'},
    {"ç", 'c'}, {"À", 'a'}, {"Â", 'a'}, {"É", 'e'}, {"È", 'e'}, {"Ê", 'e'}, {"Ë", 'e'},
    {"Î", 'i'}, {"Ï", 'i'}, {"Ô", 'o'}, {"Ù", 'u'}, {"Û", 'u'}, {"Ü", 'u'}, {"Ç", 'c'},
};

std::vector<std::string> words(std::string_view query) {
    std::vector<std::string> out;
    std::istringstream in{normalizeForSearch(query)};
    for (std::string w; in >> w;) {
        out.push_back(std::move(w));
    }
    return out;
}

} // namespace

std::string normalizeForSearch(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        bool replaced = false;
        for (const auto& a : kAccents) {
            if (text.compare(i, a.utf8.size(), a.utf8) == 0) {
                out.push_back(a.base);
                i += a.utf8.size();
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
            ++i;
        }
    }
    return out;
}

std::vector<const CommandEntry*> filterCommands(const std::vector<CommandEntry>& entries,
                                                std::string_view query) {
    const auto terms = words(query);
    struct Scored {
        const CommandEntry* entry;
        int rank;
        std::size_t order;
    };
    std::vector<Scored> kept;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const std::string label = normalizeForSearch(e.label);
        const std::string haystack =
            label + ' ' + normalizeForSearch(e.group) + ' ' + normalizeForSearch(e.id);
        bool all = true;
        bool startsWithWord = false;
        bool inLabel = true;
        for (const auto& t : terms) {
            all = all && haystack.find(t) != std::string::npos;
            const auto pos = label.find(t);
            inLabel = inLabel && pos != std::string::npos;
            startsWithWord =
                startsWithWord || (pos != std::string::npos && (pos == 0 || label[pos - 1] == ' '));
        }
        if (!all) {
            continue;
        }
        int rank = terms.empty() ? 1 : (startsWithWord ? 0 : (inLabel ? 1 : 2));
        rank += e.enabled ? 0 : 10;
        kept.push_back({&e, rank, i});
    }
    std::stable_sort(kept.begin(), kept.end(), [](const Scored& a, const Scored& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.order < b.order;
    });
    std::vector<const CommandEntry*> out;
    out.reserve(kept.size());
    for (const auto& k : kept) {
        out.push_back(k.entry);
    }
    return out;
}

} // namespace openstitch::desktop_v2
