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

// Écrit toutes les entrées dans un ZIP (compression deflate).
[[nodiscard]] Result<void> write_zip(const std::filesystem::path& path,
                                     const std::map<std::string, Blob>& entries);

// Lit toutes les entrées d'un ZIP.
[[nodiscard]] Result<std::map<std::string, Blob>> read_zip(const std::filesystem::path& path);

}  // namespace openstitch::project_io::detail
