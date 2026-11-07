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
            return qsTr("Geste actif : cliquez sur le canevas ; Échap pour terminer.")
        if (toolCtl && toolCtl.hint !== "")
            return toolCtl.hint
        return qsTr("Cliquez sur un fil ou choisissez un objet dans la liste.")
    }
    color: Theme.background
    implicitHeight: Theme.controlHeight + 6

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        Text {
            visible: bar.digitizeCtl !== null && !bar.compact
            text: qsTr("Gestes")
            color: Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Layout.rightMargin: 4
        }
        Repeater {
            model: bar.digitizeCtl ? [["points", qsTr("Points"), "stitches", qsTr("Geste points : poser des points à la main")],
                                      ["fade", qsTr("Fondu"), "fade", qsTr("Geste fondu : tracer l'axe d'un fondu")],
                                      ["satin", qsTr("Satin"), "satin", qsTr("Geste satin : tracer une colonne satin")],
                                      ["guide", qsTr("Guide"), "guide", qsTr("Guide de direction du remplissage")],
                                      ["break", qsTr("Rupture"), "break", qsTr("Ligne de rupture du remplissage")],
                                      ["rungs", qsTr("Barreaux"), "rungs", qsTr("Barreaux d'un satin")]] : []
            delegate: StudioButton {
                required property var modelData
                objectName: "gesture_" + modelData[0]
                quiet: true
                checkable: true
                iconName: modelData[2]
                text: bar.compact ? "" : modelData[1]
                tooltip: modelData[3]
                implicitHeight: Theme.controlHeight - 4
                checked: bar.digitizeCtl !== null && bar.digitizeCtl.mode === modelData[0]
                onClicked: bar.digitizeCtl.mode = checked ? modelData[0] : ""
            }
        }

        Rectangle {
            visible: bar.digitizeCtl !== null
            implicitWidth: 1
            implicitHeight: 18
            color: Theme.divider
            Layout.leftMargin: 6
            Layout.rightMargin: 6
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
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
}
