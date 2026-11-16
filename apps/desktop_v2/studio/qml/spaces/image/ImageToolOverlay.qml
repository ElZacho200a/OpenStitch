// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../.."

// Outils modaux de l'espace Image posés sur le canevas : pipette « fond transparent » (un clic
// pousse UNE opération annulable sur le calque actif) et règle d'échelle (un segment glissé, puis
// sa longueur réelle : l'échelle de TOUS les calques change, ils restent alignés). Aucune logique
// ici : ImageSpaceController (`space`) calcule et pousse les commandes. Molette et clic droit
// restent au canevas (seul le bouton gauche est capté).
Item {
    id: overlay
    objectName: "imageToolOverlay"
    required property var space // ImageSpaceController
    property int viewTick: 0
    readonly property string tool: space ? space.tool : ""
    readonly property bool multiLayer: space !== null && space.imageLayerCount > 1
    visible: tool === "pick" || tool === "measure"

    // Segment mesuré, en pixels de l'image de travail (null tant qu'aucun segment).
    property var measureA: null
    property var measureB: null
    readonly property double measuredPx: measureA && measureB
        ? Math.hypot(measureB.x - measureA.x, measureB.y - measureA.y) : 0
    onToolChanged: { measureA = null; measureB = null }

    MouseArea {
        id: area
        objectName: "imageToolArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        property string hoverColor: ""
        onPositionChanged: function(mouse) {
            if (overlay.tool === "pick")
                hoverColor = overlay.space.colorAt(mouse.x, mouse.y)
            else if (pressed && overlay.tool === "measure")
                overlay.measureB = overlay.space.screenToImage(mouse.x, mouse.y)
        }
        onExited: hoverColor = ""
        onPressed: function(mouse) {
            if (overlay.tool === "measure") {
                overlay.measureA = overlay.space.screenToImage(mouse.x, mouse.y)
                overlay.measureB = overlay.measureA
            }
        }
        onClicked: function(mouse) {
            if (overlay.tool === "pick")
                overlay.space.makeTransparentAt(mouse.x, mouse.y)
        }
    }

    // Pastille de la couleur survolée (pipette).
    Rectangle {
        visible: overlay.tool === "pick" && area.containsMouse && area.hoverColor.length > 0
        x: area.mouseX + 14
        y: area.mouseY + 14
        width: 18
        height: 18
        radius: 9
        color: area.hoverColor.length > 0 ? area.hoverColor : "transparent"
        border.color: Theme.border
        border.width: 2
    }

    // Segment de la règle.
    Canvas {
        id: ruler
        anchors.fill: parent
        visible: overlay.tool === "measure" && overlay.measureA !== null
        property var a: { overlay.viewTick; return overlay.measureA ? overlay.space.imageToScreen(overlay.measureA.x, overlay.measureA.y) : null }
        property var b: { overlay.viewTick; return overlay.measureB ? overlay.space.imageToScreen(overlay.measureB.x, overlay.measureB.y) : null }
        onAChanged: requestPaint()
        onBChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!a || !b) return
            ctx.strokeStyle = Theme.accent
            ctx.fillStyle = Theme.accent
            ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
            ctx.beginPath(); ctx.arc(a.x, a.y, 4, 0, 2 * Math.PI); ctx.fill()
            ctx.beginPath(); ctx.arc(b.x, b.y, 4, 0, 2 * Math.PI); ctx.fill()
        }
    }

    // Options de l'outil actif, en haut du canevas (sous la barre d'affichage avant / après).
    Rectangle {
        objectName: "imageToolOptions"
        x: 0
        y: Theme.controlHeight + 20
        width: Math.min(overlay.width, optionsRow.implicitWidth + 20)
        height: Theme.controlHeight + 12
        radius: Theme.cardRadius
        color: Theme.panel
        border.color: Theme.divider
        RowLayout {
            id: optionsRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            spacing: 6

            // --- pipette ---
            StudioLabel {
                visible: overlay.tool === "pick"
                text: qsTr("Tolérance")
                tone: "foreground"
                wrapMode: Text.NoWrap
            }
            StudioSpinBox {
                objectName: "pickTolerance"
                visible: overlay.tool === "pick"
                from: 0; to: 255; editable: true
                value: overlay.space ? overlay.space.pickTolerance : 24
                Accessible.name: qsTr("Tolérance de couleur")
                onValueModified: overlay.space.pickTolerance = value
            }
            StudioCheckBox {
                objectName: "pickContiguous"
                visible: overlay.tool === "pick"
                text: qsTr("Zone contiguë")
                checked: overlay.space ? overlay.space.pickContiguous : true
                onToggled: overlay.space.pickContiguous = checked
                palette.windowText: Theme.foreground
            }
            StudioLabel {
                visible: overlay.tool === "pick"
                text: overlay.multiLayer ? qsTr("Cliquez le fond : calque actif seulement.")
                                         : qsTr("Cliquez la couleur du fond.")
                wrapMode: Text.NoWrap
            }

            // --- règle ---
            StudioLabel {
                objectName: "measureLength"
                visible: overlay.tool === "measure"
                text: overlay.measuredPx >= 1
                      ? qsTr("Segment : %1 px, soit %2 mm. Longueur réelle")
                            .arg(Math.round(overlay.measuredPx))
                            .arg((overlay.measuredPx * overlay.space.mmPerPx).toFixed(1))
                      : qsTr("Glissez sur une longueur connue de l'image.")
                tone: "foreground"
                wrapMode: Text.NoWrap
            }
            StudioTextField {
                id: realField
                objectName: "measureRealMm"
                visible: overlay.tool === "measure" && overlay.measuredPx >= 1
                Layout.preferredWidth: 80
                placeholderText: qsTr("mm")
                validator: DoubleValidator { bottom: 0.1; top: 5000; decimals: 2 }
                Accessible.name: qsTr("Longueur réelle du segment (mm)")
                onAccepted: applyMeasure.clicked()
            }
            StudioButton {
                id: applyMeasure
                objectName: "measureApply"
                visible: overlay.tool === "measure" && overlay.measuredPx >= 1
                text: overlay.multiLayer ? qsTr("Mettre tous les calques à l'échelle")
                                         : qsTr("Mettre à l'échelle")
                emphasized: true
                enabled: realField.acceptableInput && overlay.space && overlay.space.canResize
                tooltip: overlay.space && !overlay.space.canResize
                         ? qsTr("Des objets existent déjà : redimensionnez-les par la sélection.")
                         : qsTr("L'image prend la taille réelle mesurée ; les calques restent alignés.")
                onClicked: {
                    var value = Number.fromLocaleString(Qt.locale(), realField.text)
                    if (isNaN(value)) value = Number(realField.text)
                    if (overlay.space.scaleFromMeasure(overlay.measuredPx, value) === "") {
                        overlay.measureA = null
                        overlay.measureB = null
                        realField.text = ""
                    }
                }
            }
        }
    }
}
