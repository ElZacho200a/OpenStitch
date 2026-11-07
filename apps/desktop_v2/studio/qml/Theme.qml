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

    readonly property color background: light ? "#f3f4f7" : "#191b21"
    readonly property color surface: light ? "#e9ebf0" : "#242731"
    readonly property color panel: light ? "#ffffff" : "#20232b"
    readonly property color raised: light ? "#e4e7ee" : "#2d313d"
    readonly property color hover: light ? "#d9dde7" : "#303440"
    readonly property color border: light ? "#aeb4c2" : "#414653"
    readonly property color divider: light ? "#d3d7e0" : "#343844"
    readonly property color foreground: light ? "#1b1d24" : "#ececf2"
    readonly property color muted: light ? "#3f4554" : "#b6bbc8"
    readonly property color faint: light ? "#5d6475" : "#858d9e"
    readonly property color accent: light ? "#b02a66" : "#f08cb7"
    readonly property color accentSoft: light ? "#f6d9e6" : "#483141"
    readonly property color accentInk: light ? "#ffffff" : "#2a1721"
    readonly property color success: light ? "#2c6b4b" : "#94c6ac"

    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI" : "sans-serif"
    readonly property int captionSize: 10
    readonly property int smallSize: 11
    readonly property int bodySize: 12
    readonly property int headingSize: 14
    readonly property int railWidth: 52
    readonly property int inspectorWidth: 282
    readonly property int rowHeight: Math.round(38 * densityScale)
    readonly property int radius: 5
    readonly property int animationMs: reducedMotion ? 0 : 150
}
