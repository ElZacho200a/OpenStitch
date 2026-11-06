// SPDX-License-Identifier: Apache-2.0
pragma Singleton
import QtQuick

QtObject {
    // Atelier tokens shared with the Widgets design direction. Thread swatches
    // always use the document's own colors, never these interface colors.
    readonly property color background: "#191b21"
    readonly property color surface: "#242731"
    readonly property color panel: "#20232b"
    readonly property color raised: "#2d313d"
    readonly property color hover: "#303440"
    readonly property color border: "#414653"
    readonly property color divider: "#343844"
    readonly property color foreground: "#ececf2"
    readonly property color muted: "#b6bbc8"
    readonly property color faint: "#858d9e"
    readonly property color accent: "#f08cb7"
    readonly property color accentSoft: "#483141"
    readonly property color accentInk: "#2a1721"
    readonly property color success: "#94c6ac"

    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI" : "sans-serif"
    readonly property int captionSize: 10
    readonly property int smallSize: 11
    readonly property int bodySize: 12
    readonly property int headingSize: 14
    readonly property int railWidth: 52
    readonly property int inspectorWidth: 282
    readonly property int rowHeight: 38
    readonly property int radius: 5
}
