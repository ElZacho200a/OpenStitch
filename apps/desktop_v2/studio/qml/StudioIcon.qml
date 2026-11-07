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
        // Icônes de l'espace « Points » (type de point, outils de numérisation, panneaux).
        case "satin": return "M3 15 C6 15 6 5 3 5 M17 15 C14 15 14 5 17 5 M4 14 L16 14 M4 11 L16 11 M4 8 L16 8";
        case "fade": return "M3 4 L17 4 M3 8 L14 8 M3 12 L11 12 M3 16 L8 16 M16 10 L16 16 M13 13 L19 13";
        case "guide": return "M3 16 L16 4 M16 4 L11 5 M16 4 L15 9 M3 10 L7 10 M3 14 L5 14";
        case "break": return "M3 6 L8 6 M12 6 L17 6 M3 14 L17 14 M10 2 L8 18";
        case "rungs": return "M4 3 L4 17 M16 3 L16 17 M4 6 L16 6 M4 10 L16 10 M4 14 L16 14";
        case "stitches": return "M3 14 L7 6 L11 14 L15 6 L17 10 M3 14 L3 14 M5 17 L7 17 M12 17 L14 17";
        case "text": return "M4 5 L4 3.5 L16 3.5 L16 5 M10 3.5 L10 16.5 M7.5 16.5 L12.5 16.5";
        case "eyedropper": return "M13 3 L17 7 M12 4 L16 8 L8 16 L4 16 L4 12 Z M4 16 L2.5 17.5";
        case "measure": return "M2 13 L13 2 L18 7 L7 18 Z M5 10 L7 12 M8 7 L10 9 M11 4 L13 6";
        case "applique": return "M3 10 C3 5 8 3 10 3 C13 3 17 5 17 10 C17 14 13 17 10 17 C6 17 3 14 3 10 Z M7 10 L9 12 L13 8";
        case "pull": return "M3 10 L17 10 M3 10 L6 7 M3 10 L6 13 M17 10 L14 7 M17 10 L14 13 M10 4 L10 7 M10 13 L10 16";
        case "view": return "M2 10 C5 5 15 5 18 10 C15 15 5 15 2 10 Z M10 7.5 A2.5 2.5 0 1 1 9.99 7.5";
        case "thread": return "M6 3 L14 3 L14 6 L6 6 Z M6 6 L6 17 L14 17 L14 6 M6 9 L14 9 M6 12 L14 12 M6 15 L14 15";
        case "analysis": return "M3 17 L3 3 M3 17 L17 17 M6 14 L6 10 M10 14 L10 6 M14 14 L14 8";
        case "play": return "M6 3.5 L16 10 L6 16.5 Z";
        case "realistic": return "M10 2 L12 8 L18 10 L12 12 L10 18 L8 12 L2 10 L8 8 Z";
        case "link": return "M8 12 L12 8 M7 9 L5 11 C3.5 12.5 3.5 15 5 16 C6.5 17 8.5 16.5 9.5 15 L11 13 M13 11 L15 9 C16.5 7.5 16.5 5 15 4 C13.5 3 11.5 3.5 10.5 5 L9 7";
        case "edit": return "M3 17 L3 13 L13 3 L17 7 L7 17 Z M11 5 L15 9";
        case "tree": return "M4 3 L4 17 M4 6 L10 6 M4 11 L10 11 M4 16 L10 16 M10 4 L17 4 L17 8 L10 8 Z";
        case "list": return "M4 5 L17 5 M4 10 L17 10 M4 15 L17 15";
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
