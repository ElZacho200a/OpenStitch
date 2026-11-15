// SPDX-License-Identifier: Apache-2.0
#include "openstitch/document/stitch_recipes.hpp"

#include <algorithm>
#include <numbers>

namespace openstitch::document {

namespace {

constexpr Angle kQuarterTurn{std::numbers::pi / 4.0};

RunningStitchParams running(RunningStyle style, int length, int repeats = 1) {
    RunningStitchParams p;
    p.style = style;
    p.stitch_length = Micrometers{length};
    p.repeats = repeats;
    return p;
}

CrossStitchParams cross(int cell, bool full, Angle angle = Angle{0.0}) {
    CrossStitchParams p;
    p.cell = Micrometers{cell};
    p.full_cross = full;
    p.grid_angle = angle;
    return p;
}

std::vector<StitchRecipe> build() {
    constexpr auto F = RecipeFidelity::Faithful;
    constexpr auto A = RecipeFidelity::Approximation;
    constexpr auto Outline = RecipeTarget::Outline;
    constexpr auto Fill = RecipeTarget::Fill;
    std::vector<StitchRecipe> out;
    const auto add = [&out](std::string_view id, std::string_view fr, std::string_view en,
                            RecipeTarget target, RecipeFidelity fidelity, std::string_view url,
                            std::string_view note, StitchParams params) {
        out.push_back({id, 1, fr, en, target, fidelity, url, note, std::move(params)});
    };

    // --- Contours ------------------------------------------------------------------------------
    add("running-stitch", "Point avant", "Running stitch", Outline, F,
        "https://crosstela.com/fr/learn/stitches/running-stitch/",
        "Sur machine, les points se touchent : pas d'intervalle visible entre deux points.",
        running(RunningStyle::Run, 2'500));
    add("backstitch", "Point arrière", "Backstitch", Outline, F,
        "https://crosstela.com/fr/learn/stitches/backstitch/", "Ligne fine et continue.",
        running(RunningStyle::Backstitch, 2'000));
    add("stem-stitch", "Point de tige", "Stem stitch", Outline, F,
        "https://crosstela.com/fr/learn/stitches/stem-stitch/",
        "Pénétrations décalées : aspect torsadé, idéal pour les tiges et les courbes.", [] {
            auto p = running(RunningStyle::Stem, 2'500);
            p.stem_offset = Micrometers{350};
            return p;
        }());
    add("holbein-stitch", "Point de Holbein", "Holbein stitch", Outline, F,
        "https://crosstela.com/fr/learn/stitches/holbein-stitch/",
        "Double point avant : un aller, un retour dans les mêmes trous.",
        running(RunningStyle::Run, 2'500, 2));
    add("split-stitch", "Point fendu", "Split stitch", Outline, A,
        "https://crosstela.com/fr/learn/stitches/split-stitch/",
        "Points courts qui reviennent sur le précédent ; la machine ne fend pas le fil.",
        running(RunningStyle::Backstitch, 1'500));
    add("blanket-stitch", "Point de feston", "Blanket stitch", Outline, F,
        "https://crosstela.com/fr/learn/stitches/blanket-stitch/",
        "Silhouette en dents ; la boucle de la main est remplacée par un point droit.", [] {
            BlanketStitchParams p;
            p.width = Micrometers{3'000};
            p.spacing = Micrometers{2'500};
            return p;
        }());
    add("zigzag-row", "Rangée en zigzag", "Zigzag row", Outline, F,
        "https://crosstela.com/fr/learn/stitches/zigzag-row/", "Zigzag régulier le long du tracé.",
        [] {
            MotifRunParams p;
            p.motif = MotifId::Zigzag;
            p.size = Micrometers{2'500};
            p.spacing = Micrometers{3'000};
            return p;
        }());

    // --- Remplissages --------------------------------------------------------------------------
    // Pas de « passé plat » ici : un satin sur une forme exige la planification et la conversion
    // de groupe de l'opération `auto_satin`, pas une simple affectation de paramètres.
    add("long-and-short-stitch", "Passé empiétant", "Long and short stitch", Fill, F,
        "https://crosstela.com/fr/learn/stitches/long-and-short-stitch/",
        "Longueurs irrégulières et pénétrations imbriquées ; ajouter des guides pour orienter le "
        "fil.",
        [] {
            DirectionalFillParams p;
            p.handmade = true;
            p.handmade_intensity = 60;
            p.stagger = 3;
            p.stitch_length = Micrometers{4'000};
            return p;
        }());
    add("laid-work", "Broderie posée", "Laid work", Fill, F,
        "https://crosstela.com/fr/learn/stitches/laid-work/",
        "Longs points parallèles ; au-delà de 8 mm de large, préférer le passé empiétant.", [] {
            TatamiParams p;
            p.stitch_length = Micrometers{7'000};
            p.row_spacing = Micrometers{450};
            return p;
        }());
    add("darning-stitch", "Point de reprise", "Darning stitch", Fill, F,
        "https://crosstela.com/fr/learn/stitches/darning-stitch/",
        "Rangées espacées laissant voir le tissu.", [] {
            TatamiParams p;
            p.row_spacing = Micrometers{1'500};
            p.stitch_length = Micrometers{4'000};
            p.stagger = 2;
            return p;
        }());
    add("cross-stitch", "Point de croix", "Cross stitch", Fill, F,
        "https://crosstela.com/fr/learn/stitches/cross-stitch/", "Croix complètes sur une grille.",
        cross(2'500, true));
    add("half-cross-stitch", "Demi-point de croix", "Half cross stitch", Fill, F,
        "https://crosstela.com/fr/learn/stitches/half-cross-stitch/", "Diagonales d'un seul sens.",
        cross(2'500, false));
    add("tent-stitch", "Petit point", "Tent stitch", Fill, F,
        "https://crosstela.com/fr/learn/stitches/tent-stitch/",
        "Demi-croix fine, comme sur canevas.", cross(1'500, false));
    add("upright-cross", "Croix droite", "Upright cross", Fill, F,
        "https://crosstela.com/fr/learn/stitches/upright-cross/", "Croix en « + » sur la grille.",
        cross(2'500, true, kQuarterTurn));
    add("diamond-lattice", "Treillis de losanges", "Diamond lattice", Fill, F,
        "https://crosstela.com/fr/learn/stitches/diamond-lattice/",
        "Deux couches de lignes à 45° ; sans attache aux croisements.", [] {
            CrosshatchFillParams p;
            p.layers = 2;
            p.angle = kQuarterTurn;
            p.row_spacing = Micrometers{3'000};
            return p;
        }());
    return out;
}

} // namespace

const std::vector<StitchRecipe>& stitch_recipes() {
    static const std::vector<StitchRecipe> recipes = build();
    return recipes;
}

const StitchRecipe* find_stitch_recipe(std::string_view id) {
    const auto& all = stitch_recipes();
    const auto it = std::ranges::find(all, id, &StitchRecipe::id);
    return it == all.end() ? nullptr : &*it;
}

std::string_view recipe_target_id(RecipeTarget target) {
    return target == RecipeTarget::Outline ? "outline" : "fill";
}

std::string_view recipe_fidelity_id(RecipeFidelity fidelity) {
    return fidelity == RecipeFidelity::Faithful ? "faithful" : "approximation";
}

} // namespace openstitch::document
