// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Retouche point à point de l'objet sélectionné. Adaptateur sur PointEditController : tout est
// calculé et muté par le cœur (une commande annulable par bouton). Coordonnées en mm modèle.
Rectangle {
    id: panel
    objectName: "pointEditPanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
    // The UI accepts French decimal commas, but never passes an incomplete/invalid value
    // to PointEditController. parseFloat("0,5") is 0 and parseFloat("1mm") is 1.
    function parseOffset(value) {
        const normalized = String(value).trim().replace(",", ".")
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized))
            return NaN
        const number = Number(normalized)
        return isFinite(number) ? number : NaN
    }
    function validOffset(value) { return isFinite(parseOffset(value)) }
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: column.implicitHeight
        clip: true
        ColumnLayout {
            id: column
            width: parent.width - 12
            spacing: 8
            SectionLabel { text: qsTr("RETOUCHE DES POINTS") }
            StudioButton {
                objectName: "pointEditToggle"
                text: panel.ready && panel.controller.active ? qsTr("Quitter la retouche") : qsTr("Afficher et retoucher les points")
                checkable: true
                checked: panel.ready && panel.controller.active
                enabled: panel.ready
                onClicked: panel.controller.active = !panel.controller.active
            }
            Text {
                objectName: "pointEditState"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: !panel.ready ? "" :
                      panel.controller.state === "none" ? qsTr("Sélectionnez un objet puis activez la retouche.") :
                      panel.controller.state === "dirty" ? qsTr("Retouches périmées : la forme a changé. Effacez-les pour repartir de la génération.") :
                      panel.controller.pointCount + qsTr(" points · ") + panel.controller.editedCount + qsTr(" retouchés · ") + panel.controller.selectedCount + qsTr(" sélectionnés")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                enabled: panel.ready && panel.controller.state !== "none"
                StudioButton { objectName: "pointSelectAll"; text: qsTr("Tout sélectionner"); onClicked: panel.controller.selectAllPoints() }
                StudioButton { objectName: "pointSelectNone"; text: qsTr("Aucun"); onClicked: panel.controller.clearPointSelection() }
                StudioButton { objectName: "pointDelete"; text: qsTr("Supprimer"); onClicked: panel.controller.deleteSelected() }
                StudioButton { objectName: "pointRestore"; text: qsTr("Restaurer"); onClicked: panel.controller.restoreSelected() }
                StudioButton { objectName: "pointToJump"; text: qsTr("En saut"); onClicked: panel.controller.setJump(true) }
                StudioButton { objectName: "pointToStitch"; text: qsTr("En point cousu"); onClicked: panel.controller.setJump(false) }
                StudioButton { objectName: "pointTrim"; text: qsTr("Couper le fil après"); onClicked: panel.controller.setTrim(true) }
                StudioButton { objectName: "pointNoTrim"; text: qsTr("Ne pas couper"); onClicked: panel.controller.setTrim(false) }
                StudioButton { objectName: "pointDiscard"; text: qsTr("Abandonner les retouches"); onClicked: panel.controller.discardEdits() }
            }
            SectionLabel { text: qsTr("DÉPLACER LA SÉLECTION (mm)") }
            RowLayout {
                StudioTextField {
                    id: dx
                    objectName: "pointMoveDx"
                    text: "0.5"
                    placeholderText: qsTr("ΔX (mm)")
                    invalid: !panel.validOffset(text)
                    Layout.preferredWidth: 85
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                StudioTextField {
                    id: dy
                    objectName: "pointMoveDy"
                    text: "0"
                    placeholderText: qsTr("ΔY (mm)")
                    invalid: !panel.validOffset(text)
                    Layout.preferredWidth: 85
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                StudioButton {
                    objectName: "pointMove"
                    text: qsTr("Déplacer")
                    enabled: panel.ready && panel.controller.selectedCount > 0
                             && panel.validOffset(dx.text) && panel.validOffset(dy.text)
                             && (panel.parseOffset(dx.text) !== 0 || panel.parseOffset(dy.text) !== 0)
                    onClicked: panel.controller.moveSelected(panel.parseOffset(dx.text),
                                                             panel.parseOffset(dy.text))
                }
            }
            Text {
                objectName: "pointEditMessage"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: panel.ready ? panel.controller.message : ""
                color: panel.ready && panel.controller.lastOk ? Theme.faint : Theme.accent
                font.family: Theme.fontFamily
            }
        }
    }
}
