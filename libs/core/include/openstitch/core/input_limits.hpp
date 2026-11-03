// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// Plafonds appliqués aux entrées hostiles (HP-QA-013) : au-delà, le fichier est refusé avec un
// code `E_*` et un conseil, avant toute allocation proportionnelle à une taille annoncée.
namespace openstitch::limits {

// Image : 256 millions de pixels (16384 x 16384) -> 1 Gio en RGBA 8 bits.
inline constexpr std::int64_t kMaxImagePixels = std::int64_t{256} * 1024 * 1024;
// Fichier image encodé : 512 Mio.
inline constexpr std::int64_t kMaxImageFileBytes = std::int64_t{512} * 1024 * 1024;
// Archive .osp : nombre d'entrées, taille décompressée par entrée et au total.
inline constexpr std::int32_t kMaxZipEntries = 1024;
inline constexpr std::int64_t kMaxZipEntryBytes = std::int64_t{1024} * 1024 * 1024;
inline constexpr std::int64_t kMaxZipTotalBytes = std::int64_t{2048} * 1024 * 1024;

namespace code {
inline constexpr const char* kImageTooManyPixels = "E_IMAGE_TOO_MANY_PIXELS";
inline constexpr const char* kImageFileTooLarge = "E_IMAGE_FILE_TOO_LARGE";
inline constexpr const char* kZipTooManyEntries = "E_ZIP_TOO_MANY_ENTRIES";
inline constexpr const char* kZipEntryTooLarge = "E_ZIP_ENTRY_TOO_LARGE";
inline constexpr const char* kZipTotalTooLarge = "E_ZIP_TOTAL_TOO_LARGE";
inline constexpr const char* kZipDuplicateEntry = "E_ZIP_DUPLICATE_ENTRY";
inline constexpr const char* kWriteFailed = "E_WRITE_FAILED";
inline constexpr const char* kWriteTargetBusy = "E_WRITE_TARGET_BUSY";
} // namespace code

} // namespace openstitch::limits
