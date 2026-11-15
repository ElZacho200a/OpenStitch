import QtQuick
import QtQuick.Layouts

// Stitch recipe catalogue (STCH-CROS-01): OpenStitch-rendered thumbnails, search (French or
// English name), filters, the selected recipe's note, one-step apply. Presentation only: the list
// comes from InspectorController.recipes, applying goes through InspectorController.applyRecipe.
ColumnLayout {
    id: catalog
    property var ctl: null
    property string selectedId: ""
    property string query: ""
    property string filter: "all" // all | outline | fill | approximation
    // Thumbnails load once the provider is installed on this engine (no request before).
    property bool swatchesReady: false
    Component.onCompleted: swatchesReady = !!ctl && ctl.installRecipeSwatches(catalog)
    onCtlChanged: if (!swatchesReady && ctl) swatchesReady = ctl.installRecipeSwatches(catalog)
    readonly property var selected: {
        const list = ctl ? ctl.recipes : [];
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === selectedId)
                return list[i];
        return null;
    }
    readonly property var shown: {
        const list = ctl ? ctl.recipes : [];
        const q = query.trim().toLowerCase();
        return list.filter(r => {
            if (filter === "approximation" && !r.approximation)
                return false;
            if ((filter === "outline" || filter === "fill") && r.target !== filter)
                return false;
            return q.length === 0 || r.name.toLowerCase().indexOf(q) >= 0
                || r.nameEn.toLowerCase().indexOf(q) >= 0 || r.id.indexOf(q) >= 0;
        });
    }
    spacing: 8

    SectionLabel { text: qsTr("RECETTES DE POINTS") }

    StudioTextField {
        id: search
        objectName: "recipeSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Rechercher un point (français ou anglais)")
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        selectByMouse: true
        onTextChanged: catalog.query = text
        background: Rectangle {
            radius: 3
            color: Theme.raised
            border.width: 1
            border.color: search.activeFocus ? Theme.accent : Theme.border
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: [
                { key: "all", label: qsTr("Tous") },
                { key: "outline", label: qsTr("Contours") },
                { key: "fill", label: qsTr("Remplissages") },
                { key: "approximation", label: qsTr("Simulations") }
            ]
            delegate: StudioButton {
                required property var modelData
                objectName: "recipeFilter_" + modelData.key
                text: modelData.label
                textSize: Theme.captionSize
                quiet: catalog.filter !== modelData.key
                active: catalog.filter === modelData.key
                implicitHeight: 26
                onClicked: catalog.filter = modelData.key
            }
        }
    }

    Flow {
        id: grid
        objectName: "recipeGrid"
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            id: tiles
            model: catalog.shown
            delegate: Rectangle {
                id: tile
                required property var modelData
                objectName: "recipeTile_" + modelData.id
                width: Math.floor((grid.width - 2 * grid.spacing) / 3)
                height: width + 30
                radius: Theme.radius
                color: catalog.selectedId === modelData.id ? Theme.accentSoft
                     : tileArea.containsMouse ? Theme.hover : Theme.raised
                border.width: catalog.selectedId === modelData.id ? 2 : 1
                border.color: catalog.selectedId === modelData.id ? Theme.accent : Theme.border
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name

                Image {
                    anchors { top: parent.top; left: parent.left; right: parent.right; margins: 4 }
                    height: tile.width - 8
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(160, 160)
                    source: catalog.swatchesReady ? "image://recipe/" + tile.modelData.id : ""
                }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                              margins: 4 }
                    text: tile.modelData.name
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.captionSize
                }
                Rectangle {
                    visible: tile.modelData.approximation
                    anchors { top: parent.top; right: parent.right; margins: 4 }
                    width: 16; height: 16; radius: 8
                    color: Theme.warning
                    Text {
                        anchors.centerIn: parent
                        text: "≈"
                        color: Theme.panel
                        font.pixelSize: Theme.captionSize
                        font.bold: true
                    }
                }
                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: catalog.selectedId = tile.modelData.id
                    onDoubleClicked: catalog.ctl.applyRecipe(tile.modelData.id)
                }
                StudioToolTip {
                    visible: tileArea.containsMouse
                    text: tile.modelData.name + " — " + tile.modelData.nameEn
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        objectName: "noRecipe"
        visible: tiles.count === 0
        text: qsTr("(aucune recette ne correspond)")
        color: Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }

    Text {
        Layout.fillWidth: true
        objectName: "recipeNote"
        visible: catalog.selected !== null
        text: catalog.selected
              ? catalog.selected.name + " (" + catalog.selected.nameEn + ") — "
                + (catalog.selected.approximation ? qsTr("Simulation : ") : "")
                + catalog.selected.note
              : ""
        color: catalog.selected && catalog.selected.approximation ? Theme.warning : Theme.muted
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }

    StudioButton {
        objectName: "applyRecipe"
        Layout.fillWidth: true
        text: qsTr("Appliquer à la sélection")
        tooltip: qsTr("Remplace le type et les réglages de la sélection (un pas d'annulation)")
        emphasized: true
        enabled: catalog.selected !== null
        onClicked: catalog.ctl.applyRecipe(catalog.selectedId)
    }
}
