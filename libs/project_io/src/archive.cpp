// SPDX-License-Identifier: Apache-2.0
#include "archive.hpp"

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <cstring>

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

    if (mz_zip_writer_open_file(writer, path.string().c_str(), 0, 0) != MZ_OK) {
        return fail(ErrorCategory::UserInput, "Impossible d'écrire le fichier : " + path.string());
    }

    for (const auto& [name, blob] : entries) {
        mz_zip_file file_info;
        std::memset(&file_info, 0, sizeof(file_info));
        file_info.filename = name.c_str();
        file_info.flag = MZ_ZIP_FLAG_UTF8;
        file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_AUTO;
        if (mz_zip_writer_add_buffer(writer, const_cast<std::uint8_t*>(blob.data()),
                                     static_cast<std::int32_t>(blob.size()),
                                     &file_info) != MZ_OK) {
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

    if (mz_zip_reader_open_file(reader, path.string().c_str()) != MZ_OK) {
        return fail(ErrorCategory::InvalidFile,
                    "Fichier projet illisible ou introuvable : " + path.string());
    }

    std::map<std::string, Blob> entries;
    std::int32_t err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        mz_zip_file* info = nullptr;
        if (mz_zip_reader_entry_get_info(reader, &info) != MZ_OK || info == nullptr) {
            return fail(ErrorCategory::InvalidFile, "Entrée ZIP corrompue");
        }
        const std::string name = info->filename;
        const std::int64_t size = info->uncompressed_size;
        if (size < 0) {
            return fail(ErrorCategory::InvalidFile, "Taille d'entrée ZIP invalide");
        }
        Blob blob(static_cast<std::size_t>(size));
        if (mz_zip_reader_entry_open(reader) != MZ_OK) {
            return fail(ErrorCategory::InvalidFile, "Impossible d'ouvrir l'entrée « " + name + " »");
        }
        if (size > 0) {
            const std::int32_t read = mz_zip_reader_entry_read(
                reader, blob.data(), static_cast<std::int32_t>(size));
            if (read != static_cast<std::int32_t>(size)) {
                mz_zip_reader_entry_close(reader);
                return fail(ErrorCategory::InvalidFile, "Lecture incomplète de « " + name + " »");
            }
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

}  // namespace openstitch::project_io::detail
