// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/color_companion.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

namespace openstitch::formats {

namespace {

constexpr std::uint32_t kInfVersion = 1;
constexpr std::size_t kInfFixedRecord = 10; // longueur, réservé, RVB, 0, index

void put_u16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put_u16(out, v >> 16);
    put_u16(out, v & 0xFFFFu);
}

std::uint32_t get_u16(std::span<const std::uint8_t> b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 8) | b[at + 1];
}

std::uint32_t get_u32(std::span<const std::uint8_t> b, std::size_t at) {
    return (get_u16(b, at) << 16) | get_u16(b, at + 2);
}

std::vector<std::uint8_t> encode_inf(std::span<const ThreadColor> colors) {
    std::vector<std::uint8_t> out;
    put_u32(out, kInfVersion);
    put_u32(out, static_cast<std::uint32_t>(colors.size()));
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const std::string name = fmt::format("Color {}", i + 1);
        put_u16(out, static_cast<std::uint32_t>(kInfFixedRecord + name.size() + 1));
        put_u16(out, 0);
        out.insert(out.end(), colors[i].begin(), colors[i].end());
        out.push_back(0);
        put_u16(out, static_cast<std::uint32_t>(i + 1));
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0);
    }
    return out;
}

std::vector<std::uint8_t> encode_csv(std::span<const ThreadColor> colors) {
    std::string text = "index,r,g,b,hex\n";
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto& c = colors[i];
        text += fmt::format("{},{},{},{},#{:02X}{:02X}{:02X}\n", i + 1, c[0], c[1], c[2], c[0],
                            c[1], c[2]);
    }
    return {text.begin(), text.end()};
}

Result<std::vector<ThreadColor>> decode_inf(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 8 || get_u32(bytes, 0) != kInfVersion) {
        return fail(ErrorCategory::InvalidFile, "Compagnon .inf : en-tête invalide");
    }
    const std::uint32_t count = get_u32(bytes, 4);
    if (count == 0 || count > kMaxCompanionColors) {
        return fail(ErrorCategory::InvalidFile, "Compagnon .inf : nombre de fils invalide");
    }
    std::vector<ThreadColor> colors;
    colors.reserve(count);
    std::size_t at = 8;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at + kInfFixedRecord > bytes.size()) {
            return fail(ErrorCategory::InvalidFile, "Compagnon .inf : fichier tronqué");
        }
        const std::size_t length = get_u16(bytes, at);
        if (length < kInfFixedRecord || at + length > bytes.size()) {
            return fail(ErrorCategory::InvalidFile, "Compagnon .inf : longueur d'enregistrement");
        }
        colors.push_back({bytes[at + 4], bytes[at + 5], bytes[at + 6]});
        at += length;
    }
    return colors;
}

bool parse_channel(std::string_view field, std::uint8_t& out) {
    while (!field.empty() && (field.front() == ' ' || field.front() == '\t')) {
        field.remove_prefix(1);
    }
    while (!field.empty() && (field.back() == ' ' || field.back() == '\t')) {
        field.remove_suffix(1);
    }
    int v = 0;
    const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), v);
    if (ec != std::errc{} || ptr != field.data() + field.size() || v < 0 || v > 255) {
        return false;
    }
    out = static_cast<std::uint8_t>(v);
    return true;
}

Result<std::vector<ThreadColor>> decode_csv(std::span<const std::uint8_t> bytes) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::vector<ThreadColor> colors;
    bool header_seen = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        std::string_view fields[5];
        std::size_t n = 0;
        std::size_t start = 0;
        while (n < 5) {
            const std::size_t comma = line.find(',', start);
            if (comma == std::string_view::npos) {
                fields[n++] = line.substr(start);
                break;
            }
            fields[n++] = line.substr(start, comma - start);
            start = comma + 1;
        }
        if (!header_seen && n >= 4 && !fields[0].empty() && fields[0][0] == 'i') {
            header_seen = true; // ligne d'en-tête « index,r,g,b,hex »
            continue;
        }
        ThreadColor c{};
        if (n < 4 || !parse_channel(fields[1], c[0]) || !parse_channel(fields[2], c[1]) ||
            !parse_channel(fields[3], c[2])) {
            return fail(ErrorCategory::InvalidFile, "Compagnon .csv : ligne de couleur invalide");
        }
        header_seen = true;
        if (colors.size() >= kMaxCompanionColors) {
            return fail(ErrorCategory::InvalidFile, "Compagnon .csv : trop de fils");
        }
        colors.push_back(c);
    }
    if (colors.empty()) {
        return fail(ErrorCategory::InvalidFile, "Compagnon de couleurs : aucun fil");
    }
    return colors;
}

const char* extension_of(CompanionKind kind) {
    return kind == CompanionKind::Inf ? ".inf" : ".csv";
}

Result<std::vector<std::uint8_t>> read_all(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput,
                    "Fichier introuvable ou illisible : " + path.string());
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

} // namespace

std::filesystem::path companion_path(const std::filesystem::path& design_path, CompanionKind kind) {
    auto p = design_path;
    p.replace_extension(extension_of(kind));
    return p;
}

Result<std::vector<std::uint8_t>> encode_color_companion(std::span<const ThreadColor> colors,
                                                         CompanionKind kind) {
    if (colors.empty() || colors.size() > kMaxCompanionColors) {
        return fail(ErrorCategory::UserInput, "Compagnon de couleurs : nombre de fils invalide");
    }
    return kind == CompanionKind::Inf ? encode_inf(colors) : encode_csv(colors);
}

Result<std::vector<ThreadColor>> decode_color_companion(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return fail(ErrorCategory::InvalidFile, "Compagnon de couleurs vide");
    }
    // Le binaire commence par la version 32 bits gros-boutiste (octet 0 nul) ; le texte jamais.
    return bytes[0] == 0 ? decode_inf(bytes) : decode_csv(bytes);
}

Result<void> write_color_companion_file(const std::filesystem::path& design_path,
                                        std::span<const ThreadColor> colors, CompanionKind kind) {
    auto bytes = encode_color_companion(colors, kind);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const auto path = companion_path(design_path, kind);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCategory::UserInput, "Écriture impossible : " + path.string());
    }
    file.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
    if (!file) {
        return fail(ErrorCategory::UserInput, "Écriture impossible : " + path.string());
    }
    return {};
}

Result<std::optional<std::vector<ThreadColor>>>
read_color_companion_for(const std::filesystem::path& design_path) {
    for (const auto kind : {CompanionKind::Inf, CompanionKind::Csv}) {
        const auto path = companion_path(design_path, kind);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        auto bytes = read_all(path);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto colors = decode_color_companion(*bytes);
        if (!colors) {
            return std::unexpected(colors.error());
        }
        return std::optional<std::vector<ThreadColor>>(std::move(*colors));
    }
    return std::optional<std::vector<ThreadColor>>(std::nullopt);
}

} // namespace openstitch::formats
