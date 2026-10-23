// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openstitch::mcp {

// Octets d'un document du guide, générés à la configuration depuis le skill
// `openstitch-digitize` (voir libs/mcp/CMakeLists.txt). `data` est terminé par un octet nul.
struct GuideBlob {
    const char* name;
    const unsigned char* data;
    std::size_t size;
};
extern const GuideBlob kGuideBlobs[];
extern const std::size_t kGuideBlobCount;

struct GuideTopic {
    std::string name;        // « workflow », « parametres », « mcp »…
    std::string title;       // premier titre `# …` du document
    std::string description; // une ligne pour l'index
    std::string text;        // Markdown (sans front matter YAML)
};

// Sujets du guide : « workflow » (SKILL.md), « mcp » (correspondance CLI -> outils MCP, rédigée
// ici), puis les références du skill.
[[nodiscard]] const std::vector<GuideTopic>& guide_topics();
[[nodiscard]] const GuideTopic* find_topic(std::string_view name);

// Sommaire lisible (sujets, tailles, conseils de lecture).
[[nodiscard]] std::string guide_index_text();

} // namespace openstitch::mcp
