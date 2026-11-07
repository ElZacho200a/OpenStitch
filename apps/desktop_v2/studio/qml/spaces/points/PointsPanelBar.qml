// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// Barre compacte des panneaux de l'espace « Points » : réglages par type de point à gauche
// (Satin, Fondu, Retouche, Tirage, Appliqué), affichage à droite (Réaliste, Vue, Fils, Analyse,
// Simulation). Un seul panneau latéral est ouvert à la fois : l'espace ferme le hub frère dès
// qu'un hub ouvre un panneau. Étroite, la barre ne garde que les icônes (infobulles).
Rectangle {
    id: bar
    objectName: "pointsPanelBar"
    required property var digitizeHub
    required property var viewHub
    readonly property var vw: typeof view !== "undefined" ? view : null
    readonly property bool compact: width < 960
    readonly property string openName: digitizeHub.openPanel !== "" ? digitizeHub.openPanel : viewHub.openPanel
    color: Theme.panel
    implicitHeight: Theme.controlHeight + 12

    component Tab: StudioButton {
        quiet: true
        checkable: true
        implicitHeight: Theme.controlHeight - 4
    }
    function label(text) { return bar.compact ? "" : text }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        Text {
            visible: bar.digitizeHub.ready && !bar.compact
            text: qsTr("Réglages")
            color: Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Layout.rightMargin: 4
        }
        Tab { objectName: "satinPanelButton"; visible: bar.digitizeHub.ready; iconName: "satin"; text: bar.label(qsTr("Satin")); tooltip: qsTr("Réglages du satin"); checked: bar.digitizeHub.openPanel === "satin"; onClicked: bar.digitizeHub.toggle("satin") }
        Tab { objectName: "fadePanelButton"; visible: bar.digitizeHub.ready; iconName: "fade"; text: bar.label(qsTr("Fondu")); tooltip: qsTr("Fondu de deux couleurs"); checked: bar.digitizeHub.openPanel === "fade"; onClicked: bar.digitizeHub.toggle("fade") }
        Tab { objectName: "pointEditPanelButton"; visible: bar.digitizeHub.ready; iconName: "stitches"; text: bar.label(qsTr("Retouche")); tooltip: qsTr("Retouche point à point"); checked: bar.digitizeHub.openPanel === "points"; onClicked: bar.digitizeHub.toggle("points") }
        Tab { objectName: "pullPanelButton"; visible: bar.digitizeHub.ready && typeof pull !== "undefined"; iconName: "pull"; text: bar.label(qsTr("Tirage")); tooltip: qsTr("Compensation du tirage du tissu"); checked: bar.digitizeHub.openPanel === "pull"; onClicked: bar.digitizeHub.toggle("pull") }
        Tab { objectName: "appliquePanelButton"; visible: bar.digitizeHub.ready && typeof applique !== "undefined"; iconName: "applique"; text: bar.label(qsTr("Appliqué")); tooltip: qsTr("Appliqué : placement, maintien, découpe"); checked: bar.digitizeHub.openPanel === "applique"; onClicked: bar.digitizeHub.toggle("applique") }

        Item { Layout.fillWidth: true }

        Text {
            visible: bar.viewHub.ready && !bar.compact
            text: qsTr("Affichage")
            color: Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Layout.rightMargin: 4
        }
        Tab { objectName: "realisticButton2"; visible: bar.viewHub.ready; iconName: "realistic"; text: bar.label(qsTr("Réaliste")); shortcutLabel: "Ctrl+Shift+T"; showShortcutLabel: false; tooltip: qsTr("Rendu réaliste (fils en relief, ombre, tissu)"); checked: bar.vw !== null && bar.vw.realistic; onClicked: bar.vw.toggleRealistic() }
        Tab { objectName: "viewPanelButton"; visible: bar.viewHub.ready; iconName: "view"; text: bar.label(qsTr("Vue")); tooltip: qsTr("Vue : filtres, calques, grille, zoom"); checked: bar.viewHub.openPanel === "view"; onClicked: bar.viewHub.toggle("view") }
        Tab { objectName: "threadsPanelButton"; visible: bar.viewHub.ready; iconName: "thread"; text: bar.label(qsTr("Fils")); tooltip: qsTr("Fils et ordre des couleurs"); checked: bar.viewHub.openPanel === "threads"; onClicked: bar.viewHub.toggle("threads") }
        Tab { objectName: "analysisPanelButton"; visible: bar.viewHub.ready; iconName: "analysis"; text: bar.label(qsTr("Analyse")); tooltip: qsTr("Analyse de la broderie"); checked: bar.viewHub.openPanel === "analysis"; onClicked: bar.viewHub.toggle("analysis") }
        Tab { objectName: "simulationButton"; visible: bar.viewHub.ready; iconName: "play"; text: bar.label(qsTr("Simulation")); shortcutLabel: "Ctrl+L"; showShortcutLabel: false; tooltip: qsTr("Simulation de la couture"); checked: bar.vw !== null && bar.vw.simActive; enabled: bar.vw !== null && bar.vw.simReady; onClicked: bar.vw.simActive ? bar.vw.stopSimulation() : bar.vw.startSimulation() }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
}
