// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::formats {

// HP-FMT-009 / HP-FMT-005 : fichier COMPAGNON de couleurs, écrit à côté d'un DST (qui n'emporte que
// des arrêts couleur) ou d'un EXP (aucune couleur). Il liste les fils dans l'ordre des blocs de
// couleur, un par bloc. Deux représentations, mêmes données (RVB 8 bits) :
//  - `Inf` : binaire gros-boutiste (cf. docs/source/formats-pes-jef-exp.md, « Fichier compagnon »),
//    extension `.inf` ; la structure retenue N'EST PAS validée par un lecteur tiers ;
//  - `Csv` : texte lisible `index,r,g,b,hex`, extension `.csv`, ouvrable dans n'importe quel
//    tableur ou éditeur de texte.
// Le décodeur reconnaît tout seul la représentation, ne plante jamais et renvoie une erreur sur
// un contenu invalide.
using ThreadColor = std::array<std::uint8_t, 3>;

enum class CompanionKind : std::uint8_t { Inf, Csv };

// Nombre maximal de fils d'un compagnon (borne contre les fichiers corrompus).
inline constexpr std::size_t kMaxCompanionColors = 4096;

// Chemin du compagnon d'un fichier de motif : même dossier, même nom, extension remplacée.
[[nodiscard]] std::filesystem::path companion_path(const std::filesystem::path& design_path,
                                                   CompanionKind kind);

// Déterministe : mêmes couleurs -> mêmes octets. Erreur si la liste est vide ou dépasse
// `kMaxCompanionColors`.
[[nodiscard]] Result<std::vector<std::uint8_t>>
encode_color_companion(std::span<const ThreadColor> colors, CompanionKind kind);
[[nodiscard]] Result<std::vector<ThreadColor>>
decode_color_companion(std::span<const std::uint8_t> bytes);

[[nodiscard]] Result<void> write_color_companion_file(const std::filesystem::path& design_path,
                                                      std::span<const ThreadColor> colors,
                                                      CompanionKind kind);
// Cherche le compagnon à côté de `design_path` (`.inf` d'abord, puis `.csv`). Aucun fichier
// présent -> `std::nullopt` (ce n'est pas une erreur) ; fichier présent mais illisible/invalide ->
// erreur.
[[nodiscard]] Result<std::optional<std::vector<ThreadColor>>>
read_color_companion_for(const std::filesystem::path& design_path);

} // namespace openstitch::formats
