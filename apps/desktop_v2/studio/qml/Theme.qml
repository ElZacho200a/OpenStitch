// SPDX-License-Identifier: Apache-2.0
pragma Singleton
import QtQuick

// Façade QML du moteur de thèmes (docs/desktop-v2/22-moteur-de-themes.md). Les composants lisent
// `Theme.accent`, `Theme.radius`… comme avant ; les valeurs viennent de `tokens`, posé par
// Main.qml depuis `themeEngine` (ThemeController, thème résolu en C++). Sans moteur (tests QML
// isolés), chaque jeton retombe sur sa valeur Neo Studio sombre écrite ici ; `test_theme_engine`
// vérifie que ces replis égalent les défauts compilés (theme_schema.cpp).
// Changer de thème remplace `tokens` / `styles` / `icons` : les liaisons se réévaluent, aucun
// composant n'est recréé et rien de la broderie n'est recalculé.
QtObject {
    id: theme
    // `mode` ("dark" | "light") : apparence demandée (préférences) ; `densityScale` et
    // `reducedMotion` restent des préférences d'accessibilité, appliquées par-dessus le thème.
    property string mode: "dark"
    property real densityScale: 1.0
    property bool reducedMotion: false

    // Thème résolu : jetons « famille.nom », styles composant -> variante -> état -> propriété,
    // icônes nom -> {path} | {source}, méta (familles de polices réellement disponibles…).
    property var tokens: ({})
    property var styles: ({})
    property var icons: ({})
    property var meta: ({})
    readonly property string themeId: meta.id !== undefined ? meta.id : "neo-studio"
    readonly property bool light: meta.appearance !== undefined ? meta.appearance === "light"
                                                               : mode === "light"

    function tok(key, fallback) {
        const value = tokens[key]
        return value === undefined ? fallback : value
    }
    function num(key, fallback) {
        const value = tokens[key]
        return value === undefined ? fallback : value
    }
    function metaValue(key, fallback) {
        const value = meta[key]
        return value === undefined ? fallback : value
    }
    // Style fusionné d'un composant : {} si le thème ne le surcharge pas (le composant garde alors
    // son style intégré). États : normal, hover, pressed, selected, focus, disabled.
    function style(component, variant, state) {
        const c = styles[component]
        if (!c)
            return ({})
        const v = c[variant && variant.length > 0 ? variant : "default"] || c["default"]
        if (!v)
            return ({})
        return v[state && state.length > 0 ? state : "normal"] || v["normal"] || ({})
    }
    // État dominant d'un contrôle, dans l'ordre des priorités du moteur.
    function stateOf(enabled, pressed, selected, hovered) {
        return !enabled ? "disabled" : pressed ? "pressed" : selected ? "selected"
             : hovered ? "hover" : "normal"
    }
    function easing(name) {
        switch (name) {
        case "Linear": return Easing.Linear
        case "InQuad": return Easing.InQuad
        case "OutQuad": return Easing.OutQuad
        case "InOutQuad": return Easing.InOutQuad
        case "InCubic": return Easing.InCubic
        case "InOutCubic": return Easing.InOutCubic
        case "OutQuart": return Easing.OutQuart
        case "InOutQuart": return Easing.InOutQuart
        case "OutQuint": return Easing.OutQuint
        case "InOutQuint": return Easing.InOutQuint
        case "InSine": return Easing.InSine
        case "OutSine": return Easing.OutSine
        case "InOutSine": return Easing.InOutSine
        case "OutExpo": return Easing.OutExpo
        case "InOutExpo": return Easing.InOutExpo
        case "OutCirc": return Easing.OutCirc
        case "InOutCirc": return Easing.InOutCirc
        case "OutBack": return Easing.OutBack
        case "InOutBack": return Easing.InOutBack
        case "OutElastic": return Easing.OutElastic
        case "OutBounce": return Easing.OutBounce
        default: return Easing.OutCubic
        }
    }

    // --- Couleurs -------------------------------------------------------------------------
    // Thread swatches always use the document's own colors, never these interface colors.
    readonly property color background: tok("color.background", "#171b25")
    readonly property color surface: tok("color.surface", "#222936")
    readonly property color panel: tok("color.panel", "#1d2330")
    readonly property color raised: tok("color.raised", "#2c3545")
    readonly property color hover: tok("color.hover", "#374153")
    readonly property color border: tok("color.border", "#445069")
    // Interface-component outline (inputs, check boxes, sliders): >= 3:1 on background/surface/panel.
    readonly property color outline: tok("color.outline", "#6b7a94")
    readonly property color divider: tok("color.divider", "#313b4b")
    readonly property color foreground: tok("color.foreground", "#f1f4fa")
    readonly property color muted: tok("color.muted", "#bdc8d8")
    readonly property color faint: tok("color.faint", "#a3b2c6")
    readonly property color accent: tok("color.accent", "#ef91bb")
    readonly property color accentPressed: tok("color.accentPressed", "#dc79a6")
    readonly property color secondaryAccent: tok("color.secondaryAccent", "#b7a6f2")
    readonly property color info: tok("color.info", "#8ad4f0")
    readonly property color warning: tok("color.warning", "#ebc47c")
    // Cartes peintes de l'espace Segmentation (mêmes teintes que leurs incrustations C++).
    readonly property color zoneDetail: tok("color.zoneDetail", "#ff8c00")
    readonly property color zoneLinework: tok("color.zoneLinework", "#00beff")
    readonly property color error: tok("color.error", "#f1a0ad")
    readonly property color accentSoft: tok("color.accentSoft", "#4b344c")
    readonly property color accentInk: tok("color.accentInk", "#281a28")
    readonly property color success: tok("color.success", "#93d5b9")
    readonly property color focus: tok("color.focus", "#ef91bb")
    readonly property color gloss: tok("color.gloss", "#ffffff")
    // Couleurs proposées pour les guides du canevas (panneau Vue).
    readonly property color guideCyan: tok("color.guideCyan", "#00bcd4")
    readonly property color guideRed: tok("color.guideRed", "#e53935")
    readonly property color guideGreen: tok("color.guideGreen", "#43a047")
    readonly property color guideMagenta: tok("color.guideMagenta", "#d81b60")

    // --- Typographie ----------------------------------------------------------------------
    // Familles réellement installées, choisies par le moteur dans la liste du thème (repli
    // système si aucune n'est disponible).
    readonly property string fontFamily: metaValue("fontFamily", Qt.platform.os === "windows" ? "Segoe UI Variable" : "sans-serif")
    readonly property string headingFamily: metaValue("headingFamily", fontFamily)
    readonly property int captionSize: Math.round(num("font.captionSize", 11))
    readonly property int smallSize: Math.round(num("font.smallSize", 12))
    readonly property int bodySize: Math.round(num("font.bodySize", 13))
    readonly property int headingSize: Math.round(num("font.headingSize", 16))
    readonly property int brandSize: Math.round(num("font.brandSize", 20))
    readonly property int heroSize: Math.round(num("font.heroSize", 22))
    readonly property int bodyWeight: num("font.bodyWeight", 400)
    readonly property int strongWeight: num("font.strongWeight", 600)
    readonly property int headingWeight: num("font.headingWeight", 600)
    readonly property real letterSpacing: num("font.letterSpacing", 0)
    readonly property real sectionLetterSpacing: num("font.sectionLetterSpacing", 1)

    // --- Géométrie ------------------------------------------------------------------------
    // Shared visual rhythm; avoid local type scales and duplicated sizes.
    readonly property int panelPadding: num("space.panelPadding", 18)
    readonly property int railWidth: num("size.railWidth", 76)
    readonly property int inspectorWidth: num("size.inspectorWidth", 294)
    readonly property int controlHeight: Math.round(num("size.controlHeight", 38) * densityScale)
    readonly property int rowHeight: Math.round(num("size.rowHeight", 42) * densityScale)
    readonly property int radius: num("shape.radius", 8)
    // Cartes flottantes (StudioCard) : rayon, écart entre cartes et avec les bords, ombre portée.
    readonly property int cardRadius: num("shape.cardRadius", 14)
    readonly property int gap: num("space.gap", 10)
    readonly property real borderWidth: num("shape.borderWidth", 1)
    // Anneau de focus clavier : jamais sous 2 px (borne du schéma).
    readonly property real focusWidth: num("shape.focusWidth", 2)
    readonly property int iconSize: num("icon.size", 18)
    readonly property real iconStroke: num("icon.strokeWidth", 1.45)

    // --- Effets ---------------------------------------------------------------------------
    readonly property color shadow: tok("color.shadow", "#000000")
    readonly property real shadowOpacity: num("effect.shadowOpacity", 0.2)
    readonly property int shadowOffset: num("effect.shadowOffset", 4)
    readonly property int shadowLayers: num("effect.shadowLayers", 3)
    readonly property int shadowSpread: num("effect.shadowSpread", 3)
    // Plan de travail : semis de points discret derrière les cartes et le canevas, texture
    // facultative du thème (fichier local résolu par le moteur, jamais une URL distante).
    readonly property color deskDot: tok("color.deskDot", "#262d3b")
    readonly property int deskStep: num("effect.deskStep", 24)
    readonly property string deskTexture: metaValue("deskTexture", "")
    readonly property real deskTextureOpacity: num("effect.deskTextureOpacity", 0)

    // --- Mouvement ------------------------------------------------------------------------
    // Toutes les durées tombent à 0 avec « Réduire les animations » (préférence ou système).
    readonly property int animationMs: reducedMotion ? 0 : num("motion.durationMs", 150)
    readonly property int feedbackFastMs: reducedMotion ? 0 : num("motion.fastMs", 90)
    readonly property int feedbackSlowMs: reducedMotion ? 0 : num("motion.slowMs", 200)
    readonly property int easingType: easing(tok("motion.easing", "OutCubic"))
    readonly property int easingEmphasis: easing(tok("motion.easingEmphasis", "OutCubic"))
    readonly property real pressScale: reducedMotion ? 1 : num("motion.pressScale", 0.97)
    readonly property real hoverLift: reducedMotion ? 0 : num("motion.hoverLift", 0)
    readonly property real popupScale: reducedMotion ? 1 : num("motion.popupScale", 1)
}
