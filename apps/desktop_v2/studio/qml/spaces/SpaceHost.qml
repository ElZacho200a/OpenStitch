// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import ".."

// Hôte des QUATRE ESPACES (image, segmentation, vector, points). FICHIER PARTAGÉ : seul le Lead le
// modifie (docs/desktop-v2/23-espaces-studio.md).
//  - UN SEUL espace est instancié à la fois (Loader) : une seule instance de canevas GPU ;
//  - barre d'onglets avec pastille d'état, raccourcis Ctrl+1..4 indépendants du focus ;
//  - bandeau sous la barre : état de l'étape AMONT de l'espace courant + bouton d'action fourni par
//    l'espace (`workspace.setStageAction`) ;
//  - la vue (centre, zoom) du canevas qui part est sauvegardée, celle de l'espace qui arrive est
//    restaurée ; la sélection vit dans la session et traverse sans copie.
//  - Le focus est transféré explicitement au canvas actif lors du changement d'espace via 
//    activateCanvas(), assurant que les raccourcis et interactions fonctionnent immédiatement.
Item {
    id: host
    objectName: "spaceHost"
    required property var workspace
    // Racine de l'espace actif (contrat : `canvas`, `active`) et son canevas.
    readonly property Item space: loader.item as Item
    readonly property var canvas: loader.item ? loader.item.canvas : null
    readonly property string loadedId: internal.loadedId

    readonly property var sources: ({
        "image": "ImageSpace.qml",
        "segmentation": "SegmentationSpace.qml",
        "vector": "VectorSpace.qml",
        "points": "PointsSpace.qml"
    })

    QtObject {
        id: internal
        property string loadedId: ""
    }

    function stateColor(state) {
        switch (state) {
        case "upToDate": return Theme.success
        case "stale": return Theme.warning
        case "blocked": return Theme.error
        default: return Theme.faint
        }
    }
    function stateLabel(state) {
        switch (state) {
        case "upToDate": return qsTr("à jour")
        case "stale": return qsTr("obsolète")
        case "blocked": return qsTr("bloquée")
        default: return qsTr("vide")
        }
    }

    // Sauvegarde la vue de l'espace sortant, charge l'entrant, restaure sa vue.
    function switchTo(id) {
        if (!sources[id] || id === internal.loadedId)
            return
        if (loader.item && loader.item.canvas && internal.loadedId !== "")
            workspace.saveView(loader.item.canvas.viewState())
        loader.setSource(sources[id], { "workspace": host.workspace })
        internal.loadedId = id
    }

    // Transfère le focus au canvas de l'espace actif quand il devient courant.
    function activateCanvas() {
        const space = loader.item
        if (!space || !space.canvas)
            return
        space.canvas.setFocus()
    }
    Connections {
        target: host.workspace
        function onCurrentChanged() { activateCanvas() }
    }

    Connections {
        target: host.workspace
        function onCurrentChanged() { host.switchTo(host.workspace.current) }
    }
    Component.onCompleted: switchTo(workspace.current)

    Shortcut { sequence: "Ctrl+1"; onActivated: host.workspace.setCurrent("image") }
    Shortcut { sequence: "Ctrl+2"; onActivated: host.workspace.setCurrent("segmentation") }
    Shortcut { sequence: "Ctrl+3"; onActivated: host.workspace.setCurrent("vector") }
    Shortcut { sequence: "Ctrl+4"; onActivated: host.workspace.setCurrent("points") }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- barre d'onglets ---------------------------------------------------------------
        Rectangle {
            objectName: "spaceTabs"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 3
                Repeater {
                    // Modèle = nombre d'espaces : les onglets ne sont pas recréés quand un état change.
                    model: 4
                    delegate: Controls.AbstractButton {
                        id: tab
                        required property int index
                        readonly property var modelData: host.workspace.spaces[index]
                        objectName: "spaceTab_" + modelData.id
                        readonly property bool current: host.workspace.current === modelData.id
                        Layout.preferredHeight: 32
                        Layout.preferredWidth: host.width < 1180 ? 122 : 146
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.role: Accessible.PageTab
                        Accessible.name: modelData.label + " : " + host.stateLabel(modelData.state)
                        Accessible.description: modelData.detail + ". " + qsTr("Raccourci ") + modelData.shortcut
                        onClicked: host.workspace.setCurrent(modelData.id)
                        background: Rectangle {
                            color: tab.current ? Theme.accentSoft : tab.hovered ? Theme.raised : "transparent"
                            radius: Theme.radius
                            border.width: tab.activeFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                        contentItem: Row {
                            spacing: 8
                            anchors.centerIn: parent
                            Rectangle {
                                objectName: "spaceBadge_" + tab.modelData.id
                                property string badgeState: tab.modelData.state
                                anchors.verticalCenter: parent.verticalCenter
                                width: 10
                                height: 10
                                radius: 5
                                color: badgeState === "empty" ? "transparent" : host.stateColor(badgeState)
                                border.width: 2
                                border.color: host.stateColor(badgeState)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tab.modelData.label
                                color: tab.current ? Theme.accent : Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.bodySize
                                font.weight: tab.current ? Font.DemiBold : Font.Normal
                            }
                        }
                        StudioToolTip {
                            delay: 500
                            text: tab.modelData.label + " (" + host.stateLabel(tab.modelData.state) + ") · "
                                  + tab.modelData.detail + "  ·  " + tab.modelData.shortcut
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: host.width >= 1160
                    text: qsTr("UN DOCUMENT · QUATRE ESPACES LIÉS")
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.captionSize
                    font.letterSpacing: 1.1
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

        // --- bandeau de l'étape amont ------------------------------------------------------
        Rectangle {
            id: banner
            objectName: "upstreamBanner"
            readonly property var info: host.workspace.banner
            visible: info.visible === true
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            color: Theme.accentSoft
            Accessible.role: Accessible.AlertMessage
            Accessible.name: info.text !== undefined ? info.text : ""
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 10
                Rectangle {
                    implicitWidth: 10
                    implicitHeight: 10
                    radius: 5
                    color: host.stateColor(banner.info.state)
                }
                Text {
                    objectName: "upstreamBannerText"
                    Layout.fillWidth: true
                    text: banner.info.text !== undefined ? banner.info.text : ""
                    elide: Text.ElideRight
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                }
                StudioButton {
                    objectName: "upstreamBannerAction"
                    visible: banner.info.actionLabel !== undefined && banner.info.actionLabel !== ""
                    text: banner.info.actionLabel !== undefined ? banner.info.actionLabel : ""
                    enabled: banner.info.actionEnabled === true
                    implicitHeight: 26
                    onClicked: host.workspace.requestUpdate(host.workspace.current)
                }
            }
        }

        // --- espace actif ------------------------------------------------------------------
        Loader {
            id: loader
            objectName: "spaceLoader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            asynchronous: false
            onLoaded: {
                const item = loader.item
                if (!item)
                    return
                const view = host.workspace.view
                if (item.canvas && view.zoom !== undefined)
                    item.canvas.restoreViewState(view)
                item.active = true
                activateCanvas()
            }
        }
    }
}
