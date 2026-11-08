// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Aide : guide de prise en main, À propos, aide de l'outil actif. `mode` : "guide" | "about" |
// "tool". Texte généré par `help` (HelpController) ; fichiers récents : voir l'écran d'accueil.
Controls.Popup {
    id: dialog
    objectName: "helpDialog"
    readonly property bool hasHelp: typeof help !== "undefined" && help !== null
    property string mode: "guide"
    property string toolText: ""
    modal: true
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
    parent: Controls.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 32 : 520)
    height: Math.min(460, parent ? parent.height - 32 : 460)
    padding: 16

    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    function show(newMode, text) {
        mode = newMode
        toolText = text || ""
        open()
        closeButton.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Text {
            Layout.fillWidth: true
            text: dialog.mode === "about" ? qsTr("À propos") : dialog.mode === "tool" ? qsTr("Aide de l'outil") : qsTr("Guide de prise en main")
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.headingSize
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            objectName: "helpAbout"
            visible: dialog.mode === "about"
            Layout.fillWidth: true
            text: dialog.hasHelp ? help.aboutText : ""
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodySize
            Accessible.name: text
        }
        Text {
            objectName: "helpTool"
            visible: dialog.mode === "tool"
            Layout.fillWidth: true
            text: dialog.toolText !== "" ? dialog.toolText : qsTr("Aucune aide pour cet outil.")
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodySize
            Accessible.name: text
        }
        ListView {
            objectName: "helpGuide"
            visible: dialog.mode === "guide"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: dialog.hasHelp ? help.guide : []
            delegate: ColumnLayout {
                required property var modelData
                width: ListView.view.width
                spacing: 2
                Text {
                    text: modelData.title
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.text
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }
        Item { Layout.fillHeight: dialog.mode !== "guide" }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            StudioButton { id: closeButton; objectName: "helpClose"; text: qsTr("Fermer"); emphasized: true; onClicked: dialog.close() }
        }
    }
}
