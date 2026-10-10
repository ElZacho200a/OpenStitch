// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::lettering {

// Point en unités de police (double, Y vers le haut). Aucun type FreeType ne
// sort de cette bibliothèque (même règle d'encapsulation que Clipper2).
struct FontPoint {
    double x{0.0};
    double y{0.0};
};

// Contour d'un glyphe : polylignes FERMÉES en unités de police (les Béziers
// quadratiques TrueType et cubiques CFF sont aplaties). L'orientation d'origine
// est conservée : la règle de remplissage « non nul » distingue extérieur et
// trous (cf. geometry::union_nonzero).
struct GlyphOutline {
    bool present{false};                       // le glyphe existe dans la police
    double advance{0.0};                       // chasse horizontale (unités de police)
    std::vector<std::vector<FontPoint>> loops; // vide pour un espace
};

// Visage d'un fichier de police, lu SANS charger tout le fichier (catalogue des
// polices installées, desktop) ; un fichier .ttc en porte plusieurs.
struct FontFaceInfo {
    int face_index{0};
    std::string family;
    std::string style; // « Regular », « Bold »...
};

// Visages d'un fichier de police ; liste vide si le fichier n'est pas une police
// lisible (jamais d'exception).
[[nodiscard]] std::vector<FontFaceInfo> inspect_font_file(const std::filesystem::path& path);

// Police TrueType / OpenType chargée en mémoire (HP-TXT-003). Non copiable ;
// NON thread-safe (un FT_Face par instance) : une instance par thread.
class Font {
public:
    // Les octets sont copiés dans la police (le tampon appelant peut disparaître).
    [[nodiscard]] static Result<Font> from_memory(std::vector<std::uint8_t> bytes,
                                                  int face_index = 0);
    [[nodiscard]] static Result<Font> from_file(const std::filesystem::path& path,
                                                int face_index = 0);

    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    ~Font();

    [[nodiscard]] std::string family_name() const;
    [[nodiscard]] double units_per_em() const;
    // Hauteur de capitale mesurée sur le glyphe « H » (repli : 70 % de l'em).
    [[nodiscard]] double cap_height_units() const;
    [[nodiscard]] bool has_glyph(char32_t code_point) const;

    // Contour du glyphe, aplati avec une tolérance (en unités de police).
    [[nodiscard]] GlyphOutline outline(char32_t code_point, double tolerance_units) const;

    // Crénage de la paire (table `kern` ; GPOS non lu, cf. docs/source/lettering.md),
    // en unités de police ; 0 si la police n'en porte pas.
    [[nodiscard]] double kerning(char32_t left, char32_t right) const;

private:
    struct Impl;
    explicit Font(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace openstitch::lettering
