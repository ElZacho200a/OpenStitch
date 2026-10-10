// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/lettering/lettering.hpp"

namespace openstitch::lettering {

namespace {

using document::TextFill;

// Ordre = index des groupes dans `build_text_objects` (satin d'abord, comme la couture).
enum class Kind { Satin = 0, Tatami = 1, Contour = 2 };

// Petite forme pleine sans trou (point d'un i, tréma, point final) : ni squelette
// ni trait, cousue en tatami.
bool is_tiny_blob(const geometry::PathSet& set) {
    if (!set.holes.empty() || set.outer.nodes.empty()) {
        return false;
    }
    std::int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (const auto& n : set.outer.nodes) {
        x0 = std::min(x0, n.pos.x.value);
        x1 = std::max(x1, n.pos.x.value);
        y0 = std::min(y0, n.pos.y.value);
        y1 = std::max(y1, n.pos.y.value);
    }
    const double w = static_cast<double>(x1) - x0;
    const double h = static_cast<double>(y1) - y0;
    const double lo = std::min(w, h);
    const double hi = std::max(w, h);
    return hi <= 3'500.0 && lo >= 0.5 * static_cast<double>(document::kTextMinStrokeWidth.value) &&
           hi <= 2.0 * lo;
}

std::string mm_fr(double um) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", um / 1000.0);
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// Réglages « petites lettres » (HP-TXT-013) : sous kTextSmallLetterHeight, la
// sous-couche centrale n'est gardée que si le trait est assez large pour la
// porter, et la compensation de tirage est supprimée (elle élargirait le trait
// jusqu'à boucher les contre-formes).
document::AutoSatinParams make_satin(const document::TextObject& text, double stroke_um) {
    document::AutoSatinParams p;
    p.spacing = text.density;
    p.center_underlay = stroke_um >= 1'600.0;
    const bool small = text.cap_height < document::kTextSmallLetterHeight;
    p.pull_compensation = (small || stroke_um < 2'000.0) ? Micrometers{0} : Micrometers{150};
    return p;
}

document::TatamiParams make_tatami(const document::TextObject& text, double stroke_um) {
    document::TatamiParams p;
    p.angle = Angle{std::numbers::pi / 4.0};
    p.row_spacing = text.density;
    p.stitch_length = Micrometers{3'000};
    const bool small = text.cap_height < document::kTextSmallLetterHeight;
    p.inset = small ? Micrometers{0} : Micrometers{200};
    p.underlay_edge = stroke_um >= 3'000.0 && !small; // sous-couche de bord : gros traits
    return p;
}

document::RunningStitchParams make_contour(const document::TextObject& text) {
    document::RunningStitchParams p;
    p.stitch_length = text.cap_height < document::kTextSmallLetterHeight ? Micrometers{1'500}
                                                                         : Micrometers{2'500};
    return p;
}

// Vrai si l'auto-satin couvre la lettre (squelette exploitable : pas un anneau pur
// ni une forme compacte omise).
bool satin_covers(const std::vector<geometry::PathSet>& shapes, Micrometers spacing,
                  double min_coverage, std::string* why) {
    double area = 0.0;
    double covered = 0.0;
    for (const auto& set : shapes) {
        auto_satin::SkeletonSatinParameters params;
        params.spacing = spacing;
        params.measure_coverage = true;
        const auto result = auto_satin::generate_skeleton_satin(set, params);
        const double a = geometry::path_set_area_um2(set);
        area += a;
        if (!result || result->columns.empty() || !result->diagnostics.coverage_measured) {
            if (why != nullptr) {
                *why = result ? "aucune colonne" : result.error().message;
            }
            return false;
        }
        covered += a * std::clamp(result->diagnostics.coverage_ratio, 0.0, 1.0);
    }
    if (area <= 0.0) {
        return false;
    }
    if (covered / area < min_coverage) {
        if (why != nullptr) {
            *why = "couverture " + std::to_string(static_cast<int>(100.0 * covered / area)) + " %";
        }
        return false;
    }
    return true;
}

} // namespace

Result<TextBuild> build_text_objects(const Font& font, const document::TextObject& text,
                                     IdGenerator<ObjectId>& ids, const BuildOptions& options) {
    auto layout = layout_text(font, text);
    if (!layout) {
        return std::unexpected(layout.error());
    }
    TextBuild build;
    build.layout = std::move(*layout);
    build.warnings = build.layout.warnings;
    for (auto& w : check_text_size(text)) {
        build.warnings.push_back(std::move(w));
    }

    const double max_satin = static_cast<double>(text.max_satin_width.value);
    const double min_stroke = static_cast<double>(document::kTextMinStrokeWidth.value);
    // Lettres concernées par chaque repli, regroupées en UN avertissement chacun.
    std::string thin_letters;
    std::string wide_letters;
    std::string skeleton_letters;
    const auto note = [](std::string& list, const std::string& letter) {
        if (list.find(letter) == std::string::npos) {
            list += (list.empty() ? "" : " ") + letter;
        }
    };

    for (const PlacedGlyph& glyph : build.layout.glyphs) {
        const std::string letter = encode_utf8(glyph.code_point);

        // Un type de point PAR MORCEAU du glyphe : un « ä » est un corps (satin) et
        // deux points (tatami) -- le satin omet les formes compactes, jamais de zone
        // oubliée. Morceaux de même type regroupés en un seul objet.
        std::vector<geometry::PathSet> groups[3];
        for (const geometry::PathSet& set : glyph.shapes) {
            const std::vector<geometry::PathSet> one{set};
            const double stroke = mean_stroke_width_um(one);
            Kind kind = Kind::Tatami;
            switch (text.fill) {
            case TextFill::Contour:
                kind = Kind::Contour;
                break;
            case TextFill::Tatami:
                kind = Kind::Tatami;
                break;
            case TextFill::Satin:
            case TextFill::Auto:
                if (is_tiny_blob(set)) {
                    kind = Kind::Tatami; // point, tréma, point final : trop petit pour un squelette
                } else if (stroke < min_stroke) {
                    kind = Kind::Contour;
                    note(thin_letters, letter);
                } else if (stroke > max_satin) {
                    kind = Kind::Tatami;
                    if (text.fill == TextFill::Satin) {
                        note(wide_letters, letter);
                    }
                } else {
                    kind = Kind::Satin;
                    if (options.verify_satin &&
                        !satin_covers(one, text.density, options.min_satin_coverage, nullptr)) {
                        kind = Kind::Tatami;
                        note(skeleton_letters, letter);
                    }
                }
                break;
            }
            groups[static_cast<int>(kind)].push_back(set);
        }

        int emitted = 0;
        for (int k = 0; k < 3; ++k) {
            if (groups[k].empty()) {
                continue;
            }
            const double stroke = mean_stroke_width_um(groups[k]);
            document::VectorObject vector;
            vector.id = ids.next();
            vector.name =
                "Lettre " + letter + (emitted > 0 ? " (" + std::to_string(emitted + 1) + ")" : "");
            vector.rgb = text.rgb;
            vector.paths = std::move(groups[k]);
            vector.text_owner = text.id;

            document::EmbroideryObject emb;
            emb.id = ids.next();
            emb.name = vector.name;
            emb.source_vector = vector.id;
            emb.rgb = text.rgb;
            emb.intent = document::EmbroideryIntent::ForcedUserChoice;
            switch (static_cast<Kind>(k)) {
            case Kind::Satin:
                emb.params = make_satin(text, stroke);
                break;
            case Kind::Tatami:
                emb.params = make_tatami(text, stroke);
                break;
            case Kind::Contour:
                emb.params = make_contour(text);
                break;
            }
            build.vectors.push_back(std::move(vector));
            build.embroideries.push_back(std::move(emb));
            ++emitted;
        }
    }

    if (!thin_letters.empty()) {
        build.warnings.push_back(
            {"trait-trop-fin",
             "Traits de moins de " + mm_fr(min_stroke) +
                 " mm, trop fins pour du satin : cousus en contour (" + thin_letters +
                 "). Agrandissez le texte ou choisissez une police plus grasse."});
    }
    if (!wide_letters.empty()) {
        build.warnings.push_back(
            {"lettre-trop-large", "Traits de plus de " + mm_fr(max_satin) +
                                      " mm, trop larges pour du satin : cousus en tatami (" +
                                      wide_letters + ")."});
    }
    if (!skeleton_letters.empty()) {
        build.warnings.push_back(
            {"satin-impossible",
             "L'auto-satin ne couvre pas ces lettres (boucles ou formes trop complexes) : "
             "cousues en tatami (" +
                 skeleton_letters + ")."});
    }
    return build;
}

std::optional<Vec2um> text_displacement(const document::Project& project,
                                        const document::TextObject& text, const Font& font) {
    const auto layout = layout_text(font, text);
    if (!layout) {
        return std::nullopt;
    }
    struct Min {
        bool any{false};
        std::int32_t x{0};
        std::int32_t y{0};
        void add(const geometry::PathSet& set) {
            for (const auto& n : set.outer.nodes) {
                if (!any) {
                    x = n.pos.x.value;
                    y = n.pos.y.value;
                    any = true;
                } else {
                    x = std::min(x, n.pos.x.value);
                    y = std::min(y, n.pos.y.value);
                }
            }
        }
    };
    Min expected;
    for (const PlacedGlyph& glyph : layout->glyphs) {
        for (const auto& set : glyph.shapes) {
            expected.add(set);
        }
    }
    Min actual;
    for (const document::VectorObject& vector : project.vector_objects) {
        if (vector.text_owner && *vector.text_owner == text.id) {
            for (const auto& set : vector.paths) {
                actual.add(set);
            }
        }
    }
    if (!expected.any || !actual.any) {
        return std::nullopt;
    }
    return Vec2um{Micrometers{actual.x - expected.x}, Micrometers{actual.y - expected.y}};
}

} // namespace openstitch::lettering
