// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "../.."

// Liste des objets de broderie de l'espace « Points » : une ligne par objet (couleur, nom, type de
// points, forme source), bascule liste / arbre et calques, actions d'édition de la sélection.
ColumnLayout {
    id: panel
    objectName: "pointsObjectsPanel"
    required property var workspace
    readonly property var spaceCtl: typeof pointsSpace !== "undefined" ? pointsSpace : null
    readonly property int selectedCount: {
        var count = 0
        const entries = studio.objects
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].selected)
                ++count
        return count
    }
    function sourceOf(objectId) {
        return spaceCtl && spaceCtl.sources[objectId] ? spaceCtl.sources[objectId] : null
    }
    // Canevas de l'espace (survol d'une ligne = présélection de l'objet) et lignes compactes en
    // fenêtre basse : fournis par l'espace hôte, jamais déduits de la taille de ce panneau.
    property var canvas: null
    property bool compactRows: false
    // Filtre par nom de la vue Liste (la vue Arbre a son propre champ, dans LayerPanel).
    property string filterText: ""
    readonly property var shownObjects: {
        const needle = panel.filterText.trim().toLowerCase()
        const all = studio.objects
        if (needle.length === 0)
            return all
        return all.filter(o => String(o.label || "").toLowerCase().indexOf(needle) >= 0)
    }
    spacing: 0
    // Keep excess content inside its panel, not across the point inspector.
    clip: true

    // Défaut à la première ouverture de l'espace Points (le Loader hôte ne détruit jamais cette
    // instance : ceci ne s'applique donc qu'une fois par session, jamais par-dessus un choix
    // explicite de l'utilisateur fait ensuite) : un document à plusieurs calques s'ouvre en vue
    // Arbre, sinon la vue Liste de la V1 reste le défaut.
    Component.onCompleted: {
        if (editor.layers.length > 1)
            panel.workspace.treeMode = true
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        Layout.leftMargin: Theme.panelPadding
        Layout.rightMargin: Theme.panelPadding
        Text {
            text: qsTr("Objets de broderie")
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        Text {
            text: studio.objects.length
            color: Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodySize
        }
        StudioButton {
            visible: panel.selectedCount > 0
            iconName: "close"
            tooltip: qsTr("Effacer la sélection")
            shortcutLabel: qsTr("Échap")
            quiet: true
            implicitWidth: 28
            implicitHeight: 28
            onClicked: studio.clearSelection()
        }
    }
    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 6
        spacing: 4
        StudioButton {
            objectName: "listModeButton"
            iconName: "list"
            text: qsTr("Liste")
            implicitHeight: 28
            active: !panel.workspace.treeMode
            tooltip: qsTr("Liste à plat des objets de broderie")
            onClicked: panel.workspace.treeMode = false
        }
        StudioButton {
            objectName: "treeModeButton"
            iconName: "tree"
            text: qsTr("Arbre")
            implicitHeight: 28
            active: panel.workspace.treeMode
            tooltip: qsTr("Arbre : formes, calques et couches de couture")
            onClicked: panel.workspace.treeMode = true
        }
        Item { Layout.fillWidth: true }
        StudioButton {
            objectName: "editOpenButton"
            iconName: "edit"
            text: qsTr("Édition")
            implicitHeight: 28
            active: panel.workspace.editOpen
            tooltip: qsTr("Afficher les actions d'édition de la sélection")
            onClicked: panel.workspace.editOpen = !panel.workspace.editOpen
        }
    }

    LayerPanel {
        objectName: "layerPanel"
        visible: panel.workspace.treeMode
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: implicitHeight
    }

    StudioTextField {
        objectName: "objectSearchField"
        visible: !panel.workspace.treeMode
        Layout.fillWidth: true
        Layout.leftMargin: Theme.panelPadding
        Layout.rightMargin: Theme.panelPadding
        Layout.bottomMargin: 4
        placeholderText: qsTr("Rechercher un objet…")
        Accessible.name: qsTr("Rechercher un objet par son nom")
        onTextChanged: panel.filterText = text
        Keys.onEscapePressed: text = ""
    }

    ListView {
        id: objectList
        objectName: "objectList"
        visible: !panel.workspace.treeMode
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.shownObjects
        spacing: 1
        topMargin: 4
        bottomMargin: 4
        // Barre toujours visible : la liste dépasse presque toujours, la coupure se lit comme du défilement.
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AlwaysOn }

        delegate: Controls.ItemDelegate {
            id: objectRow
            required property var modelData
            required property int index
            readonly property var source: panel.sourceOf(modelData.id)
            readonly property bool primarySelected: modelData.selected && studio.primarySelectionId === modelData.id
            width: objectList.width
            height: panel.compactRows ? 32 : Theme.rowHeight
            leftPadding: 18
            rightPadding: 18
            hoverEnabled: true
            // Survol : l'objet est mis en évidence sur le motif (relie la ligne à la zone vue).
            onHoveredChanged: if (panel.canvas) panel.canvas.previewObject(hovered ? modelData.id : "")
            focusPolicy: Qt.StrongFocus
            Accessible.name: modelData.label
            Accessible.description: modelData.type
            Accessible.selected: modelData.selected
            onClicked: studio.selectObject(modelData.id, false)

            background: Rectangle {
                color: objectRow.modelData.selected ? Theme.accentSoft
                       : objectRow.hovered ? Theme.raised : "transparent"
                Rectangle {
                    width: objectRow.primarySelected ? 4 : 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: objectRow.primarySelected ? Theme.accent : Theme.info
                    visible: objectRow.modelData.selected
                }
                border.width: objectRow.activeFocus ? 1 : 0
                border.color: Theme.accent
                Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                Behavior on border.width { NumberAnimation { duration: Theme.feedbackFastMs } }
            }

            contentItem: RowLayout {
                spacing: 10
                Rectangle {
                    implicitWidth: 12
                    implicitHeight: 12
                    radius: 6 // ronde : ne se confond pas avec une case à cocher
                    color: objectRow.modelData.color
                    border.width: 1
                    border.color: Qt.alpha(Theme.faint, 0.4)
                    opacity: objectRow.modelData.visible ? 1 : 0.4
                    Behavior on opacity { NumberAnimation { duration: Theme.feedbackFastMs } }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: objectRow.modelData.label
                              + (panel.compactRows ? "  ·  " + objectRow.modelData.type : "")
                        color: objectRow.modelData.selected ? Theme.foreground : Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodySize
                        elide: Text.ElideRight
                    }
                    Text {
                        objectName: "objectSubtitle"
                        visible: !panel.compactRows
                        Layout.fillWidth: true
                        text: objectRow.modelData.type
                              + (objectRow.modelData.visible ? "" : qsTr(" · masqué"))
                              + (objectRow.source && objectRow.source.found && objectRow.source.label !== ""
                                 ? qsTr(" · forme « %1 »").arg(objectRow.source.label) : "")
                              + (objectRow.source && !objectRow.source.found ? qsTr(" · forme source disparue") : "")
                        color: objectRow.source && !objectRow.source.found ? Theme.warning : Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                        elide: Text.ElideRight
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: function(mouse) {
                    objectRow.forceActiveFocus()
                    studio.selectObject(objectRow.modelData.id,
                                        Boolean(mouse.modifiers & Qt.ControlModifier))
                }
            }
            StudioToolTip { delay: 700; text: modelData.label + "\n" + modelData.id }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(260, parent.width - 36)
            visible: objectList.count === 0
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: studio.commandCount > 0 ? qsTr("Aucun objet de broderie pour le moment.")
                                              : qsTr("Les objets numérisés apparaîtront ici.")
                color: Theme.muted
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
                wrapMode: Text.WordWrap
            }
            StudioButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Voir les formes vectorielles")
                tooltip: qsTr("Passer à l'espace Vecteurs sans générer de points")
                onClicked: panel.workspace.setCurrent("vector")
            }
        }
    }

    // Wrapped actions can be taller than the dock. Keep a scrollable section
    // instead of painting them over the object tree or parameter inspector.
    Flickable {
        id: editActionsScroll
        objectName: "pointsEditActionsScroll"
        visible: panel.workspace.editOpen
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: visible ? Theme.rowHeight : 0
        Layout.preferredHeight: visible ? Math.min(editActions.implicitHeight + 24,
                                                   Math.max(96, panel.height * 0.45)) : 0
        Layout.maximumHeight: Layout.preferredHeight
        clip: true
        contentWidth: width
        contentHeight: editActions.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        Controls.ScrollBar.vertical: Controls.ScrollBar { }

        EditBar {
            id: editActions
            x: 12
            y: 12
            width: Math.max(0, editActionsScroll.width - 24)
        }
    }

    Text {
        objectName: "pointsSummary"
        Layout.fillWidth: true
        Layout.leftMargin: Theme.panelPadding
        Layout.rightMargin: Theme.panelPadding
        Layout.topMargin: 4
        Layout.bottomMargin: 8
        visible: text !== ""
        text: panel.spaceCtl ? panel.spaceCtl.summary : ""
        color: panel.spaceCtl && panel.spaceCtl.state === "stale" ? Theme.warning : Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
        wrapMode: Text.WordWrap
    }
}
