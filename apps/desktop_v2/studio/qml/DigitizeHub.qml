// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Hub « numérisation avancée » (satin, fondus, retouche des points, tirage, appliqué) : UN
// panneau à la fois, posé sur le bord droit du canevas. Les boutons et les gestes ne sont plus
// flottants : l'espace « Points » les porte dans sa barre de panneaux et sa barre d'options
// (`toolbarVisible: false`). Inerte si l'hôte ne fournit pas les adaptateurs.
Item {
    id: hub
    objectName: "digitizeHub"
    property string openPanel: ""
    // Vrai : le hub dessine sa propre barre de boutons (usage historique). L'espace « Points » la
    // remplace par ses barres et la met à faux.
    property bool toolbarVisible: true
    readonly property bool ready: loader.status === Loader.Ready
    function toggle(name) { openPanel = openPanel === name ? "" : name }

    Loader {
        id: loader
        anchors.fill: parent
        active: typeof satin !== "undefined" && typeof fade !== "undefined" && typeof pointEdit !== "undefined"
        sourceComponent: Item {
            Loader {
                active: hub.toolbarVisible
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 10
                anchors.leftMargin: 44
                sourceComponent: Row {
                    objectName: "digitizeToolbar"
                    spacing: 4
                    StudioButton { objectName: "satinPanelButton"; text: qsTr("Satin"); checkable: true; checked: hub.openPanel === "satin"; onClicked: hub.toggle("satin") }
                    StudioButton { objectName: "fadePanelButton"; text: qsTr("Fondu"); checkable: true; checked: hub.openPanel === "fade"; onClicked: hub.toggle("fade") }
                    StudioButton { objectName: "pointEditPanelButton"; text: qsTr("Points"); checkable: true; checked: hub.openPanel === "points"; onClicked: hub.toggle("points") }
                    StudioButton { objectName: "pullPanelButton"; visible: typeof pull !== "undefined"; text: qsTr("Tirage"); checkable: true; checked: hub.openPanel === "pull"; onClicked: hub.toggle("pull") }
                    StudioButton { objectName: "fixPanelButton"; visible: typeof fix !== "undefined"; text: qsTr("Corriger"); checkable: true; checked: hub.openPanel === "fix"; onClicked: hub.toggle("fix") }
                    StudioButton { objectName: "appliquePanelButton"; visible: typeof applique !== "undefined"; text: qsTr("Appliqué"); checkable: true; checked: hub.openPanel === "applique"; onClicked: hub.toggle("applique") }
                }
            }
            Loader {
                active: hub.toolbarVisible && typeof digitizer !== "undefined"
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.bottomMargin: 76
                anchors.leftMargin: 44
                sourceComponent: Row {
                    objectName: "gestureToolbar"
                    spacing: 4
                    Repeater {
                        model: [["points", qsTr("Geste points")], ["fade", qsTr("Geste fondu")], ["satin", qsTr("Geste satin")],
                                ["guide", qsTr("Guide")], ["break", qsTr("Rupture")], ["rungs", qsTr("Barreaux")]]
                        StudioButton {
                            objectName: "gesture_" + modelData[0]
                            text: modelData[1]
                            checkable: true
                            checked: typeof digitizer !== "undefined" && digitizer.mode === modelData[0]
                            onClicked: digitizer.mode = checked ? modelData[0] : ""
                        }
                    }
                }
            }
            Item {
                id: drawer
                objectName: "digitizeDrawer"
                visible: hub.openPanel !== ""
                anchors.top: parent.top
                anchors.topMargin: hub.toolbarVisible ? 54 : 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: hub.toolbarVisible ? 70 : 12
                // Barre d'outils historique : à gauche. Espace « Points » : bord droit, comme
                // le tiroir de la Vue (jamais deux tiroirs ouverts : le hub frère se ferme).
                x: hub.toolbarVisible ? 44 : parent.width - width - 12
                width: Math.min(340, parent.width - (hub.toolbarVisible ? 90 : 24))
                SatinPanel { anchors.fill: parent; visible: hub.openPanel === "satin"; controller: satin }
                FadePanel { anchors.fill: parent; visible: hub.openPanel === "fade"; controller: fade }
                PointEditPanel { anchors.fill: parent; visible: hub.openPanel === "points"; controller: pointEdit }
                PullPanel { anchors.fill: parent; visible: hub.openPanel === "pull"; controller: typeof pull !== "undefined" ? pull : null }
                AppliquePanel { anchors.fill: parent; visible: hub.openPanel === "applique"; controller: typeof applique !== "undefined" ? applique : null }
                FixPanel { anchors.fill: parent; visible: hub.openPanel === "fix"; controller: typeof fix !== "undefined" ? fix : null }
            }
        }
    }
}
