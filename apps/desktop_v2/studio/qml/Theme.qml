// SPDX-License-Identifier: Apache-2.0
pragma Singleton
import QtQuick

QtObject {
    // Atelier tokens shared with the Widgets design direction. Thread swatches
    // always use the document's own colors, never these interface colors.
    // `mode` ("dark" | "light"), `densityScale` and `reducedMotion` are driven by the
    // preferences (Main.qml binds them to `prefs`); the defaults match the historical dark look.
    property string mode: "dark"
    property real densityScale: 1.0
    property bool reducedMotion: false
    readonly property bool light: mode === "light"

    readonly property color background: light ? "#f3f4f7" : "#171b25"
    readonly property color surface: light ? "#e9ebf0" : "#222936"
    readonly property color panel: light ? "#ffffff" : "#1d2330"
    readonly property color raised: light ? "#e4e7ee" : "#2c3545"
    readonly property color hover: light ? "#d9dde7" : "#374153"
    readonly property color border: light ? "#aeb4c2" : "#445069"
    // Interface-component outline (inputs, check boxes, sliders): >= 3:1 on background/surface/panel.
    readonly property color outline: light ? "#7a8194" : "#6b7a94"
    readonly property color divider: light ? "#d3d7e0" : "#313b4b"
    readonly property color foreground: light ? "#1b1d24" : "#f1f4fa"
    readonly property color muted: light ? "#3f4554" : "#bdc8d8"
    readonly property color faint: light ? "#565d6e" : "#a3b2c6"
    readonly property color accent: light ? "#b02a66" : "#ef91bb"
    readonly property color accentPressed: light ? "#9e245c" : "#dc79a6"
    readonly property color secondaryAccent: light ? "#6446b0" : "#b7a6f2"
    readonly property color info: light ? "#0f6585" : "#8ad4f0"
    readonly property color warning: light ? "#83531a" : "#ebc47c"
    readonly property color error: light ? "#ac3649" : "#f1a0ad"
    readonly property color accentSoft: light ? "#f6d9e6" : "#4b344c"
    readonly property color accentInk: light ? "#ffffff" : "#281a28"
    readonly property color success: light ? "#2c6b4b" : "#93d5b9"

    // Shared visual rhythm; avoid local type scales and duplicated sizes.
    // Qt falls back to the system sans-serif if the variable face is unavailable.
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI Variable" : "sans-serif"
    readonly property int captionSize: 11
    readonly property int smallSize: 12
    readonly property int bodySize: 13
    readonly property int headingSize: 16
    readonly property int brandSize: 20
    readonly property int heroSize: 22
    readonly property int panelPadding: 18
    readonly property int railWidth: 56
    readonly property int inspectorWidth: 294
    readonly property int controlHeight: Math.round(38 * densityScale)
    readonly property int rowHeight: Math.round(42 * densityScale)
    readonly property int radius: 8
    readonly property int animationMs: reducedMotion ? 0 : 150
}
