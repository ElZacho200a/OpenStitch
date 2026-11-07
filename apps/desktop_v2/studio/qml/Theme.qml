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

    readonly property color background: light ? "#f3f4f7" : "#171a20"
    readonly property color surface: light ? "#e9ebf0" : "#232833"
    readonly property color panel: light ? "#ffffff" : "#1e222a"
    readonly property color raised: light ? "#e4e7ee" : "#2c323d"
    readonly property color hover: light ? "#d9dde7" : "#353c49"
    readonly property color border: light ? "#aeb4c2" : "#3b4552"
    readonly property color divider: light ? "#d3d7e0" : "#303742"
    readonly property color foreground: light ? "#1b1d24" : "#f2f4f8"
    readonly property color muted: light ? "#3f4554" : "#b9c2d0"
    readonly property color faint: light ? "#5d6475" : "#98a5b7"
    readonly property color accent: light ? "#b02a66" : "#ee90b7"
    readonly property color accentPressed: light ? "#982354" : "#d977a3"
    readonly property color accentSoft: light ? "#f6d9e6" : "#443040"
    readonly property color accentInk: light ? "#ffffff" : "#2a1721"
    readonly property color success: light ? "#2c6b4b" : "#94c6ac"

    // One typographic scale for the entire Qt Quick shell and its panels.
    // Windows 11 uses the variable system face; Qt falls back to the system sans-serif.
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI Variable" : "sans-serif"
    readonly property int captionSize: 11
    readonly property int smallSize: 12
    readonly property int bodySize: 13
    readonly property int headingSize: 16
    readonly property int brandSize: 20
    readonly property int heroSize: 22
    readonly property int panelPadding: 18
    readonly property int railWidth: 56
    readonly property int inspectorWidth: 300
    readonly property int controlHeight: Math.round(38 * densityScale)
    readonly property int rowHeight: Math.round(42 * densityScale)
    readonly property int radius: 8
    readonly property int animationMs: reducedMotion ? 0 : 150
}
