// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// Barre d'actions flottante du canevas Vecteurs : les opérations utilisées en permanence
// (booléennes, fusion, séparation, nœuds) sont à portée de souris dès qu'une sélection existe,
// sans passer par l'onglet « Édition » ni défiler. Raccourcis actifs seulement dans l'espace.
StudioCard {
    id: bar
    objectName: "vectorQuickBar"
    property bool spaceActive: false
    readonly property var vec: typeof vectors !== "undefined" ? vectors : null
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property int count: vec ? vec.selectedCount : 0
    readonly property bool two: count > 1
    readonly property bool nodeMode: toolCtl !== null && toolCtl.activeTool === "node.edit"
    readonly property bool nodes: nodeMode && toolCtl.selectedNodeCount > 0
    readonly property string needTwo: qsTr("Sélectionnez au moins deux formes (Maj+clic)")
    visible: vec !== null && (count > 0 || nodeMode)
    implicitWidth: row.implicitWidth + 12
    implicitHeight: row.implicitHeight + 10

    component Op: StudioButton {
        implicitHeight: 28
        implicitWidth: Math.max(30, implicitContentWidth + 14)
        quiet: true
        showShortcutLabel: false
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Text {
            objectName: "vectorQuickCount"
            text: bar.nodeMode ? qsTr("%1 nœud(s)").arg(bar.toolCtl.selectedNodeCount)
                               : qsTr("%1 forme(s)").arg(bar.count)
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Layout.rightMargin: 6
            Layout.leftMargin: 4
        }
        Op { objectName: "quickUnion"; visible: !bar.nodeMode; text: qsTr("Union"); shortcutLabel: "Ctrl++"; enabled: bar.two; disabledReason: bar.needTwo
             tooltip: qsTr("Réunit les formes sélectionnées en une seule"); onClicked: bar.vec.booleanOp("union") }
        Op { objectName: "quickSubtract"; visible: !bar.nodeMode; text: qsTr("Soustraire"); shortcutLabel: "Ctrl+-"; enabled: bar.two; disabledReason: bar.needTwo
             tooltip: qsTr("Découpe les formes du dessus dans la plus basse de la pile"); onClicked: bar.vec.booleanOp("subtract") }
        Op { objectName: "quickIntersect"; visible: !bar.nodeMode; text: qsTr("Intersection"); shortcutLabel: "Ctrl+*"; enabled: bar.two; disabledReason: bar.needTwo
             tooltip: qsTr("Garde seulement la partie commune aux formes"); onClicked: bar.vec.booleanOp("intersect") }
        Op { objectName: "quickExclude"; visible: !bar.nodeMode; text: qsTr("Exclusion"); shortcutLabel: "Ctrl+^"; enabled: bar.two; disabledReason: bar.needTwo
             tooltip: qsTr("Garde tout sauf la partie commune"); onClicked: bar.vec.booleanOp("exclude") }
        Rectangle { visible: !bar.nodeMode; implicitWidth: 1; implicitHeight: 18; color: Theme.divider; Layout.leftMargin: 3; Layout.rightMargin: 3 }
        Op { objectName: "quickBreak"; visible: !bar.nodeMode; text: qsTr("Séparer"); shortcutLabel: "Ctrl+Maj+K"; enabled: bar.count > 0
             tooltip: qsTr("Sépare chaque morceau disjoint en objet indépendant"); onClicked: bar.vec.breakApart() }
        Op { objectName: "quickNodeCorner"; visible: bar.nodeMode; text: qsTr("Anguleux"); shortcutLabel: "C"; enabled: bar.nodes
             disabledReason: qsTr("Sélectionnez des nœuds"); onClicked: bar.toolCtl.setSelectedNodeKind(false) }
        Op { objectName: "quickNodeSmooth"; visible: bar.nodeMode; text: qsTr("Lisse"); shortcutLabel: "S"; enabled: bar.nodes
             disabledReason: qsTr("Sélectionnez des nœuds"); onClicked: bar.toolCtl.setSelectedNodeKind(true) }
        Op { objectName: "quickNodeDelete"; visible: bar.nodeMode; text: qsTr("Supprimer nœud"); enabled: bar.nodes
             disabledReason: qsTr("Sélectionnez des nœuds"); onClicked: bar.toolCtl.deleteSelectedNodes() }
        Op { objectName: "quickNodeHole"; visible: bar.nodeMode; text: qsTr("Supprimer trou"); enabled: bar.nodes
             disabledReason: qsTr("Sélectionnez un nœud du trou"); onClicked: bar.toolCtl.removeHole() }
    }

    // Raccourcis style éditeur vectoriel ; « = » et « 8 » couvrent les claviers sans pavé.
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; enabled: bar.spaceActive && bar.two; onActivated: bar.vec.booleanOp("union") }
    Shortcut { sequences: ["Ctrl+-"]; enabled: bar.spaceActive && bar.two; onActivated: bar.vec.booleanOp("subtract") }
    Shortcut { sequences: ["Ctrl+*", "Ctrl+8"]; enabled: bar.spaceActive && bar.two; onActivated: bar.vec.booleanOp("intersect") }
    Shortcut { sequences: ["Ctrl+^", "Ctrl+6"]; enabled: bar.spaceActive && bar.two; onActivated: bar.vec.booleanOp("exclude") }
    Shortcut { sequences: ["Ctrl+Shift+K"]; enabled: bar.spaceActive && bar.count > 0; onActivated: bar.vec.breakApart() }
}
