// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Shapes
import "."

Item {
    id: icon
    property string name: ""
    property color color: Theme.muted
    implicitWidth: 20
    implicitHeight: 20

    function pathForName() {
        switch (name) {
        case "select": return "M4 3 L15 11 L10 12.5 L8 17 Z M10 12.5 L14 17";
        case "pan": return "M6 10 L6 5 C6 3.5 8 3.5 8 5 L8 9 L8 3.5 C8 2 10 2 10 3.5 L10 9 L10 4.5 C10 3 12 3 12 4.5 L12 9 L12 6 C12 4.5 14 4.5 14 6 L14 12 C14 15 12 17 9 17 C7 17 6 16 5 14 L2.5 10.5 C1.5 9 3 8 4 9 Z";
        case "fit": return "M3 7 L3 3 L7 3 M13 3 L17 3 L17 7 M17 13 L17 17 L13 17 M7 17 L3 17 L3 13 M7 7 L13 7 L13 13 L7 13 Z";
        case "open": return "M2 6 L2 4 L8 4 L10 6 L16 6 L16 8 M2 6 L2 16 L15 16 L18 8 L6 8 L3 16";
        case "save": return "M3 3 L14 3 L17 6 L17 17 L3 17 Z M6 3 L6 8 L13 8 L13 3 M6 17 L6 12 L14 12 L14 17";
case "undo": return "M7 4 L3 8 L7 12 M3 8 L12 8 C15.5 8 17 10 17 13 C17 16 15 16 13 16";
case "redo": return "M13 4 L17 8 L13 12 M17 8 L8 8 C4.5 8 3 10 3 13 C3 16 5 16 7 16";
case "plus": return "M4 10 L16 10 M10 4 L10 16";
        case "minus": return "M4 10 L16 10";
        case "close": return "M5 5 L15 15 M15 5 L5 15";
        case "layers": return "M2 7 L10 3 L18 7 L10 11 Z M3 11 L10 15 L17 11 M3 15 L10 19 L17 15";
        case "needle": return "M4 17 L14 3 C16 1 19 3 17 5 Z M14.5 4.5 L15.5 3.5 M4 17 C2 13 2 10 5 10 C8 10 8 15 11 15";
        case "chevron": return "M8 5 L13 10 L8 15";
        default: return "";
        }
    }

    Shape {
        anchors.centerIn: parent
        width: 20
        height: 20
        scale: Math.min(icon.width, icon.height) / 20
        ShapePath {
            strokeColor: icon.color
            strokeWidth: 1.45
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.pathForName() }
        }
    }
}
