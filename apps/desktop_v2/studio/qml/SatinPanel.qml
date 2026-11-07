// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Colonne satin (rails A/B, barreaux), diagnostic de satinabilité, guides de direction et lignes de
// rupture du remplissage sélectionné. Adaptateur sur SatinController ; mm modèle.
Rectangle {
    id: panel
    objectName: "satinPanel"
    property var controller
    readonly property bool ready: controller !== undefined && controller !== null
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
            SectionLabel { text: qsTr("COLONNE SATIN") }
            Text {
                objectName: "satinDiagnosis"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: !panel.ready ? "" : panel.controller.isSatin
                      ? panel.controller.railNodesA + qsTr(" nœuds (rail A), ") + panel.controller.railNodesB + qsTr(" (rail B), ") + panel.controller.rungCount + qsTr(" barreaux. ") + panel.controller.diagnosis
                      : qsTr("L'objet sélectionné n'est pas une colonne satin à rails.")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                enabled: panel.ready && panel.controller.isSatin
                StudioButton { objectName: "satinInsertRung"; text: qsTr("Barreau dans le plus grand vide")
                    onClicked: panel.controller.insertRungBetween(Math.max(0, panel.controller.widestGap)) }
                StudioButton { objectName: "satinRemoveRedundant"; text: qsTr("Supprimer le barreau superflu")
                    enabled: panel.controller.redundantRungIndex >= 0
                    onClicked: panel.controller.removeRung(panel.controller.redundantRungIndex) }
            }
            SectionLabel { text: qsTr("NŒUD D'UN RAIL (mm)") }
            RowLayout {
                enabled: panel.ready && panel.controller.isSatin
                Controls.ComboBox { id: side; objectName: "satinSide"; model: ["A", "B"] }
                Controls.SpinBox { id: node; objectName: "satinNode"; from: 0; to: 999 }
                Controls.TextField { id: nx; objectName: "satinNodeX"; Layout.preferredWidth: 56; placeholderText: "x" }
                Controls.TextField { id: ny; objectName: "satinNodeY"; Layout.preferredWidth: 56; placeholderText: "y" }
                StudioButton { objectName: "satinMoveNode"; text: qsTr("Déplacer")
                    onClicked: panel.controller.moveRailNode(side.currentText, node.value, parseFloat(nx.text), parseFloat(ny.text)) }
            }
            SectionLabel { text: qsTr("GUIDES DE DIRECTION") }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: !panel.ready || panel.controller.guideCount < 0 ? qsTr("Remplissage non directionnel (le premier guide le convertit).")
                      : panel.controller.guideCount + qsTr(" guide(s), ") + panel.controller.breakLineCount + qsTr(" ligne(s) de rupture")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                enabled: panel.ready && panel.controller.guideCount >= 0
                StudioButton { objectName: "guidesClear"; text: qsTr("Retirer les guides"); onClicked: panel.controller.clearGuides(false) }
                StudioButton { objectName: "breaksClear"; text: qsTr("Retirer les ruptures"); onClicked: panel.controller.clearGuides(true) }
            }
            Text {
                objectName: "satinMessage"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: panel.ready ? panel.controller.message : ""
                color: panel.ready && panel.controller.lastOk ? Theme.faint : Theme.accent
                font.family: Theme.fontFamily
            }
        }
    }
}
