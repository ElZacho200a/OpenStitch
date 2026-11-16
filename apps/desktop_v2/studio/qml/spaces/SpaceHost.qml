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
    signal stageFeedback(string message, string tone)
    property var lastStageStates: ({})

    // Only publish completion when a real workspace stage state changes.
    function updateStageFeedback() {
        const previous = lastStageStates
        const next = ({})
        const spaces = workspace.spaces
        for (let i = 0; i < spaces.length; ++i) {
            const stage = spaces[i]
            next[stage.id] = stage.state
            if (previous[stage.id] !== undefined && previous[stage.id] !== stage.state) {
                if (stage.state === "upToDate")
                    stageFeedback(qsTr("%1 : résultat à jour").arg(stage.label), "success")
                else if (stage.state === "stale")
                    stageFeedback(qsTr("%1 : résultat obsolète").arg(stage.label), "warning")
                else if (stage.state === "blocked" && stage.detail && stage.detail.length > 0
                         && stage.detail.toLowerCase().indexOf("cours") < 0)
                    stageFeedback(qsTr("%1 : %2").arg(stage.label).arg(stage.detail), "warning")
            }
        }
        lastStageStates = next
    }

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
        function onSpacesChanged() { host.updateStageFeedback() }
    }
    Component.onCompleted: {
        updateStageFeedback()
        switchTo(workspace.current)
    }

    // --- Cadre : accessible depuis l'en-tête et le clic droit de TOUS les canevas -------------
    // `openHoop(item, x, y)` ouvre le réglage rapide au point donné ; sans point, en haut à droite.
    function openHoop(item, x, y) {
        if (typeof files === "undefined" || files === null)
            return
        if (item)
            hoopPopover.openAt(item, x, y)
        else
            hoopPopover.openAt(host, host.width - hoopPopover.width - 16, 8)
    }
    function hoopSummary() {
        if (typeof files === "undefined" || files === null)
            return ""
        studio.commandCount // relu à chaque changement du document
        const h = files.currentHoop()
        return qsTr("%1 × %2 mm").arg(Math.round(h.widthMm)).arg(Math.round(h.heightMm))
    }
    HoopPopover {
        id: hoopPopover
        parent: host
        ctl: typeof files !== "undefined" ? files : null
    }
    // Espaces sans menu d'objets (image, segmentation, vecto) : clic droit = menu du canevas.
    Controls.Menu {
        id: canvasMenu
        objectName: "spaceCanvasMenu"
        property real atX: 0
        property real atY: 0
        Controls.MenuItem {
            objectName: "canvasMenuHoop"
            text: qsTr("Cadre (%1)…").arg(host.hoopSummary())
            onTriggered: host.openHoop(host.canvas, canvasMenu.atX, canvasMenu.atY)
        }
    }
    Connections {
        target: host.loadedId !== "points" ? host.canvas : null
        ignoreUnknownSignals: true
        function onContextMenuRequested(x, y, objectId) {
            canvasMenu.atX = x
            canvasMenu.atY = y
            const p = host.canvas.mapToItem(host, x, y)
            canvasMenu.popup(host, p.x, p.y)
        }
    }

    Shortcut { sequence: "Ctrl+1"; onActivated: host.workspace.setCurrent("image") }
    Shortcut { sequence: "Ctrl+2"; onActivated: host.workspace.setCurrent("segmentation") }
    Shortcut { sequence: "Ctrl+3"; onActivated: host.workspace.setCurrent("vector") }
    Shortcut { sequence: "Ctrl+4"; onActivated: host.workspace.setCurrent("points") }

    // --- parcours en quatre étapes -----------------------------------------------------------
    // Les quatre espaces sont présentés comme un parcours numéroté. L'hôte (Main.qml) fournit
    // `tabsHost`, un emplacement dans l'en-tête ; sans lui (tests, hôte autonome) le parcours
    // reste au-dessus de l'espace.
    property Item tabsHost: null
    readonly property real tabsWidth: stepper.implicitWidth
    Rectangle {
        id: stepper
        objectName: "spaceTabs"
        parent: host.tabsHost ? host.tabsHost : fallbackSlot
        anchors.centerIn: parent
        // Sous ce palier seuls le numéro et l'étape active gardent leur libellé.
        readonly property bool compact: host.width < 1240
        implicitWidth: tabsRow.implicitWidth + 8
        width: implicitWidth
        height: 40
        radius: 12
        color: Theme.background
        border.width: 1
        border.color: Theme.divider

        // Pastille pleine qui glisse d'une étape à l'autre, y compris via Ctrl+1..4.
        Rectangle {
            id: activePill
            objectName: "activeSpaceUnderline"
            // `count` rend la liaison réactive à la création des étapes (itemAt seul ne notifie pas).
            readonly property var currentTab: tabsRepeater.count > host.workspace.currentIndex
                                              ? tabsRepeater.itemAt(host.workspace.currentIndex) : null
            x: currentTab ? tabsRow.x + currentTab.x : 4
            y: 4
            width: currentTab ? currentTab.width : 0
            height: parent.height - 8
            radius: 9
            color: Theme.accent
            // Pas de glissement à l'ouverture : la pastille est posée d'emblée sur l'étape active.
            property bool settled: false
            Timer { interval: 300; running: true; onTriggered: activePill.settled = true }
            Behavior on x { enabled: activePill.settled; NumberAnimation { duration: Theme.feedbackSlowMs; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: activePill.settled; NumberAnimation { duration: Theme.feedbackSlowMs; easing.type: Easing.OutCubic } }
        }

        Row {
            id: tabsRow
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Repeater {
                id: tabsRepeater
                // Modèle = nombre d'espaces : les étapes ne sont pas recréées quand un état change.
                model: 4
                delegate: Controls.AbstractButton {
                    id: tab
                    required property int index
                    readonly property var modelData: host.workspace.spaces[index]
                    objectName: "spaceTab_" + modelData.id
                    readonly property bool current: host.workspace.current === modelData.id
                    implicitHeight: 32
                    implicitWidth: tabContent.implicitWidth + 24
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.label + " : " + host.stateLabel(modelData.state)
                    Accessible.description: modelData.detail + ". " + qsTr("Raccourci ") + modelData.shortcut
                    onClicked: host.workspace.setCurrent(modelData.id)
                    background: Rectangle {
                        radius: 9
                        color: tab.current ? "transparent" : tab.down ? Theme.hover
                               : tab.hovered ? Theme.raised : "transparent"
                        border.width: tab.activeFocus ? 2 : 0
                        border.color: tab.current ? Theme.accentInk : Theme.accent
                        Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                    }
                    contentItem: Item {
                        implicitWidth: tabContent.implicitWidth
                        implicitHeight: tabContent.implicitHeight
                        Row {
                            id: tabContent
                            anchors.centerIn: parent
                            spacing: 8
                            Rectangle {
                                objectName: "spaceBadge_" + tab.modelData.id
                                property string badgeState: tab.modelData.state
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20
                                height: 20
                                radius: 10
                                color: tab.current ? Theme.accentInk : Theme.raised
                                border.width: badgeState === "empty" ? 0 : 2
                                border.color: host.stateColor(badgeState)
                                Behavior on color { ColorAnimation { duration: Theme.feedbackSlowMs } }
                                Behavior on border.color { ColorAnimation { duration: Theme.feedbackSlowMs } }
                                Text {
                                    anchors.centerIn: parent
                                    // L'état est porté par un signe, pas seulement par la couleur.
                                    text: parent.badgeState === "upToDate" ? "✓"
                                          : parent.badgeState === "stale" || parent.badgeState === "blocked" ? "!"
                                          : (tab.index + 1)
                                    color: tab.current ? Theme.accent : host.stateColor(parent.badgeState)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.captionSize
                                    font.weight: Font.Bold
                                }
                            }
                            Text {
                                visible: !stepper.compact || tab.current
                                anchors.verticalCenter: parent.verticalCenter
                                text: tab.modelData.label
                                color: tab.current ? Theme.accentInk : Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.bodySize
                                font.weight: tab.current ? Font.DemiBold : Font.Normal
                                Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                            }
                        }
                    }
                    StudioToolTip {
                        delay: 500
                        text: tab.modelData.label + " (" + host.stateLabel(tab.modelData.state) + ") · "
                              + tab.modelData.detail + "  ·  " + tab.modelData.shortcut
                    }
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
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: fallbackSlot
            Layout.fillWidth: true
            Layout.preferredHeight: host.tabsHost ? 0 : 52
        }

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
                sourceComponent: PointsSpace {
                    workspace: host.workspace
                    onHoopRequested: function(x, y) { host.openHoop(host.canvas, x, y) }
                }
                onLoaded: host.activateSpace("points", item)
            }
        }
    }
}
