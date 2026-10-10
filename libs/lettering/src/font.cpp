// SPDX-License-Identifier: Apache-2.0
#include "openstitch/lettering/font.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

namespace openstitch::lettering {

struct Font::Impl {
    FT_Library library{nullptr};
    FT_Face face{nullptr};
    std::vector<std::uint8_t> bytes; // FT_New_Memory_Face ne copie pas
    double cap_units{0.0};

    ~Impl() {
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }
};

namespace {

struct Decomposer {
    std::vector<std::vector<FontPoint>>* loops{nullptr};
    FontPoint current{};
    double tolerance{1.0};
};

FontPoint to_point(const FT_Vector* v) {
    return {static_cast<double>(v->x), static_cast<double>(v->y)};
}

int move_to(const FT_Vector* to, void* user) {
    auto* d = static_cast<Decomposer*>(user);
    d->loops->emplace_back();
    d->current = to_point(to);
    d->loops->back().push_back(d->current);
    return 0;
}

int line_to(const FT_Vector* to, void* user) {
    auto* d = static_cast<Decomposer*>(user);
    d->current = to_point(to);
    d->loops->back().push_back(d->current);
    return 0;
}

int conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto* d = static_cast<Decomposer*>(user);
    const FontPoint p0 = d->current;
    const FontPoint c = to_point(control);
    const FontPoint p1 = to_point(to);
    // Écart max à la corde d'une quadratique : |p0 - 2c + p1| / 4 / n^2.
    const double dev = std::hypot(p0.x - 2.0 * c.x + p1.x, p0.y - 2.0 * c.y + p1.y) / 4.0;
    const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(dev / d->tolerance))), 1, 64);
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double u = 1.0 - t;
        d->loops->back().push_back({u * u * p0.x + 2.0 * u * t * c.x + t * t * p1.x,
                                    u * u * p0.y + 2.0 * u * t * c.y + t * t * p1.y});
    }
    d->current = p1;
    return 0;
}

int cubic_to(const FT_Vector* c1v, const FT_Vector* c2v, const FT_Vector* to, void* user) {
    auto* d = static_cast<Decomposer*>(user);
    const FontPoint p0 = d->current;
    const FontPoint c1 = to_point(c1v);
    const FontPoint c2 = to_point(c2v);
    const FontPoint p3 = to_point(to);
    const double m = std::max(std::hypot(p0.x - 2.0 * c1.x + c2.x, p0.y - 2.0 * c1.y + c2.y),
                              std::hypot(c1.x - 2.0 * c2.x + p3.x, c1.y - 2.0 * c2.y + p3.y));
    const int n =
        std::clamp(static_cast<int>(std::ceil(std::sqrt(0.75 * m / d->tolerance))), 1, 64);
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double u = 1.0 - t;
        const double b0 = u * u * u;
        const double b1 = 3.0 * u * u * t;
        const double b2 = 3.0 * u * t * t;
        const double b3 = t * t * t;
        d->loops->back().push_back({b0 * p0.x + b1 * c1.x + b2 * c2.x + b3 * p3.x,
                                    b0 * p0.y + b1 * c1.y + b2 * c2.y + b3 * p3.y});
    }
    d->current = p3;
    return 0;
}

} // namespace

