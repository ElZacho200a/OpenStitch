// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "../.."

// UNE barre d'options contextuelle (remplace les boutons flottants du canevas) : à gauche les
// gestes de numérisation (points, fondu, satin, guide, rupture, barreaux), puis l'indication de
// l'outil actif, à droite l'état de l'étape « points ».
Rectangle {
    id: bar
    objectName: "pointsOptionsBar"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var digitizeCtl: typeof digitizer !== "undefined" ? digitizer : null
    readonly property var satinCtl: typeof satin !== "undefined" ? satin : null
    readonly property var spaceCtl: typeof pointsSpace !== "undefined" ? pointsSpace : null
    // Canevas de l'espace : son viseur de centre (radial, spirale, concentrique) change l'aide.
    property var canvas: null
    readonly property bool compact: width < 900
    readonly property bool needleToolActive: toolCtl !== null && toolCtl.activeTool === "needle.origin"
    readonly property string stateName: spaceCtl ? spaceCtl.state : "empty"
    readonly property bool guidedTrace: digitizeCtl && digitizeCtl.mode === "guided" && satinCtl !== null
    readonly property bool breakTool: digitizeCtl !== null && digitizeCtl.mode === "break"
    readonly property bool fillGuideTool: digitizeCtl !== null && satinCtl !== null
        && (digitizeCtl.mode === "guide" || breakTool)
        && !satinCtl.isAutoSatin && satinCtl.canGuideFill
    // Remplissage pas encore directionnel : compteurs à -1, affichés comme « aucun ».
    readonly property int fillGuideTotal: !fillGuideTool ? 0
        : Math.max(0, breakTool ? satinCtl.breakLineCount : satinCtl.guideCount)
    readonly property bool busy: typeof studio !== "undefined" && studio.busy
    readonly property string hintText: {
        if (guidedTrace)
            return satinCtl.hasGuidedPath ? qsTr("Glissez les nœuds pour ajuster la ligne centrale.")
                                         : qsTr("Glissez au centre de la bande, d’une extrémité à l’autre.")
        if (digitizeCtl && digitizeCtl.mode !== "")
            return digitizeCtl.hint !== "" ? digitizeCtl.hint : qsTr("Geste actif : cliquez sur le canevas ; Échap pour terminer.")
        if (toolCtl && toolCtl.hint !== "")
            return toolCtl.hint
        var centre = canvas ? canvas.fillCenter : null
        if (centre && centre.visible)
            return centre.fixed ? qsTr("Glissez le viseur pour déplacer le centre ; double-clic : centre automatique.")
                                : qsTr("Glissez le viseur pour choisir le centre du remplissage.")
        return qsTr("Cliquez sur un fil ou choisissez un objet dans la liste.")
    }
    color: "transparent" // posée dans une carte (StudioCard) qui porte le fond
    implicitHeight: Theme.controlHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        Text {
            objectName: "pointsMode"
            text: bar.guidedTrace ? qsTr("Ligne centrale du satin")
                  : bar.fillGuideTool ? (bar.breakTool ? qsTr("Ruptures du remplissage") : qsTr("Guides du remplissage"))
                  : bar.digitizeCtl && bar.digitizeCtl.mode !== "" ? qsTr("Geste : ") + bar.digitizeCtl.mode
                  : bar.toolCtl && bar.toolCtl.activeTool === "needle.origin" ? qsTr("Point zéro")
                  : qsTr("Sélection")
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            font.weight: Font.DemiBold
            Layout.rightMargin: 8
        }

        Text {
            objectName: "pointsHint"
            Layout.fillWidth: true
            text: bar.hintText + (bar.toolCtl && bar.toolCtl.message !== "" ? "  ·  " + bar.toolCtl.message : "")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            elide: Text.ElideRight
        }

        // Saisie numérique du point zéro (mm, repère document) : le contrôleur valide, arrondit et
        // pousse UNE commande annulable ; les champs suivent annuler / rétablir (liaisons).
        Repeater {
            model: [
                { name: "needleXField", axis: "X", tip: qsTr("Point zéro, abscisse en mm") },
                { name: "needleYField", axis: "Y", tip: qsTr("Point zéro, ordonnée en mm (vers le haut)") }
            ]
            delegate: RowLayout {
                id: axisRow
                required property var modelData
                visible: bar.needleToolActive
                spacing: 4
                Text {
                    text: axisRow.modelData.axis
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                StudioTextField {
                    id: field
                    objectName: axisRow.modelData.name
                    implicitWidth: 72
                    placeholderText: axisRow.modelData.tip
                    Accessible.name: axisRow.modelData.tip
                    wheelStep: 0.1
                    wheelMin: -1000
                    wheelMax: 1000
                    validator: DoubleValidator { bottom: -1000; top: 1000; decimals: 1; notation: DoubleValidator.StandardNotation; locale: "C" }
                    readonly property real current: bar.toolCtl
                        ? (axisRow.modelData.axis === "X" ? bar.toolCtl.needleOriginXMm : bar.toolCtl.needleOriginYMm) : 0
                    text: current.toFixed(1)
                    onCurrentChanged: if (!activeFocus) text = current.toFixed(1)
                    onEditingFinished: {
                        if (!bar.toolCtl) return
                        const typed = parseFloat(text.replace(",", "."))
                        const x = axisRow.modelData.axis === "X" ? typed : bar.toolCtl.needleOriginXMm
                        const y = axisRow.modelData.axis === "Y" ? typed : bar.toolCtl.needleOriginYMm
                        if (isNaN(typed) || !bar.toolCtl.setNeedleOriginMm(x, y))
                            text = current.toFixed(1)
                    }
                }
                Text {
                    text: qsTr("mm")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }

        // Guides d'un remplissage (outil Guide ou Rupture) : nombre tracé, proposition automatique,
        // retrait du dernier ou de tous. Chaque action est UNE étape annulable.
        Text {
            objectName: "fillGuideCount"
            visible: bar.fillGuideTool
            text: !bar.fillGuideTool ? ""
                  : bar.breakTool
                    ? (bar.fillGuideTotal === 0 ? qsTr("Aucune rupture")
                       : bar.fillGuideTotal === 1 ? qsTr("1 rupture") : qsTr("%1 ruptures").arg(bar.fillGuideTotal))
                    : (bar.fillGuideTotal === 0 ? qsTr("Aucun guide")
                       : bar.fillGuideTotal === 1 ? qsTr("1 guide") : qsTr("%1 guides").arg(bar.fillGuideTotal))
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Layout.leftMargin: 8
        }
        StudioButton {
            objectName: "fillGuidesAuto"
            visible: bar.fillGuideTool && !bar.breakTool
            text: qsTr("Guides auto")
            quiet: true
            enabled: !bar.busy
            tooltip: bar.fillGuideTotal > 0
                     ? qsTr("Remplace les guides par ceux que propose la forme (annulable)")
                     : qsTr("Propose des guides d'après la forme ; glissez ensuite leurs nœuds")
            onClicked: bar.satinCtl.autoFillGuides()
        }
        StudioButton {
            objectName: "fillGuidesRemoveLast"
            visible: bar.fillGuideTool
            text: qsTr("Retirer le dernier")
            quiet: true
            enabled: !bar.busy && bar.fillGuideTotal > 0
            disabledReason: bar.breakTool ? qsTr("Aucune rupture à retirer.") : qsTr("Aucun guide à retirer.")
            tooltip: qsTr("Retire le dernier tracé ; Maj+clic sur le canevas en retire un précis")
            onClicked: bar.satinCtl.removeLastFillGuide(bar.breakTool)
        }
        StudioButton {
            objectName: "fillGuidesClear"
            visible: bar.fillGuideTool
            text: qsTr("Tout retirer")
            quiet: true
            enabled: !bar.busy && bar.fillGuideTotal > 0
            disabledReason: bar.breakTool ? qsTr("Aucune rupture à retirer.") : qsTr("Aucun guide à retirer.")
            onClicked: bar.satinCtl.clearGuides(bar.breakTool)
        }

        StudioButton {
            objectName: "needleResetButton"
            visible: bar.needleToolActive
            text: qsTr("Centre")
            quiet: true
            enabled: bar.toolCtl !== null && bar.toolCtl.needleOriginCustom
            tooltip: qsTr("Remet le point zéro au centre de la broderie")
            shortcutLabel: qsTr("Suppr")
            onClicked: bar.toolCtl.resetNeedleOrigin()
        }

        Rectangle {
            id: dot
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: bar.stateName === "upToDate" ? Theme.success
                   : bar.stateName === "stale" ? Theme.warning : Theme.faint
        }
        Text {
            objectName: "pointsStateText"
            visible: !bar.compact
            text: bar.stateName === "upToDate" ? qsTr("Points à jour")
                  : bar.stateName === "stale" ? qsTr("Formes sources manquantes") : qsTr("Pas encore de points")
            color: bar.stateName === "stale" ? Theme.warning : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Accessible.name: bar.spaceCtl ? bar.spaceCtl.summary : text
        }
    }
}
