// SPDX-License-Identifier: Apache-2.0
#include "archive.hpp"

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <algorithm>
#include <cstring>

#include "openstitch/core/input_limits.hpp"

namespace openstitch::project_io::detail {

Result<void> write_zip(const std::filesystem::path& path,
                       const std::map<std::string, Blob>& entries) {
    void* writer = mz_zip_writer_create();
    if (writer == nullptr) {
        return fail(ErrorCategory::Internal, "Impossible d'initialiser l'écriture ZIP");
    }
    struct WriterGuard {
        void* w;
        ~WriterGuard() { mz_zip_writer_delete(&w); }
    } guard{writer};

    if (mz_zip_writer_open_file(writer, path_utf8(path).c_str(), 0, 0) != MZ_OK) {
        return fail(ErrorCategory::UserInput,
                    "Impossible d'écrire le fichier : " + path_utf8(path));
    }

    // Niveau rapide : les étiquettes de segmentation (4 octets par pixel, très répétitives) et
    // le JSON se compressent presque aussi bien qu'au niveau par défaut, en bien moins de temps
    // (mesures dans docs/performance-audit.md). Le format reste du DEFLATE standard.
    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_FAST);
    for (const auto& [name, blob] : entries) {
        mz_zip_file file_info;
        std::memset(&file_info, 0, sizeof(file_info));
        file_info.filename = name.c_str();
        file_info.flag = MZ_ZIP_FLAG_UTF8;
        // Un PNG est déjà compressé (DEFLATE) : le recompresser coûte du temps sans rien
        // gagner en taille. Entrée simplement stockée ; tout lecteur ZIP la relit telle quelle.
        const bool alreadyCompressed = name.size() >= 4 && name.ends_with(".png");
        file_info.compression_method =
            alreadyCompressed ? MZ_COMPRESS_METHOD_STORE : MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_AUTO;
        if (mz_zip_writer_add_buffer(writer, const_cast<std::uint8_t*>(blob.data()),
                                     static_cast<std::int32_t>(blob.size()), &file_info) != MZ_OK) {
            return fail(ErrorCategory::Internal, "Échec d'écriture de l'entrée « " + name + " »");
        }
    }
    if (mz_zip_writer_close(writer) != MZ_OK) {
        return fail(ErrorCategory::Internal, "Échec de finalisation du ZIP");
    }
    return {};
}

