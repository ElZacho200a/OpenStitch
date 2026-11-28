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
    signal guidedTraceRequested()
    // Guides d'un remplissage : ouvrir l'outil Guide du canevas (glisser, Maj+clic, barre d'options).
    signal fillGuideTraceRequested()
    function plural(n, one, many) { return n === 1 ? one.arg(n) : many.arg(n) }
    readonly property bool ready: controller !== undefined && controller !== null
    readonly property var actionController: typeof actions !== "undefined" ? actions : null
    readonly property bool guided: ready && controller.isGuidedSatin
    readonly property string selectedObjectId: ready ? controller.objectId : ""
    readonly property bool showNumericGuides: !guided && numericGuidesOpen && selectedObjectId !== ""
    // Remplissage guidable (directionnel ou convertible) : la gestion des guides reste visible.
    readonly property bool fillGuides: ready && !guided && controller.canGuideFill && !controller.isAutoSatin
    readonly property bool compactPresentation: !showNumericGuides && (!ready || !controller.isSatin)
    property bool numericGuidesOpen: false
    property string guidedRefusal: ""
    onSelectedObjectIdChanged: {
        guidedRefusal = ""
        numericGuidesOpen = false
    }
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
        Controls.ScrollBar.vertical: Controls.ScrollBar {}
        ColumnLayout {
            id: column
            width: parent.width - 12
            spacing: 8
            SectionLabel { text: qsTr("SATIN GUIDÉ") }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                text: panel.ready && panel.controller.guidedSatinDisabledReason !== ""
                      ? panel.controller.guidedSatinDisabledReason
                      : panel.guided && panel.controller.hasGuidedPath
                        ? qsTr("Déplacez les nœuds pour ajuster l’orientation des points.")
                      : qsTr("Tracez la ligne centrale pour faire tourner les points le long de la forme.")
            }
            StudioButton {
                objectName: "guidedSatinTraceButton"
                text: !panel.guided ? qsTr("Créer un satin guidé")
                      : panel.controller.hasGuidedPath ? qsTr("Modifier la ligne centrale")
                      : qsTr("Tracer la ligne centrale")
                enabled: panel.ready && panel.controller.guidedSatinDisabledReason === ""
                         && panel.actionController !== null && !studio.busy
                disabledReason: studio.busy ? qsTr("Recalcul des points en cours.")
                                : panel.ready ? panel.controller.guidedSatinDisabledReason : ""
                onClicked: {
                    panel.guidedRefusal = ""
                    if (!panel.guided && panel.controller.isAutoSatin) {
                        if (!panel.controller.useGuidedStrategy())
                            return
                    } else if (!panel.guided) {
                        panel.guidedRefusal = panel.actionController.run(
                            "satin.auto", { spacing_mm: "0.4", strategy: "guided_satin" })
                        if (panel.guidedRefusal !== "")
                            return
                    }
                    panel.guidedTraceRequested()
                }
            }
            Text {
                visible: panel.guidedRefusal !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.accent
                font.family: Theme.fontFamily
                text: panel.guidedRefusal
            }
            SectionLabel {
                visible: panel.ready && panel.controller.isSatin
                text: qsTr("COLONNE SATIN")
            }
            Text {
                objectName: "satinDiagnosis"
                visible: panel.ready && panel.controller.isSatin
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: !panel.ready ? "" : panel.controller.isSatin
                      ? panel.controller.railNodesA + qsTr(" nœuds (rail A), ") + panel.controller.railNodesB + qsTr(" (rail B), ") + panel.controller.rungCount + qsTr(" barreaux. ") + panel.controller.diagnosis
                      : qsTr("L'objet sélectionné n'est pas une colonne satin à rails.")
            }
            Flow {
                visible: panel.ready && panel.controller.isSatin
                Layout.fillWidth: true
                spacing: 4
                enabled: panel.ready && panel.controller.isSatin
                StudioButton { objectName: "satinInsertRung"; text: qsTr("Barreau dans le plus grand vide")
                    onClicked: panel.controller.insertRungBetween(Math.max(0, panel.controller.widestGap)) }
                StudioButton { objectName: "satinRemoveRedundant"; text: qsTr("Supprimer le barreau superflu")
                    enabled: panel.controller.redundantRungIndex >= 0
                    onClicked: panel.controller.removeRung(panel.controller.redundantRungIndex) }
            }
            SectionLabel {
                visible: panel.ready && panel.controller.isSatin
                text: qsTr("NŒUD D'UN RAIL (mm)")
            }
            RowLayout {
                visible: panel.ready && panel.controller.isSatin
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
            StudioButton {
                objectName: "numericGuidesToggle"
                visible: !panel.guided && panel.selectedObjectId !== ""
                quiet: true
                checkable: true
                checked: panel.showNumericGuides
                text: qsTr("Saisie par coordonnées")
                onClicked: panel.numericGuidesOpen = checked
            }
            SectionLabel { visible: panel.showNumericGuides; text: qsTr("GUIDE DE DIRECTION (mm)") }
            RowLayout {
                visible: panel.showNumericGuides
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
                visible: panel.showNumericGuides
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
                visible: panel.showNumericGuides
                text: qsTr("Ajouter le guide")
                enabled: panel.ready && panel.validGuidePoints()
                tooltip: qsTr("Crée un guide de direction entre deux points du modèle (mm)")
                onClicked: panel.controller.addGuide(panel.guidePoints(), false)
            }
            StudioButton {
                objectName: "addBreakLine"
                visible: panel.showNumericGuides
                text: qsTr("Ajouter la ligne de rupture")
                enabled: panel.ready && panel.validGuidePoints()
                         && panel.controller.breakLineCount >= 0
                tooltip: qsTr("Crée une ligne de rupture entre deux points du modèle (mm)")
                onClicked: panel.controller.addGuide(panel.guidePoints(), true)
            }
            SectionLabel { visible: panel.showNumericGuides || panel.fillGuides; text: qsTr("GUIDES DE DIRECTION") }
            Text {
                visible: panel.guided || panel.showNumericGuides || panel.fillGuides
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.faint
                font.family: Theme.fontFamily
                text: !panel.ready || panel.controller.guideCount < 0 ? qsTr("Remplissage non directionnel (le premier guide le convertit).")
                      : panel.guided
                        ? (panel.controller.hasGuidedPath ? qsTr("Ligne centrale tracée.") : qsTr("Aperçu automatique."))
                      : panel.controller.breakLineCount < 0
                        ? panel.plural(panel.controller.guideCount, qsTr("%1 guide de satin"), qsTr("%1 guides de satin"))
                        : panel.plural(panel.controller.guideCount, qsTr("%1 guide"), qsTr("%1 guides")) + " · "
                          + panel.plural(panel.controller.breakLineCount, qsTr("%1 rupture"), qsTr("%1 ruptures"))
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                visible: panel.guided || panel.showNumericGuides || panel.fillGuides
                StudioButton {
                    objectName: "fillGuidesTracePanel"
                    visible: panel.fillGuides
                    text: qsTr("Tracer au canevas")
                    shortcutLabel: "D"
                    tooltip: qsTr("Outil Guide : tracez, glissez un nœud pour remodeler, Maj+clic pour retirer un guide")
                    onClicked: panel.fillGuideTraceRequested()
                }
                StudioButton {
                    objectName: "fillGuidesAutoPanel"
                    visible: panel.fillGuides
                    text: panel.controller.guideCount > 0 ? qsTr("Remplacer par auto") : qsTr("Guides auto")
                    enabled: !studio.busy
                    tooltip: panel.controller.guideCount < 0
                             ? qsTr("Convertit ce tatami en remplissage directionnel et propose des guides d'après la forme (annulable)")
                             : qsTr("Propose des guides d'après la forme ; remplace les guides existants (annulable)")
                    onClicked: panel.controller.autoFillGuides()
                }
                StudioButton {
                    objectName: "guidesClear"
                    visible: !panel.guided || panel.controller.guideCount > 0
                    text: panel.guided ? qsTr("Revenir à l’aperçu automatique") : qsTr("Retirer les guides")
                    tooltip: panel.guided ? "" : qsTr("Retire tous les guides (annulable)")
                    enabled: panel.ready && panel.controller.guideCount > 0 && !studio.busy
                             && (!panel.guided || panel.controller.guidedSatinDisabledReason === "")
                    disabledReason: panel.guided ? panel.controller.guidedSatinDisabledReason
                                                 : qsTr("Aucun guide à retirer.")
                    onClicked: panel.controller.clearGuides(false)
                }
                StudioButton { objectName: "breaksClear"; text: qsTr("Retirer les ruptures")
                    visible: !panel.guided
                    enabled: panel.ready && panel.controller.breakLineCount > 0 && !studio.busy
                    disabledReason: qsTr("Aucune rupture à retirer.")
                    tooltip: qsTr("Retire toutes les ruptures (annulable)")
                    onClicked: panel.controller.clearGuides(true) }
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
