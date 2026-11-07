// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Feuille des raccourcis et des gestes, GÉNÉRÉE depuis `help` (table des commandes et registre des
// outils) : ajouter une commande avec un raccourci l'ajoute ici sans toucher ce fichier.
Controls.Popup {
    id: sheet
    objectName: "shortcutsSheet"
    readonly property bool hasHelp: typeof help !== "undefined" && help !== null
    modal: true
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
    parent: Controls.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(560, parent ? parent.height - 32 : 560)
    padding: 16

    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    function openSheet() { open(); closeButton.forceActiveFocus() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Raccourcis clavier et gestes")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.headingSize
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            StudioButton { id: closeButton; objectName: "shortcutsClose"; text: qsTr("Fermer"); onClicked: sheet.close() }
        }
        Controls.TextField {
            id: filter
            objectName: "shortcutsFilter"
            Layout.fillWidth: true
            placeholderText: qsTr("Filtrer…")
            Accessible.name: qsTr("Filtrer les raccourcis")
        }
        ListView {
            id: list
            objectName: "shortcutsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sheet.hasHelp ? sheet.rows(filter.text) : []
            Controls.ScrollBar.vertical: Controls.ScrollBar { }
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                Text {
                    Layout.fillWidth: true
                    text: modelData.header ? modelData.label.toUpperCase() : modelData.label
                    color: modelData.header ? Theme.faint : Theme.foreground
                    topPadding: modelData.header ? 8 : 0
                    font.family: Theme.fontFamily
                    font.pixelSize: modelData.header ? Theme.captionSize : Theme.bodySize
                    Accessible.name: text + (modelData.keys ? " : " + modelData.keys : "")
                }
                Text {
                    text: modelData.keys
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                }
            }
        }
    }

    function rows(query) {
        const needle = query.toLowerCase()
        const out = []
        const groups = help.shortcuts
        for (let g = 0; g < groups.length; ++g) {
            const items = groups[g].items.filter(i => needle === "" || (i.label + " " + i.shortcut).toLowerCase().indexOf(needle) >= 0)
            if (items.length === 0)
                continue
            out.push({ header: true, label: groups[g].group, keys: "" })
            for (let i = 0; i < items.length; ++i)
                out.push({ header: false, label: items[i].label, keys: items[i].shortcut })
        }
        const gestures = help.gestures.filter(i => needle === "" || (i.label + " " + i.gesture).toLowerCase().indexOf(needle) >= 0)
        if (gestures.length > 0) {
            out.push({ header: true, label: qsTr("Gestes"), keys: "" })
            for (let k = 0; k < gestures.length; ++k)
                out.push({ header: false, label: gestures[k].label, keys: gestures[k].gesture })
        }
        return out
    }
}
