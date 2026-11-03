// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>

#include "openstitch/core/error.hpp"

// Primitive unique d'écriture de sortie (HP-QA-021) : fichier temporaire à nom unique dans le
// dossier de la cible, flush disque (FlushFileBuffers / fsync), puis renommage atomique qui
// remplace la cible. Un échec à n'importe quelle étape laisse l'ancien fichier intact et
// supprime le temporaire. Deux écritures simultanées vers la même cible produisent l'un ou
// l'autre contenu complet, jamais un mélange.
namespace openstitch {

[[nodiscard]] Result<void> write_file_atomic(const std::filesystem::path& target,
                                             std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<void> write_file_atomic(const std::filesystem::path& target,
                                             std::string_view text);

// Variante pour les écrivains qui ne produisent pas un tampon (ex. ZIP) : `writer` écrit le
// fichier au chemin temporaire reçu ; la primitive vide ensuite les tampons sur disque et
// renomme. Si `writer` échoue, son erreur est renvoyée et le temporaire supprimé.
using TempFileWriter = std::function<Result<void>(const std::filesystem::path& tmp)>;
[[nodiscard]] Result<void> write_file_atomic_with(const std::filesystem::path& target,
                                                  const TempFileWriter& writer);

} // namespace openstitch
