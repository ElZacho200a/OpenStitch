// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "."

// Segmentation IA : un backend déclaré propose des formes, chacune est ensuite découpée par couleur
// (CIELAB). Backend absent ou en échec : repli explicite sur la segmentation couleur classique.
ColumnLayout {
    id: section
    objectName: "aiSection"
    spacing: 8

    property string engineId: ""
    property bool refine: true
    property int maxColors: 8
    property int minRegionPx: 16

    readonly property var engines: {
        const list = [
            { id: "", label: qsTr("Backend IA préféré (repli CIELAB)") },
            { id: "classical", label: qsTr("Segmentation couleur (CIELAB, sans IA)") }
        ]
        const found = imageTools.aiBackends
        for (let i = 0; i < found.length; ++i)
            list.push({ id: found[i].id, label: found[i].name + (found[i].available ? "" : " — " + found[i].state) })
        return list
    }

    SectionLabel { text: qsTr("SEGMENTATION IA") }

    Text { text: qsTr("Moteur"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily }
    StudioComboBox {
        id: engineBox
        objectName: "aiEngineBox"
        Layout.fillWidth: true
        model: section.engines
        textRole: "label"
        function syncIndex() {
            for (let i = 0; i < section.engines.length; ++i)
                if (section.engines[i].id === section.engineId) { currentIndex = i; return }
            currentIndex = 0
        }
        onModelChanged: syncIndex()
        onActivated: (index) => section.engineId = section.engines[index].id
    }

    StudioCheckBox {
        objectName: "aiRefineCheck"
        text: qsTr("Diviser chaque forme par couleur (CIELAB)")
        checked: section.refine
        onToggled: section.refine = checked
        palette.windowText: Theme.foreground
    }
    GridLayout {
        columns: 2
        Layout.fillWidth: true
        columnSpacing: 8
        Text { text: qsTr("Couleurs maximum par forme"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        StudioSpinBox { objectName: "aiMaxColors"; from: 2; to: 64; value: section.maxColors; editable: true
            onValueModified: section.maxColors = value }
        Text { text: qsTr("Taille minimale (px)"); color: Theme.foreground; font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily; Layout.fillWidth: true }
        StudioSpinBox { objectName: "aiMinRegion"; from: 1; to: 100000; value: section.minRegionPx; editable: true
            onValueModified: section.minRegionPx = value }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        StudioButton {
            objectName: "aiRunButton"
            Layout.fillWidth: true
            emphasized: true
            text: imageTools.aiBusy ? qsTr("Analyse en cours…") : qsTr("Segmenter par IA")
            enabled: imageTools.hasImage && !imageTools.busy && !imageTools.aiBusy
            disabledReason: !imageTools.hasImage ? qsTr("Ouvrez d'abord une image")
                                                 : qsTr("Un calcul est déjà en cours")
            onClicked: imageTools.applyAiSegmentation({
                engine: section.engineId,
                refine: section.refine,
                max_colors: section.maxColors,
                min_region_px: section.minRegionPx
            })
        }
        StudioButton {
            objectName: "aiCancelButton"
            visible: imageTools.aiBusy
            text: qsTr("Annuler")
            onClicked: imageTools.cancelTask()
        }
    }

    // Résultat de l'analyse, repli explicite compris (raison du repli sur CIELAB) : visible ici,
    // pas seulement dans l'onglet Segmentation.
    Text {
        objectName: "aiMessage"
        Layout.fillWidth: true
        visible: imageTools.message.length > 0
        text: imageTools.message
        wrapMode: Text.WordWrap
        color: imageTools.messageIsError ? Theme.error : Theme.success
        font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }

    SectionLabel { text: qsTr("BACKENDS IA") }
    Text {
        objectName: "aiSummary"
        Layout.fillWidth: true
        text: imageTools.aiSummary.length > 0 ? imageTools.aiSummary
              : qsTr("État inconnu : sondez les backends. La segmentation classique par couleurs reste disponible.")
        wrapMode: Text.WordWrap
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily
    }
    Repeater {
        model: imageTools.aiBackends
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: col.implicitHeight + 12
            color: Theme.surface
            border.color: modelData.available ? Theme.success : Theme.border
            Column {
                id: col
                anchors.fill: parent
                anchors.margins: 6
                Text { text: modelData.name + " — " + modelData.state; color: Theme.foreground
                    font.pixelSize: Theme.bodySize; font.family: Theme.fontFamily }
                Text { width: parent.width; text: modelData.diagnostics; wrapMode: Text.WordWrap
                    color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily }
            }
        }
    }
    Text { text: qsTr("Fichier de backends (vide : variable OPENSTITCH_AI_BACKENDS)"); wrapMode: Text.WordWrap
        Layout.fillWidth: true
        color: Theme.muted; font.pixelSize: Theme.smallSize; font.family: Theme.fontFamily }
    StudioTextField {
        objectName: "aiSettingsFile"
        Layout.fillWidth: true
        text: imageTools.aiSettingsFile
        placeholderText: qsTr("C:\\chemin\\openstitch-ai-backends.json")
        onEditingFinished: imageTools.aiSettingsFile = text
    }
    StudioButton {
        objectName: "aiProbeButton"
        text: imageTools.aiBusy ? qsTr("Sondage en cours…") : qsTr("Sonder les backends")
        enabled: !imageTools.aiBusy
        onClicked: imageTools.refreshAiBackends()
    }
}
