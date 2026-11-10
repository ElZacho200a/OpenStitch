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
    function parseRailCoordinate(value) {
        const mm = String(value).trim().replace(",", ".")
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(mm))
            return NaN
        const number = Number(mm)
        return isFinite(number) ? number : NaN
    }
    function validRailCoordinate(value) { return isFinite(parseRailCoordinate(value)) }
    function validGuidePoints() {
        if (!validRailCoordinate(guideX0.text) || !validRailCoordinate(guideY0.text)
                || !validRailCoordinate(guideX1.text) || !validRailCoordinate(guideY1.text))
            return false
        return parseRailCoordinate(guideX0.text) !== parseRailCoordinate(guideX1.text)
            || parseRailCoordinate(guideY0.text) !== parseRailCoordinate(guideY1.text)
    }
    function guidePoints() {
        return [
            { x: parseRailCoordinate(guideX0.text), y: parseRailCoordinate(guideY0.text) },
            { x: parseRailCoordinate(guideX1.text), y: parseRailCoordinate(guideY1.text) }
        ]
    }
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
                StudioComboBox { id: side; objectName: "satinSide"; model: ["A", "B"] }
                StudioSpinBox {
                    id: node
                    objectName: "satinNode"
                    from: 0
                    to: panel.ready && panel.controller.isSatin
                        ? Math.max(0, (side.currentText === "A" ? panel.controller.railNodesA
                                                                  : panel.controller.railNodesB) - 1)
                        : 0
                }
                StudioTextField {
                    id: nx
                    objectName: "satinNodeX"
                    Layout.preferredWidth: 62
                    placeholderText: qsTr("X (mm)")
                    invalid: text.trim().length > 0 && !panel.validRailCoordinate(text)
                }
                StudioTextField {
                    id: ny
                    objectName: "satinNodeY"
                    Layout.preferredWidth: 62
                    placeholderText: qsTr("Y (mm)")
                    invalid: text.trim().length > 0 && !panel.validRailCoordinate(text)
                }
                StudioButton {
                    objectName: "satinMoveNode"
                    text: qsTr("Déplacer")
                    enabled: panel.ready && panel.controller.isSatin
                             && panel.validRailCoordinate(nx.text)
                             && panel.validRailCoordinate(ny.text)
                    onClicked: panel.controller.moveRailNode(side.currentText, node.value,
                        panel.parseRailCoordinate(nx.text), panel.parseRailCoordinate(ny.text))
                }
            }
            SectionLabel { text: qsTr("TRACER UN GUIDE (mm)") }
            RowLayout {
                Layout.fillWidth: true
                StudioLabel { text: qsTr("Début") }
                StudioTextField {
                    id: guideX0
                    objectName: "guideStartX"
                    Layout.fillWidth: true
                    placeholderText: qsTr("X0")
                    invalid: text.length > 0 && !panel.validRailCoordinate(text)
                }
                StudioTextField {
                    id: guideY0
                    objectName: "guideStartY"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Y0")
                    invalid: text.length > 0 && !panel.validRailCoordinate(text)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                StudioLabel { text: qsTr("Fin") }
                StudioTextField {
                    id: guideX1
                    objectName: "guideEndX"
                    Layout.fillWidth: true
                    placeholderText: qsTr("X1")
                    invalid: text.length > 0 && !panel.validRailCoordinate(text)
                }
                StudioTextField {
                    id: guideY1
                    objectName: "guideEndY"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Y1")
                    invalid: text.length > 0 && !panel.validRailCoordinate(text)
                }
            }
            StudioButton {
                objectName: "addDirectionGuide"
                text: qsTr("Ajouter le guide")
                enabled: panel.ready && panel.validGuidePoints()
                tooltip: qsTr("Crée un guide de direction entre deux points du modèle (mm)")
                onClicked: panel.controller.addGuide(panel.guidePoints(), false)
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
