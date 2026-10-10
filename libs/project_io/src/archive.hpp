// SPDX-License-Identifier: Apache-2.0
// En-tête interne : lecture/écriture d'entrées nommées dans une archive ZIP.
// Encapsule minizip-ng (aucun type minizip dans l'API).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::project_io::detail {

using Blob = std::vector<std::uint8_t>;

// Chemin -> texte UTF-8 (path::string() passerait par la page de code ANSI
// sous Windows et corromprait les chemins accentués dans les messages).
inline std::string path_utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Écrit toutes les entrées dans un ZIP (compression deflate).
[[nodiscard]] Result<void> write_zip(const std::filesystem::path& path,
                                     const std::map<std::string, Blob>& entries);

// Lit toutes les entrées d'un ZIP.
[[nodiscard]] Result<std::map<std::string, Blob>> read_zip(const std::filesystem::path& path);

} // namespace openstitch::project_io::detail
