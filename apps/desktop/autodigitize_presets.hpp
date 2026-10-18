// SPDX-License-Identifier: Apache-2.0
#pragma once

// HP-UX-030 : préréglages de la numérisation automatique (F8). Un préréglage n'est PAS un nouveau
// réglage du moteur : c'est un jeu de valeurs pour les champs EXISTANTS du dialogue (les mêmes
// qu'alimente `project_ops::DigitizeSettings`, donc la CLI `digitize`). Choisir un préréglage
// remplit ces champs ; les saisir à la main donne exactement les mêmes options (testé).
//
// Toutes les valeurs sont dans les unités de mémorisation du dialogue (µm, 1/100 mm², entiers) :
// ce que le dialogue relit et réécrit tel quel dans `ui_memory`.

namespace openstitch::desktop {

enum class DigitizePreset { Custom = 0, Logo = 1, LineArt = 2, Photo = 3 };

enum class DigitizeStrategy { Shapes = 0, Contours = 1, Stipple = 2 };

struct DigitizeDialogValues {
    bool satinBands{false};
    int maxThreads{0}; // palette : jamais modifiée par un préréglage
    DigitizeStrategy strategy{DigitizeStrategy::Shapes};

    // Formes pleines.
    int shapeDetail{50};
    int fillOverlapUm{0};
    int minFeatureWidthUm{0};
    int minRegionAreaCmm2{0};
    int islandAreaCmm2{0};
    int islandMaxDeltaE{0};
    int curveToleranceUm{0};
    bool solidBlock{false};
    bool flowFill{false};
    bool darkLines{false};
    int darkLinesWidthUm{1600};
    int darkLinesLightness{25};
    int darkLinesMinLenUm{0};
    bool darkLinesObjectsOnly{false};
    bool ringsSatin{false};

    // Contours (dessin au trait).
    int contourDetail{50};
    bool techniqueRunning{false};

    // Pointillé.
    int stipplePoints{2000};
    int stippleSeed{1};
    bool stippleInvert{false};

    bool operator==(const DigitizeDialogValues&) const = default;
};

// Valeurs d'un préréglage. `defaults` porte les seuils venant du moteur (largeur minimale des
// détails, aire minimale des régions) ; tout ce que le préréglage ne cite pas en est repris.
[[nodiscard]] inline DigitizeDialogValues
digitizePresetValues(DigitizePreset preset, const DigitizeDialogValues& defaults) {
    DigitizeDialogValues v = defaults;
    switch (preset) {
    case DigitizePreset::Custom:
        break;
    case DigitizePreset::Logo: // aplats nets : bandes en satin, contours lissés, léger recouvrement
        v.strategy = DigitizeStrategy::Shapes;
        v.satinBands = true;
        v.shapeDetail = 60;
        v.fillOverlapUm = 150;
        v.curveToleranceUm = 100;
        break;
    case DigitizePreset::LineArt: // traits cousus en point droit le long des lignes médianes
        v.strategy = DigitizeStrategy::Contours;
        v.contourDetail = 60;
        v.techniqueRunning = false;
        break;
    case DigitizePreset::Photo: // tons rendus par la densité de points
        v.strategy = DigitizeStrategy::Stipple;
        v.stipplePoints = 4000;
        v.stippleSeed = 1;
        v.stippleInvert = false;
        break;
    }
    return v;
}

// Égalité des options, palette exclue (un préréglage ne touche pas au nombre de fils).
[[nodiscard]] inline bool sameDigitizeOptions(DigitizeDialogValues a, DigitizeDialogValues b) {
    a.maxThreads = 0;
    b.maxThreads = 0;
    return a == b;
}

// Préréglage dont les valeurs sont exactement celles-ci, ou Custom.
[[nodiscard]] inline DigitizePreset matchingDigitizePreset(const DigitizeDialogValues& values,
                                                           const DigitizeDialogValues& defaults) {
    for (const auto preset :
         {DigitizePreset::Logo, DigitizePreset::LineArt, DigitizePreset::Photo}) {
        if (sameDigitizeOptions(values, digitizePresetValues(preset, defaults))) {
            return preset;
        }
    }
    return DigitizePreset::Custom;
}

} // namespace openstitch::desktop
