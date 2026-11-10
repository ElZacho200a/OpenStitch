// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Sewing simulation controls. Pure adapter: every action is a ViewController call; the canvas
// draws the first N segments from an immutable overlay (no geometry rebuilt while scrubbing).
Rectangle {
    id: bar
    objectName: "simulationBar"
    property var controller
    implicitHeight: 74
    radius: Theme.radius
    color: Theme.panel
    border.color: Theme.divider

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            spacing: 2
            StudioButton { objectName: "simPrevColour"; enabled: bar.controller.simPosition > 0; text: qsTr("⏮ Couleur"); quiet: true; tooltip: qsTr("Début de la couleur précédente"); onClicked: bar.controller.previousColour() }
            StudioButton { objectName: "simPrevObject"; enabled: bar.controller.simPosition > 0; text: qsTr("◀ Objet"); quiet: true; tooltip: qsTr("Début de l'objet précédent"); onClicked: bar.controller.previousObject() }
            StudioButton { objectName: "simPrevStitch"; enabled: bar.controller.simPosition > 0; text: qsTr("◀ Point"); quiet: true; tooltip: qsTr("Reculer d'un point"); onClicked: bar.controller.step(-1) }
            StudioButton {
                objectName: "simPlayButton"
                emphasized: true
                text: bar.controller.playing ? qsTr("⏸ Pause") : qsTr("▶ Lecture")
                tooltip: bar.controller.playing ? qsTr("Mettre en pause") : qsTr("Lire la couture")
                onClicked: bar.controller.playing ? bar.controller.pause() : bar.controller.play()
            }
            StudioButton { objectName: "simNextStitch"; enabled: bar.controller.simPosition < bar.controller.simTotal; text: qsTr("Point ▶"); quiet: true; tooltip: qsTr("Avancer d'un point"); onClicked: bar.controller.step(1) }
            StudioButton { objectName: "simNextObject"; enabled: bar.controller.simPosition < bar.controller.simTotal; text: qsTr("Objet ▶"); quiet: true; tooltip: qsTr("Début de l'objet suivant"); onClicked: bar.controller.nextObject() }
            StudioButton { objectName: "simNextColour"; enabled: bar.controller.simPosition < bar.controller.simTotal; text: qsTr("Couleur ⏭"); quiet: true; tooltip: qsTr("Début de la couleur suivante"); onClicked: bar.controller.nextColour() }
            StudioButton { objectName: "simToEnd"; enabled: bar.controller.simPosition < bar.controller.simTotal; text: qsTr("⏹ Fin"); quiet: true; tooltip: qsTr("Aller à la fin"); onClicked: bar.controller.toEnd() }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("Vitesse"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
            StudioComboBox {
                objectName: "simSpeedBox"
                implicitWidth: 78
                model: [1, 5, 20, 60, 200]
                // A custom speed may come from project settings. Never show
                // the first preset as if it were the actual playback speed.
                displayText: "×" + bar.controller.simSpeed
                currentIndex: model.indexOf(bar.controller.simSpeed)
                onActivated: bar.controller.simSpeed = model[currentIndex]
            }
            StudioButton { objectName: "simClose"; text: qsTr("Fermer"); quiet: true; onClicked: bar.controller.stopSimulation() }
        }
        RowLayout {
            spacing: 10
            StudioSlider {
                id: slider
                objectName: "simSlider"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, bar.controller.simTotal)
                stepSize: 1
                enabled: bar.controller.simTotal > 0
                value: bar.controller.simPosition
                onMoved: bar.controller.simPosition = Math.round(value)
                Accessible.name: qsTr("Curseur temporel de la simulation")
            }
            Text {
                objectName: "simTimeLabel"
                text: bar.controller.simTimeText
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Text {
                objectName: "simEstimateLabel"
                text: bar.controller.simEstimate
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
        }
    }
}