std::vector<FontFaceInfo> inspect_font_file(const std::filesystem::path& path) {
    std::vector<FontFaceInfo> out;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        return out;
    }
    // FT_New_Face prend un chemin « char » (page de codes ANSI sous Windows) : sûr
    // seulement en ASCII pur, lecture paresseuse du fichier. Sinon on lit le fichier
    // par std::filesystem (chemin large) et on passe par la mémoire.
    const std::string file = path.string();
    const bool ascii = std::all_of(file.begin(), file.end(),
                                   [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    std::vector<std::uint8_t> bytes;
    if (!ascii) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            FT_Done_FreeType(library);
            return out;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto open = [&](long index, FT_Face* face) {
        return ascii ? FT_New_Face(library, file.c_str(), index, face)
                     : FT_New_Memory_Face(library, bytes.data(),
                                          static_cast<FT_Long>(bytes.size()), index, face);
    };
    // Index -1 : ne renvoie que le nombre de visages (lecture minimale du fichier).
    FT_Face probe = nullptr;
    long faces = 1;
    if (open(-1, &probe) == 0) {
        faces = std::clamp(probe->num_faces, 1L, 32L);
        FT_Done_Face(probe);
    } else {
        FT_Done_FreeType(library);
        return out;
    }
    for (long i = 0; i < faces; ++i) {
        FT_Face face = nullptr;
        if (open(i, &face) != 0) {
            continue;
        }
        if (face->family_name != nullptr && FT_IS_SCALABLE(face) &&
            FT_Select_Charmap(face, FT_ENCODING_UNICODE) == 0) {
            out.push_back({static_cast<int>(i), face->family_name,
                           face->style_name != nullptr ? face->style_name : ""});
        }
        FT_Done_Face(face);
    }
    FT_Done_FreeType(library);
    return out;
}

Font::Font(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;
Font::~Font() = default;

Result<Font> Font::from_memory(std::vector<std::uint8_t> bytes, int face_index) {
    if (bytes.empty()) {
        return fail(ErrorCategory::InvalidFile, "Fichier de police vide.");
    }
    auto impl = std::make_unique<Impl>();
    if (FT_Init_FreeType(&impl->library) != 0) {
        return fail(ErrorCategory::Internal, "Initialisation de FreeType impossible.");
    }
    impl->bytes = std::move(bytes);
    if (FT_New_Memory_Face(impl->library, impl->bytes.data(),
                           static_cast<FT_Long>(impl->bytes.size()), face_index,
                           &impl->face) != 0) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Police illisible : ce n'est pas un fichier TrueType/OpenType valide.");
    }
    if (FT_Select_Charmap(impl->face, FT_ENCODING_UNICODE) != 0) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Cette police n'a pas de table de caractères Unicode.");
    }
    // Hauteur de capitale mesurée sur « H » (boîte du contour, sans hinting).
    const double em = static_cast<double>(impl->face->units_per_EM);
    impl->cap_units = 0.7 * em;
    const FT_UInt h = FT_Get_Char_Index(impl->face, 'H');
    if (h != 0 && FT_Load_Glyph(impl->face, h, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0 &&
        impl->face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_BBox box{};
        FT_Outline_Get_CBox(&impl->face->glyph->outline, &box);
        if (box.yMax > 0) {
            impl->cap_units = static_cast<double>(box.yMax);
        }
    }
    return Font(std::move(impl));
}

Result<Font> Font::from_file(const std::filesystem::path& path, int face_index) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCategory::InvalidFile, "Fichier de police introuvable : " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    return from_memory(std::move(bytes), face_index);
}

std::string Font::family_name() const {
    const char* name = impl_->face->family_name;
    return name != nullptr ? std::string(name) : std::string{};
}

double Font::units_per_em() const {
    return static_cast<double>(impl_->face->units_per_EM);
}

double Font::cap_height_units() const {
    return impl_->cap_units;
}

bool Font::has_glyph(char32_t code_point) const {
    return FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(code_point)) != 0;
}

GlyphOutline Font::outline(char32_t code_point, double tolerance_units) const {
    GlyphOutline out;
    const FT_UInt index = FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(code_point));
    if (index == 0) {
        return out;
    }
    if (FT_Load_Glyph(impl_->face, index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0) {
        return out;
    }
    out.present = true;
    out.advance = static_cast<double>(impl_->face->glyph->advance.x);
    if (impl_->face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        return out;
    }
    FT_Outline_Funcs funcs{};
    funcs.move_to = move_to;
    funcs.line_to = line_to;
    funcs.conic_to = conic_to;
    funcs.cubic_to = cubic_to;
    Decomposer decomposer;
    decomposer.loops = &out.loops;
    decomposer.tolerance = std::max(tolerance_units, 1e-3);
    FT_Outline_Decompose(&impl_->face->glyph->outline, &funcs, &decomposer);
    // Retire le point de fermeture dupliqué (FreeType ramène explicitement au départ).
    for (auto& loop : out.loops) {
        if (loop.size() >= 2 && loop.front().x == loop.back().x &&
            loop.front().y == loop.back().y) {
            loop.pop_back();
        }
    }
    std::erase_if(out.loops, [](const auto& loop) { return loop.size() < 3; });
    return out;
}

double Font::kerning(char32_t left, char32_t right) const {
    if (!FT_HAS_KERNING(impl_->face)) {
        return 0.0;
    }
    const FT_UInt l = FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(left));
    const FT_UInt r = FT_Get_Char_Index(impl_->face, static_cast<FT_ULong>(right));
    if (l == 0 || r == 0) {
        return 0.0;
    }
    FT_Vector delta{};
    if (FT_Get_Kerning(impl_->face, l, r, FT_KERNING_UNSCALED, &delta) != 0) {
        return 0.0;
    }
    return static_cast<double>(delta.x);
}

} // namespace openstitch::lettering
