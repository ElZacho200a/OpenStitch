// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "."

// Barre latérale commune aux quatre espaces. Règle : elle ne porte que des OUTILS MODAUX, ceux qui
// changent ce que fait un clic sur le canevas ; les actions ponctuelles vont dans le panneau, la
// barre d'édition ou le menu contextuel. Ordre fixe partout : Sélection et Main, puis les outils de
// l'espace séparés par groupe, puis Ajuster.
//
// L'espace fournit `entries` ([{ id, icon, label, shortcut, group, caption?, captionColor?,
// disabledReason? }]) ; `caption` : étiquette courte visible sous l'icône (outils qu'une icône seule
// ne distingue pas), l'outil actif (`activeTool`,
// "" = sélection) et réagit à `toolTriggered(id)` (id "" = retour à la sélection). Recliquer
// l'outil actif ou son raccourci revient à la sélection. Les raccourcis sont des `Shortcut` QML
// actifs seulement quand l'espace est visible (`workspaceActive`).
ColumnLayout {
    id: rail
    objectName: "canvasToolRail"
    required property var canvas
    property bool workspaceActive: true
    property var entries: []
    property string activeTool: ""
    property string toolPrefix: "tool_"
    property bool showPan: true
    property string fitTooltip: qsTr("Ajuster à la fenêtre")
    signal toolTriggered(string id)
    signal fitRequested()
    spacing: 6

    readonly property bool panning: canvas !== null && canvas.panMode === true

    function trigger(id) {
        if (rail.canvas)
            rail.canvas.panMode = false
        rail.toolTriggered(rail.activeTool === id ? "" : id)
    }
    function shortcutOf(text) { return text ? text.replace("Maj+", "Shift+") : "" }
    function startsGroup(index) {
        return index > 0 && rail.entries[index].group !== rail.entries[index - 1].group
    }

    StudioButton {
        objectName: "selectToolButton"
        iconName: "select"
        tooltip: typeof studio !== "undefined" ? studio.selectionTool.label : qsTr("Sélection")
        shortcutLabel: typeof studio !== "undefined" ? studio.selectionTool.shortcut : ""
        quiet: true
        Layout.alignment: Qt.AlignHCenter
        active: !rail.panning && rail.activeTool === ""
        onClicked: {
            if (rail.canvas) rail.canvas.panMode = false
            rail.toolTriggered("")
        }
    }
    StudioButton {
        objectName: "panToolButton"
        visible: rail.showPan
        iconName: "pan"
        tooltip: typeof studio !== "undefined" ? studio.panTool.label : qsTr("Main")
        shortcutLabel: typeof studio !== "undefined" ? studio.panTool.shortcut : ""
        quiet: true
        Layout.alignment: Qt.AlignHCenter
        active: rail.panning
        onClicked: {
            rail.toolTriggered("")
            if (rail.canvas) rail.canvas.panMode = true
        }
    }

    Repeater {
        model: rail.entries
        delegate: ColumnLayout {
            id: entry
            required property var modelData
            required property int index
            spacing: 6
            Layout.alignment: Qt.AlignHCenter
            Rectangle {
                // Séparateur avant le premier outil et à chaque changement de groupe.
                visible: entry.index === 0 || rail.startsGroup(entry.index)
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                implicitWidth: 24
                implicitHeight: 1
                color: Theme.divider
            }
            StudioButton {
                objectName: rail.toolPrefix + entry.modelData.id
                iconName: entry.modelData.icon
                quiet: true
                checkable: true
                checked: !rail.panning && rail.activeTool === entry.modelData.id
                enabled: entry.modelData.enabled === undefined || entry.modelData.enabled
                tooltip: enabled || !entry.modelData.disabledReason
                         ? entry.modelData.label
                         : entry.modelData.label + " — " + entry.modelData.disabledReason
                shortcutLabel: entry.modelData.shortcut || ""
                Layout.alignment: Qt.AlignHCenter
                Accessible.name: entry.modelData.label
                Accessible.description: checked ? qsTr("Outil actif") : entry.modelData.label
                onClicked: rail.trigger(entry.modelData.id)
            }
            Text {
                objectName: rail.toolPrefix + entry.modelData.id + "_caption"
                visible: !!entry.modelData.caption
                text: entry.modelData.caption || ""
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: -4
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize - 1
                color: entry.modelData.captionColor || Theme.muted
                opacity: entry.modelData.enabled === false ? 0.5 : 1.0
            }
        }
    }

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 2
        Layout.bottomMargin: 2
        implicitWidth: 24
        implicitHeight: 1
        color: Theme.divider
    }
    StudioButton {
        objectName: "fitToolButton"
        iconName: "fit"
        tooltip: rail.fitTooltip
        shortcutLabel: "F"
        quiet: true
        Layout.alignment: Qt.AlignHCenter
        onClicked: rail.fitRequested()
    }

    Instantiator {
        model: rail.entries
        delegate: Shortcut {
            required property var modelData
            sequence: rail.shortcutOf(modelData.shortcut)
            enabled: rail.workspaceActive && sequence !== ""
                     && (modelData.enabled === undefined || modelData.enabled)
            onActivated: rail.trigger(modelData.id)
        }
    }
}