Result<std::map<std::string, Blob>> read_zip(const std::filesystem::path& path) {
    void* reader = mz_zip_reader_create();
    if (reader == nullptr) {
        return fail(ErrorCategory::Internal, "Impossible d'initialiser la lecture ZIP");
    }
    struct ReaderGuard {
        void* r;
        ~ReaderGuard() { mz_zip_reader_delete(&r); }
    } guard{reader};

    if (mz_zip_reader_open_file(reader, path_utf8(path).c_str()) != MZ_OK) {
        return fail(ErrorCategory::InvalidFile,
                    "Fichier projet illisible ou introuvable : " + path_utf8(path));
    }

    // Passe 1 (HP-QA-013) : valide TOUS les en-têtes (nombre, tailles annoncées, total, doublons)
    // avant de décompresser ou d'allouer quoi que ce soit.
    std::map<std::string, std::int64_t> declared;
    std::int64_t totalBytes = 0;
    std::int32_t err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        mz_zip_file* info = nullptr;
        if (mz_zip_reader_entry_get_info(reader, &info) != MZ_OK || info == nullptr) {
            return fail(ErrorCategory::InvalidFile, "Entrée ZIP corrompue");
        }
        if (declared.size() >= static_cast<std::size_t>(limits::kMaxZipEntries)) {
            return fail_coded(ErrorCategory::InvalidFile, limits::code::kZipTooManyEntries,
                              "Archive projet refusée : trop d'entrées (maximum " +
                                  std::to_string(limits::kMaxZipEntries) + ")",
                              "Le fichier n'est probablement pas un projet OpenStitch valide ; "
                              "ne l'ouvrez que s'il provient d'une source de confiance.");
        }
        const std::string name = info->filename;
        const std::int64_t size = info->uncompressed_size;
        if (size < 0) {
            return fail(ErrorCategory::InvalidFile, "Taille d'entrée ZIP invalide");
        }
        if (size > limits::kMaxZipEntryBytes) {
            return fail_coded(
                ErrorCategory::InvalidFile, limits::code::kZipEntryTooLarge,
                "Archive projet refusée : l'entrée « " + name + " » annonce " +
                    std::to_string(size / (1024 * 1024)) + " Mio décompressés (maximum " +
                    std::to_string(limits::kMaxZipEntryBytes / (1024 * 1024)) + " Mio)",
                "Archive corrompue ou malveillante (bombe de décompression) : "
                "restaurez une sauvegarde du projet.");
        }
        totalBytes += size;
        if (totalBytes > limits::kMaxZipTotalBytes) {
            return fail_coded(ErrorCategory::InvalidFile, limits::code::kZipTotalTooLarge,
                              "Archive projet refusée : taille décompressée totale supérieure à " +
                                  std::to_string(limits::kMaxZipTotalBytes / (1024 * 1024)) +
                                  " Mio",
                              "Archive corrompue ou malveillante (bombe de décompression) : "
                              "restaurez une sauvegarde du projet.");
        }
        if (!declared.emplace(name, size).second) {
            return fail_coded(ErrorCategory::InvalidFile, limits::code::kZipDuplicateEntry,
                              "Archive projet refusée : l'entrée « " + name +
                                  " » apparaît plusieurs fois",
                              "Le fichier a été modifié ou fabriqué à la main ; restaurez une "
                              "sauvegarde du projet.");
        }
        err = mz_zip_reader_goto_next_entry(reader);
    }
    if (err != MZ_END_OF_LIST && err != MZ_OK) {
        return fail(ErrorCategory::InvalidFile, "Archive projet corrompue");
    }

    // Passe 2 : lecture par blocs ; le tampon grandit avec les octets réellement décompressés, de
    // sorte qu'une taille annoncée mensongère ne provoque aucune allocation disproportionnée.
    std::map<std::string, Blob> entries;
    constexpr std::size_t kChunk = std::size_t{4} * 1024 * 1024;
    err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        mz_zip_file* info = nullptr;
        if (mz_zip_reader_entry_get_info(reader, &info) != MZ_OK || info == nullptr) {
            return fail(ErrorCategory::InvalidFile, "Entrée ZIP corrompue");
        }
        const std::string name = info->filename;
        const auto size = static_cast<std::size_t>(declared.at(name));
        Blob blob;
        if (mz_zip_reader_entry_open(reader) != MZ_OK) {
            return fail(ErrorCategory::InvalidFile,
                        "Impossible d'ouvrir l'entrée « " + name + " »");
        }
        while (blob.size() < size) {
            const std::size_t want = std::min(kChunk, size - blob.size());
            const std::size_t old = blob.size();
            blob.resize(old + want);
            const std::int32_t got = mz_zip_reader_entry_read(reader, blob.data() + old,
                                                              static_cast<std::int32_t>(want));
            if (got <= 0) {
                mz_zip_reader_entry_close(reader);
                return fail(ErrorCategory::InvalidFile, "Lecture incomplète de « " + name + " »");
            }
            blob.resize(old + static_cast<std::size_t>(got));
        }
        mz_zip_reader_entry_close(reader);
        entries.emplace(name, std::move(blob));
        err = mz_zip_reader_goto_next_entry(reader);
    }
    if (err != MZ_END_OF_LIST && err != MZ_OK) {
        return fail(ErrorCategory::InvalidFile, "Archive projet corrompue");
    }
    return entries;
}

} // namespace openstitch::project_io::detail
