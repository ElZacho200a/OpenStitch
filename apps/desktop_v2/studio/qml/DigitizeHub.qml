// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Hub « numérisation avancée » (satin, fondus, retouche des points) : trois panneaux à la demande,
// posés sur le canevas à gauche. Inerte si l'hôte ne fournit pas les adaptateurs.
Item {
    id: hub
    objectName: "digitizeHub"
    property string openPanel: ""
    readonly property bool ready: loader.status === Loader.Ready
    function toggle(name) { openPanel = openPanel === name ? "" : name }

    Loader {
        id: loader
        anchors.fill: parent
        active: typeof satin !== "undefined" && typeof fade !== "undefined" && typeof pointEdit !== "undefined"
        sourceComponent: Item {
            Row {
                objectName: "digitizeToolbar"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 10
                anchors.leftMargin: 44
                spacing: 4
                StudioButton { objectName: "satinPanelButton"; text: qsTr("Satin"); checkable: true; checked: hub.openPanel === "satin"; onClicked: hub.toggle("satin") }
                StudioButton { objectName: "fadePanelButton"; text: qsTr("Fondu"); checkable: true; checked: hub.openPanel === "fade"; onClicked: hub.toggle("fade") }
                StudioButton { objectName: "pointEditPanelButton"; text: qsTr("Points"); checkable: true; checked: hub.openPanel === "points"; onClicked: hub.toggle("points") }
            }
            Item {
                visible: hub.openPanel !== ""
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.topMargin: 54
                anchors.leftMargin: 44
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 70
                width: Math.min(340, parent.width - 90)
                SatinPanel { anchors.fill: parent; visible: hub.openPanel === "satin"; controller: satin }
                FadePanel { anchors.fill: parent; visible: hub.openPanel === "fade"; controller: fade }
                PointEditPanel { anchors.fill: parent; visible: hub.openPanel === "points"; controller: pointEdit }
            }
        }
    }
}
