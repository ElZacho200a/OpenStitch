// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// UNE barre d'options contextuelle (remplace les boutons flottants du canevas) : à gauche les
// gestes de numérisation (points, fondu, satin, guide, rupture, barreaux), puis l'indication de
// l'outil actif, à droite l'état de l'étape « points ».
Rectangle {
    id: bar
    objectName: "pointsOptionsBar"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var digitizeCtl: typeof digitizer !== "undefined" ? digitizer : null
    readonly property var spaceCtl: typeof pointsSpace !== "undefined" ? pointsSpace : null
    readonly property bool compact: width < 900
    readonly property string stateName: spaceCtl ? spaceCtl.state : "empty"
    readonly property string hintText: {
        if (digitizeCtl && digitizeCtl.mode !== "")
            return digitizeCtl.hint !== "" ? digitizeCtl.hint : qsTr("Geste actif : cliquez sur le canevas ; Échap pour terminer.")
        if (toolCtl && toolCtl.hint !== "")
            return toolCtl.hint
        return qsTr("Cliquez sur un fil ou choisissez un objet dans la liste.")
    }
    color: "transparent" // posée dans une carte (StudioCard) qui porte le fond
    implicitHeight: Theme.controlHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        Text {
            objectName: "pointsMode"
            text: bar.digitizeCtl && bar.digitizeCtl.mode !== "" ? qsTr("Geste : ") + bar.digitizeCtl.mode
                  : bar.toolCtl && bar.toolCtl.activeTool === "needle.origin" ? qsTr("Point zéro")
                  : qsTr("Sélection")
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            font.weight: Font.DemiBold
            Layout.rightMargin: 8
        }

        Text {
            objectName: "pointsHint"
            Layout.fillWidth: true
            text: bar.hintText + (bar.toolCtl && bar.toolCtl.message !== "" ? "  ·  " + bar.toolCtl.message : "")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            elide: Text.ElideRight
        }

        StudioButton {
            objectName: "needleResetButton"
            visible: bar.toolCtl !== null && bar.toolCtl.activeTool === "needle.origin"
            text: qsTr("Revenir au centre")
            quiet: true
            enabled: bar.toolCtl !== null && bar.toolCtl.needleOriginCustom
            tooltip: qsTr("Remet le point zéro au centre de la broderie")
            shortcutLabel: qsTr("Suppr")
            onClicked: bar.toolCtl.resetNeedleOrigin()
        }

        Rectangle {
            id: dot
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: bar.stateName === "upToDate" ? Theme.success
                   : bar.stateName === "stale" ? Theme.warning : Theme.faint
        }
        Text {
            objectName: "pointsStateText"
            visible: !bar.compact
            text: bar.stateName === "upToDate" ? qsTr("Points à jour")
                  : bar.stateName === "stale" ? qsTr("Formes sources manquantes") : qsTr("Pas encore de points")
            color: bar.stateName === "stale" ? Theme.warning : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Accessible.name: bar.spaceCtl ? bar.spaceCtl.summary : text
        }
    }
}
