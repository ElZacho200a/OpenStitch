// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import ".."

// Hôte des QUATRE ESPACES (image, segmentation, vector, points). FICHIER PARTAGÉ : seul le Lead le
// modifie (docs/desktop-v2/23-espaces-studio.md).
//  - espaces créés à la première visite puis conservés (états UI et canevas réutilisés) ;
//  - barre d'onglets avec pastille d'état, raccourcis Ctrl+1..4 indépendants du focus ;
//  - bandeau sous la barre : état de l'étape AMONT de l'espace courant + bouton d'action fourni par
//    l'espace (`workspace.setStageAction`) ;
//  - la vue (centre, zoom) du canevas qui part est sauvegardée, celle de l'espace qui arrive est
//    restaurée ; la sélection vit dans la session et traverse sans copie.
Item {
    id: host
    objectName: "spaceHost"
    required property var workspace
    // Racine de l'espace actif (contrat : `canvas`, `active`) et son canevas.
    readonly property var space: internal.loadedId === "image" ? imageLoader.item
                                  : internal.loadedId === "segmentation" ? segmentationLoader.item
                                  : internal.loadedId === "vector" ? vectorLoader.item
                                  : internal.loadedId === "points" ? pointsLoader.item : null
    readonly property var canvas: space ? space.canvas : null
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
        property bool visitedImage: false
        property bool visitedSegmentation: false
        property bool visitedVector: false
        property bool visitedPoints: false
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

    // Le changement d'espace ne détruit plus le QML ni le canevas : les textures/états
    // pourront être réutilisés. Seul l'espace visible doit rester actif et attaché.
    function activateSpace(id, item) {
        if (!item || internal.loadedId !== id || item.active)
            return
        const saved = workspace.view
        if (item.canvas && saved.zoom !== undefined)
            item.canvas.restoreViewState(saved)
        item.active = true
        // ViewPanel attache la première fois ; les visites suivantes doivent réattacher
        // le contrôleur de vue au canevas actif, pas au dernier canevas créé.
        if (item.canvas && typeof view !== "undefined" && view !== null)
            view.attachCanvas(item.canvas)
        if (item.canvas)
            item.canvas.forceActiveFocus()
    }

    function switchTo(id) {
        if (!sources[id] || id === internal.loadedId)
            return
        const outgoing = host.space
        if (outgoing && outgoing.canvas)
            workspace.saveView(outgoing.canvas.viewState())
        if (outgoing)
            outgoing.active = false
        internal.loadedId = id
        switch (id) {
        case "image": internal.visitedImage = true; break
        case "segmentation": internal.visitedSegmentation = true; break
        case "vector": internal.visitedVector = true; break
        case "points": internal.visitedPoints = true; break
        }
        activateSpace(id, host.space)
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
                id: tabsRow
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 3
                Repeater {
                    id: tabsRepeater
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
                            color: tab.current ? Theme.accentSoft : tab.down ? Theme.hover
                                   : tab.hovered ? Theme.raised : "transparent"
                            radius: Theme.radius
                            border.width: tab.activeFocus ? 2 : 0
                            border.color: Theme.accent
                            Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                            Behavior on border.width { NumberAnimation { duration: Theme.feedbackFastMs } }
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
                                Behavior on color { ColorAnimation { duration: Theme.feedbackSlowMs } }
                                Behavior on border.color { ColorAnimation { duration: Theme.feedbackSlowMs } }
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
            // Single underline moves between tabs, including Ctrl+1..4 navigation.
            Rectangle {
                objectName: "activeSpaceUnderline"
                z: 2
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                height: 3
                radius: height / 2
                color: Theme.accent
                readonly property var currentTab: tabsRepeater.itemAt(host.workspace.currentIndex)
                x: currentTab ? tabsRow.x + currentTab.x + 6 : 18
                width: currentTab ? Math.max(12, currentTab.width - 12) : 0
                Behavior on x { NumberAnimation { duration: Theme.feedbackSlowMs; easing.type: Easing.InOutCubic } }
                Behavior on width { NumberAnimation { duration: Theme.feedbackSlowMs; easing.type: Easing.InOutCubic } }
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

        // --- espaces chargés à la demande, mais pas détruits à chaque navigation --------------
        // Loader.active ne repasse jamais à false : Qt Quick conserve les instances.
        // Les espaces non visibles ne reçoivent ni souris ni focus et sont désactivés via active.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Loader {
                id: imageLoader
                anchors.fill: parent
                active: internal.visitedImage
                visible: internal.loadedId === "image"
                asynchronous: false
                sourceComponent: ImageSpace { workspace: host.workspace }
                onLoaded: host.activateSpace("image", item)
            }
            Loader {
                id: segmentationLoader
                anchors.fill: parent
                active: internal.visitedSegmentation
                visible: internal.loadedId === "segmentation"
                asynchronous: false
                sourceComponent: SegmentationSpace { workspace: host.workspace }
                onLoaded: host.activateSpace("segmentation", item)
            }
            Loader {
                id: vectorLoader
                anchors.fill: parent
                active: internal.visitedVector
                visible: internal.loadedId === "vector"
                asynchronous: false
                sourceComponent: VectorSpace { workspace: host.workspace }
                onLoaded: host.activateSpace("vector", item)
            }
            Loader {
                id: pointsLoader
                anchors.fill: parent
                active: internal.visitedPoints
                visible: internal.loadedId === "points"
                asynchronous: false
                sourceComponent: PointsSpace { workspace: host.workspace }
                onLoaded: host.activateSpace("points", item)
            }
        }
    }
}
