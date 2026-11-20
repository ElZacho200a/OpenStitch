// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Arbre documentaire : calques (du dessus au fond) > groupes > objets, dans l'ordre de couture.
// Pure présentation : toute action appelle l'EditController (`editor`) ou le StudioController.
// Glisser-déposer : une ligne d'objet déposée sur un calque y déplace la sélection ; une ligne de
// calque déposée sur un autre calque prend sa place (l'ordre de couture suit l'ordre des calques).
// Clic = sélection, Ctrl+clic = ajout, Maj+clic = plage ; Alt+clic sur l'oeil = isoler le calque.
Item {
    id: root
    objectName: "objectTree"
    // Calque courant : celui du document (`editor.activeLayer`), le même dans tous les canevas.
    property string currentLayer: editor.activeLayer
    // Ancre de la sélection par plage (Maj+clic), dernier objet cliqué sans Maj.
    property string anchorId: ""
    property bool dragLayer: false
    // Confirmation avant de supprimer un calque non vide (fusionne ses objets dans le voisin,
    // change l'ordre de couture) : {id, name, objects}, ou null hors confirmation en attente.
    property var pendingDeleteLayer: null
    property string dropId: "" // calque survolé pendant un glisser-déposer
    function layerDisplayIndex(id) {
        const list = editor.layers
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return i
        return -1
    }
    function clickObject(id, modifiers) {
        if ((modifiers & Qt.ShiftModifier) && root.anchorId.length > 0) {
            editor.selectTreeRange(root.anchorId, id, Boolean(modifiers & Qt.ControlModifier))
            return
        }
        studio.selectObject(id, Boolean(modifiers & Qt.ControlModifier))
        root.anchorId = id
    }
    property bool dragActive: false
    property string dragLabel: ""
    property point dragPos: Qt.point(0, 0)
    // Calques et groupes repliés (clé « kind:id ») ; état d'affichage seulement, pas le document.
    property var collapsed: ({})
    // Lignes visibles + nombre d'objets sous chaque calque/groupe, depuis l'arbre plat (depth).
    readonly property var folded: {
        const tree = editor.tree
        const rows = []
        const counts = {}
        const stack = []
        let hideBelow = -1
        for (let i = 0; i < tree.length; ++i) {
            const r = tree[i]
            while (stack.length > 0 && stack[stack.length - 1].depth >= r.depth)
                stack.pop()
            if (r.kind === "object")
                for (const parent of stack)
                    counts[parent.key] = (counts[parent.key] || 0) + 1
            else
                stack.push({ depth: r.depth, key: r.kind + ":" + r.id })
            if (hideBelow >= 0 && r.depth > hideBelow)
                continue
            hideBelow = -1
            rows.push(r)
            if (r.kind !== "object" && root.collapsed[r.kind + ":" + r.id])
                hideBelow = r.depth
        }
        return { rows: rows, counts: counts }
    }
    function setCollapsed(key, value) {
        const next = Object.assign({}, root.collapsed)
        if (value) next[key] = true
        else delete next[key]
        root.collapsed = next
    }

    ListView {
        id: view
        objectName: "objectTreeList"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.folded.rows
        spacing: 1
        Controls.ScrollBar.vertical: Controls.ScrollBar { }

        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool isObject: modelData.kind === "object"
            readonly property bool primarySelected: isObject && modelData.selected
                                                    && studio.primarySelectionId === modelData.id
            readonly property bool isLayer: modelData.kind === "layer"
            readonly property bool isGroup: modelData.kind === "group"
            property bool renaming: false
            readonly property string foldKey: modelData.kind + ":" + modelData.id
            readonly property int childCount: isObject ? 0 : (root.folded.counts[foldKey] || 0)
            readonly property bool isCollapsed: !isObject && root.collapsed[foldKey] === true
            Accessible.description: isObject ? "" : (isCollapsed ? qsTr("replié") : qsTr("déplié"))
            Keys.onLeftPressed: if (!row.isObject) root.setCollapsed(row.foldKey, true)
            Keys.onRightPressed: if (!row.isObject) root.setCollapsed(row.foldKey, false)
            activeFocusOnTab: true
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.label
            Accessible.selected: isObject && modelData.selected
            Keys.onReturnPressed: {
                if (row.isObject) studio.selectObject(row.modelData.id)
                else if (row.isLayer) editor.selectByLayer(row.modelData.id)
                else if (row.isGroup) editor.selectGroup(row.modelData.id)
            }
            width: view.width
            height: 30

            readonly property bool isActiveLayer: isLayer && modelData.active === true
            readonly property bool dropTarget: root.dragActive && isLayer && root.dropId === modelData.id
            Rectangle {
                anchors.fill: parent
                color: row.isObject && row.modelData.selected ? Theme.accentSoft
                       : row.dropTarget ? Theme.accentSoft
                       : row.isLayer ? Theme.raised
                       : area.containsMouse || row.activeFocus ? Theme.hover : "transparent"
                border.width: row.activeFocus || row.isActiveLayer || row.dropTarget ? 1 : 0
                border.color: Theme.accent
                Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                // Calque actif : barre d'accent, comme l'objet principal de la sélection.
                Rectangle {
                    objectName: "activeLayerMarker"
                    visible: row.isActiveLayer
                    width: 3
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: Theme.accent
                }
                Rectangle {
                    width: row.primarySelected ? 4 : 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: row.primarySelected ? Theme.accent : Theme.info
                    visible: row.isObject && row.modelData.selected
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: row.isObject && row.modelData.locked ? Qt.ForbiddenCursor
                             : root.dragActive ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true // le défilement de la liste ne doit pas voler le glisser
                onCanceled: { root.dragActive = false; root.dropId = "" }
                property point pressPoint: Qt.point(0, 0)
                onPressed: function(mouse) {
                    pressPoint = Qt.point(mouse.x, mouse.y)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !(row.isObject || row.isLayer) || (pressedButtons & Qt.RightButton))
                        return
                    if (Math.abs(mouse.x - pressPoint.x) + Math.abs(mouse.y - pressPoint.y) > 8) {
                        root.dragActive = true
                        root.dragLayer = row.isLayer
                        root.dragLabel = row.modelData.label
                        const hp = mapToItem(view.contentItem, mouse.x, mouse.y)
                        const over = view.itemAt(hp.x, hp.y)
                        root.dropId = over && over.isLayer ? over.modelData.id : ""
                        root.dragPos = mapToItem(root, mouse.x, mouse.y)
                    }
                }
                onReleased: function(mouse) {
                    if (!root.dragActive)
                        return
                    root.dragActive = false
                    root.dropId = ""
                    const p = mapToItem(view.contentItem, mouse.x, mouse.y)
                    const target = view.itemAt(p.x, p.y)
                    if (!target || !target.isLayer || target.modelData.id === row.modelData.id)
                        return
                    if (row.isLayer)
                        editor.reorderLayer(row.modelData.id, root.layerDisplayIndex(target.modelData.id))
                    else
                        editor.moveObjectToLayer(row.modelData.id, target.modelData.id)
                }
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        if (row.isLayer) {
                            layerMenu.layer = row.modelData
                            layerMenu.popup()
                        }
                        return
                    }
                    if (row.isObject) {
                        root.clickObject(row.modelData.id, mouse.modifiers)
                    } else if (row.isGroup) {
                        editor.selectGroup(row.modelData.id)
                    } else if (row.isLayer) {
                        editor.selectByLayer(row.modelData.id)
                    }
                }
                onDoubleClicked: {
                    if (row.isObject || row.isLayer) {
                        row.renaming = true
                        nameEdit.forceActiveFocus()
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8 + row.modelData.depth * 14
                anchors.rightMargin: 6
                spacing: 6
                // Flèche de repli : vraie cible de clic (zone élargie), pas un simple décor.
                Text {
                    objectName: "treeFoldToggle"
                    visible: !row.isObject
                    opacity: row.childCount > 0 ? 1 : 0.3
                    text: row.isCollapsed ? "▸" : "▾"
                    color: foldArea.containsMouse ? Theme.foreground : Theme.muted
                    font.pixelSize: Theme.bodySize
                    Layout.preferredWidth: 14
                    horizontalAlignment: Text.AlignHCenter
                    MouseArea {
                        id: foldArea
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.setCollapsed(row.foldKey, !row.isCollapsed)
                    }
                    StudioToolTip {
                        visible: foldArea.containsMouse
                        text: row.isCollapsed ? qsTr("Déplier (→)") : qsTr("Replier (←)")
                    }
                }
                Rectangle {
                    visible: row.isObject
                    implicitWidth: 11
                    implicitHeight: 11
                    radius: 3
                    color: row.isObject ? row.modelData.color : "transparent"
                    border.width: 1
                    border.color: Qt.alpha(Theme.faint, 0.4)
                    opacity: row.isObject && row.modelData.visible ? 1 : 0.4
                    Behavior on opacity { NumberAnimation { duration: Theme.feedbackFastMs } }
                }
                Text {
                    Layout.fillWidth: true
                    visible: !row.renaming
                    text: row.modelData.label + (row.isObject && row.modelData.locked ? "  (verrouillé)" : "")
                          + (row.isLayer || row.isCollapsed ? "  · " + row.childCount : "")
                    // Un calque masqué reste lisible mais s'efface : l'état se voit sans icône.
                    color: row.isObject && row.modelData.selected ? Theme.foreground
                           : row.isLayer && !row.modelData.visible ? Theme.faint
                           : row.isActiveLayer ? Theme.foreground : Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    font.italic: (row.isLayer || row.isObject) && !row.modelData.visible
                    font.weight: row.isLayer ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                TextInput {
                    id: nameEdit
                    objectName: "treeRenameField"
                    Layout.fillWidth: true
                    visible: row.renaming
                    text: row.modelData.label
                    color: Theme.foreground
                    font.pixelSize: Theme.bodySize
                    selectByMouse: true
                    onAccepted: {
                        if (row.isLayer)
                            editor.renameLayer(row.modelData.id, text)
                        else
                            editor.renameObject(row.modelData.id, text)
                        row.renaming = false
                    }
                    Keys.onEscapePressed: row.renaming = false
                    onActiveFocusChanged: if (!activeFocus) row.renaming = false
                }
                StudioIcon {
                    objectName: "treeVisibilityToggle"
                    visible: row.isObject || row.isLayer
                    name: row.modelData.visible ? "eye" : "eye-off"
                    color: eyeArea.containsMouse ? Theme.foreground
                           : row.modelData.visible ? Theme.muted : Theme.faint
                    implicitWidth: 16
                    implicitHeight: 16
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: row.modelData.visible ? qsTr("Masquer") : qsTr("Afficher")
                    MouseArea {
                        id: eyeArea
                        anchors.fill: parent
                        anchors.margins: -5
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function(mouse) {
                            if (row.isLayer && (mouse.modifiers & Qt.AltModifier))
                                editor.isolateLayer(row.modelData.id)
                            else if (row.isLayer)
                                editor.setLayerVisible(row.modelData.id, !row.modelData.visible)
                            else
                                editor.setObjectVisible(row.modelData.id, !row.modelData.visible)
                        }
                    }
                    StudioToolTip {
                        visible: eyeArea.containsMouse
                        text: row.isLayer ? qsTr("Afficher / masquer — Alt+clic : isoler ce calque")
                                          : qsTr("Afficher / masquer l'objet")
                    }
                }
                StudioIcon {
                    objectName: "treeLockToggle"
                    visible: row.isObject || row.isLayer
                    name: row.modelData.locked ? "lock" : "unlock"
                    color: row.modelData.locked ? Theme.accent
                           : lockArea.containsMouse ? Theme.foreground : Theme.faint
                    opacity: row.modelData.locked || lockArea.containsMouse || row.isLayer ? 1 : 0.55
                    implicitWidth: 16
                    implicitHeight: 16
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: row.modelData.locked ? qsTr("Déverrouiller") : qsTr("Verrouiller")
                    MouseArea {
                        id: lockArea
                        anchors.fill: parent
                        anchors.margins: -5
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (row.isLayer)
                                editor.setLayerLocked(row.modelData.id, !row.modelData.locked)
                            else
                                editor.setObjectLocked(row.modelData.id, !row.modelData.locked)
                        }
                    }
                    StudioToolTip {
                        visible: lockArea.containsMouse
                        text: row.isLayer ? qsTr("Verrouiller : ses objets ne se sélectionnent ni ne se modifient")
                                          : qsTr("Verrouiller l'objet")
                    }
                }
            }
        }
    }

    // Menu contextuel d'un calque (clic droit) : toutes les actions de calque à un clic.
    Controls.Menu {
        id: layerMenu
        objectName: "layerContextMenu"
        property var layer: ({})
        readonly property string layerId: layer && layer.id ? layer.id : ""
        Controls.MenuItem { text: qsTr("Rendre actif"); onTriggered: editor.setActiveLayer(layerMenu.layerId) }
        Controls.MenuItem { text: qsTr("Sélectionner son contenu"); onTriggered: editor.selectByLayer(layerMenu.layerId) }
        Controls.MenuItem {
            text: qsTr("Déplacer la sélection ici")
            enabled: editor.selectedCount > 0 && !(layerMenu.layer && layerMenu.layer.locked)
            onTriggered: editor.moveSelectionToLayer(layerMenu.layerId)
        }
        Controls.MenuSeparator { }
        Controls.MenuItem { text: qsTr("Isoler / tout afficher"); onTriggered: editor.isolateLayer(layerMenu.layerId) }
        Controls.MenuItem {
            text: layerMenu.layer && layerMenu.layer.locked ? qsTr("Déverrouiller") : qsTr("Verrouiller")
            onTriggered: editor.setLayerLocked(layerMenu.layerId, !layerMenu.layer.locked)
        }
        Controls.MenuItem { text: qsTr("Dupliquer"); onTriggered: editor.duplicateLayer(layerMenu.layerId) }
        Controls.MenuSeparator { }
        Controls.MenuItem {
            objectName: "layerContextDelete"
            text: qsTr("Supprimer (objets gardés dans le calque voisin)")
            enabled: editor.layers.length > 1
            onTriggered: {
                const objects = layerMenu.layer ? (layerMenu.layer.objects || 0) : 0
                if (objects > 0) {
                    root.pendingDeleteLayer = { id: layerMenu.layerId,
                                                 name: layerMenu.layer.name || "",
                                                 objects: objects }
                } else {
                    editor.removeLayer(layerMenu.layerId)
                }
            }
        }
    }

    // Banner de confirmation (patron de RegionTools.qml : une question en attente plutôt qu'une
    // boîte de dialogue modale) : un calque supprimé fusionne silencieusement ses objets dans le
    // voisin et change l'ordre de couture, sans qu'aucun autre geste ne prévienne.
    Rectangle {
        objectName: "layerDeleteConfirm"
        visible: root.pendingDeleteLayer !== null
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.gap
        z: 10
        implicitHeight: confirmColumn.implicitHeight + 16
        color: Theme.accentSoft
        border.color: Theme.accent
        radius: Theme.radius
        ColumnLayout {
            id: confirmColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: root.pendingDeleteLayer
                      ? qsTr("Supprimer « %1 » : %2 objet(s) rejoindront le calque voisin, l'ordre de couture change.")
                            .arg(root.pendingDeleteLayer.name).arg(root.pendingDeleteLayer.objects)
                      : ""
                wrapMode: Text.WordWrap
                color: Theme.foreground
                font.pixelSize: Theme.smallSize
                font.family: Theme.fontFamily
            }
            RowLayout {
                StudioButton {
                    objectName: "layerDeleteConfirmYes"
                    text: qsTr("Supprimer tout de même")
                    emphasized: true
                    onClicked: {
                        editor.removeLayer(root.pendingDeleteLayer.id)
                        root.pendingDeleteLayer = null
                    }
                }
                StudioButton {
                    objectName: "layerDeleteConfirmNo"
                    text: qsTr("Annuler")
                    onClicked: root.pendingDeleteLayer = null
                }
            }
        }
    }

    // Étiquette qui suit la souris pendant un glisser-déposer.
    Rectangle {
        visible: root.dragActive
        x: root.dragPos.x + 8
        y: root.dragPos.y + 8
        width: dragText.implicitWidth + 12
        height: 22
        radius: 3
        color: Theme.accentSoft
        border.color: Theme.accent
        Text {
            id: dragText
            anchors.centerIn: parent
            text: root.dragLabel
            color: Theme.foreground
            font.pixelSize: Theme.smallSize
        }
    }
}
