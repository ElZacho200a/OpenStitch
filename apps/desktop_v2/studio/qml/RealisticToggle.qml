// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

// Bascule du rendu réaliste + accès aux préréglages de qualité (AnalysisController). Vue seule :
// basculer ne recalcule rien si la révision du motif est inchangée.
ColumnLayout {
    id: root
    spacing: 6
    readonly property bool ready: typeof view !== "undefined" && typeof analysis !== "undefined"

    StudioCheckBox {
        objectName: "realisticCheck"
        enabled: root.ready
        text: qsTr("Rendu réaliste") + "  (Ctrl+Shift+T)"
        checked: root.ready && view.realistic
        onToggled: view.realistic = checked
    }
    Text {
        objectName: "realisticModeLabel"
        visible: root.ready && view.realistic
        text: root.ready ? view.realisticMode : ""
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Qualité du rendu")
        color: Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: root.ready ? analysis.renderPresets : []
            delegate: StudioButton {
                required property var modelData
                text: modelData.label
                active: analysis.renderSettings.preset === modelData.id
                onClicked: analysis.applyRenderPreset(modelData.id)
            }
        }
    }

    // Réglages détaillés du rendu (V1) : fil, tissu et qualité. Chaque curseur applique sa valeur
    // 250 ms après le dernier mouvement (un rendu réaliste n'est jamais recalculé à chaque pixel).
    StudioCheckBox {
        objectName: "renderAdvancedBox"
        enabled: root.ready
        text: qsTr("Réglages du rendu")
        checked: root.advancedOpen
        onToggled: root.advancedOpen = checked
        palette.windowText: Theme.foreground
    }
    property bool advancedOpen: false
    ColumnLayout {
        objectName: "renderAdvanced"
        Layout.fillWidth: true
        visible: root.ready && root.advancedOpen
        spacing: 4
        Repeater {
            model: [
                { key: "px_per_mm", prop: "pxPerMm", label: qsTr("Résolution (px/mm)"), from: 1, to: 12, step: 0.5, dec: 1 },
                { key: "thread_mm", prop: "threadWidthMm", label: qsTr("Épaisseur du fil (mm)"), from: 0.1, to: 1, step: 0.05, dec: 2 },
                { key: "relief", prop: "relief", label: qsTr("Relief du fil"), from: 0, to: 1, step: 0.05, dec: 2 },
                { key: "sheen", prop: "sheen", label: qsTr("Brillance"), from: 0, to: 1, step: 0.05, dec: 2 },
                { key: "twist", prop: "twist", label: qsTr("Torsion"), from: 0, to: 1, step: 0.05, dec: 2 },
                { key: "shadow", prop: "shadow", label: qsTr("Ombre portée"), from: 0, to: 1, step: 0.05, dec: 2 },
                { key: "fabric_relief", prop: "fabricRelief", label: qsTr("Relief du tissu"), from: 0, to: 1, step: 0.05, dec: 2 }
            ]
            delegate: ColumnLayout {
                id: param
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: param.modelData.label
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                    Text {
                        objectName: "renderParamValue_" + param.modelData.key
                        text: Number(slider.value).toFixed(param.modelData.dec)
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
                StudioSlider {
                    id: slider
                    objectName: "renderParam_" + param.modelData.key
                    Layout.fillWidth: true
                    from: param.modelData.from
                    to: param.modelData.to
                    stepSize: param.modelData.step
                    snapMode: Controls.Slider.SnapAlways
                    value: root.ready ? analysis.renderSettings[param.modelData.prop] : from
                    Accessible.name: param.modelData.label
                    onMoved: commit.restart()
                    Timer {
                        id: commit
                        interval: 250
                        onTriggered: analysis.setRenderSetting(param.modelData.key, slider.value.toFixed(3))
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                objectName: "renderFabricSwatch"
                implicitWidth: 22
                implicitHeight: 22
                radius: 4
                border.width: 1
                border.color: Theme.outline
                color: root.ready ? analysis.renderSettings.fabricColor : Theme.raised
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Couleur du tissu")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            StudioButton {
                objectName: "renderFabricButton"
                text: qsTr("Choisir…")
                onClicked: { fabricDialog.selectedColor = analysis.renderSettings.fabricColor; fabricDialog.open() }
            }
        }
        ColorDialog {
            id: fabricDialog
            objectName: "renderFabricDialog"
            title: qsTr("Couleur du tissu")
            onAccepted: analysis.setRenderSetting("fabric", selectedColor.toString().replace("#", "").substring(0, 6))
        }
        StudioCheckBox {
            objectName: "renderWeaveBox"
            text: qsTr("Armure du tissu visible")
            checked: root.ready && analysis.renderSettings.fabricWeave
            onToggled: analysis.setRenderSetting("weave", checked ? "1" : "0")
            palette.windowText: Theme.foreground
        }
        StudioCheckBox {
            objectName: "renderQualityBox"
            text: qsTr("Haute qualité (moteur détaillé)")
            checked: root.ready && analysis.renderSettings.highQuality
            onToggled: analysis.setRenderSetting("quality", checked ? "high" : "fast")
            palette.windowText: Theme.foreground
        }
    }
}
