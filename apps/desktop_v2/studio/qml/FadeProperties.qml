// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

// Éditeur UNIQUE du fondu de fils, dans l'inspecteur de l'objet sélectionné.
// Ordre : fils → technique → répartition → transition → avancé. Chaque réglage d'un fondu
// existant s'applique immédiatement en UN pas d'annulation (pas de bouton « Appliquer »).
// L'axe se règle au canevas (« Ajuster l'axe ») ou par l'orientation ; la densité et la
// direction des points restent dans les réglages du remplissage.
Item {
    id: root
    property var controller
    property string prefix: "fade"
    property bool embedded: false
    readonly property bool ready: controller !== null && controller !== undefined
    readonly property bool hasFade: ready && controller.hasFade
    readonly property bool canCreate: ready && controller.canCreate
    readonly property bool satinStyle: ready && controller.style === 5
    readonly property bool crosshatchStyle: ready && controller.style === 4
    readonly property bool hasGestures: typeof digitizer !== "undefined" && digitizer !== null
    // Brouillon des fils avant création ; ensuite la palette vient du document.
    property var draftColors: []
    readonly property var colors: hasFade ? controller.colors : draftColors
    readonly property bool paletteValid: colors.length >= 2 && colors.length <= 8
        && colors.every(function(value, i) {
            return root.isHex(value) && (i === 0 || value.toLowerCase() !== colors[i - 1].toLowerCase())
        })
    property bool advancedOpen: false
    readonly property var geometries: [
        { id: 0, label: qsTr("Linéaire") },
        { id: 1, label: qsTr("Radial") },
        { id: 2, label: qsTr("Courbe") }
    ]
    implicitHeight: fields.implicitHeight + 16

    function isHex(value) { return /^#[0-9a-fA-F]{6}$/.test(String(value).trim()) }
    function withColor(index, value) {
        const next = colors.slice(0)
        next[index] = String(value).trim()
        return next
    }
    // Teinte proposée pour un nouveau fil : distincte du dernier, sans prétendre être un fil réel.
    function proposedColor(last) {
        const options = ["#2468a0", "#c9816c", "#6b9bd1", "#d9b44a"] // lint-ok: couleurs de fil proposées, pas couleurs du thème
        for (let i = 0; i < options.length; ++i)
            if (options[i] !== String(last).toLowerCase())
                return options[i]
        return "#808080" // lint-ok: couleur de fil proposée
    }
    function commitColors(next) {
        if (root.hasFade)
            root.controller.setColors(next)
        else
            root.draftColors = next
    }
    function addColor() {
        if (colors.length >= 8)
            return
        const next = colors.slice(0)
        next.push(proposedColor(next[next.length - 1]))
        commitColors(next)
    }
    function removeColor(index) {
        if (colors.length <= 2)
            return
        const next = colors.slice(0)
        next.splice(index, 1)
        commitColors(next)
    }
    function numberFromInput(value) {
        const normalized = String(value).trim().replace(",", ".")
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized))
            return NaN
        const parsed = Number(normalized)
        return isFinite(parsed) ? parsed : NaN
    }
    function refresh() {
        if (!ready)
            return
        if (!hasFade) {
            const start = controller.colors
            if (draftColors.length < 2 || draftColors[0] !== start[0])
                draftColors = start.length >= 2 ? start.slice(0) : []
        }
        if (hasFade && controller.geometry !== 1 && !angleField.activeFocus)
            angleField.text = Number(controller.angle).toFixed(1)
    }
    onControllerChanged: Qt.callLater(refresh)
    Component.onCompleted: Qt.callLater(refresh)
    Connections {
        target: root.ready ? root.controller : null
        function onChanged() { root.refresh() }
    }

    ColorDialog {
        id: colorDialog
        property int colorIndex: -1
        objectName: root.prefix + "ColorDialog"
        title: qsTr("Couleur du fil")
        onAccepted: {
            if (colorIndex >= 0)
                root.commitColors(root.withColor(colorIndex, selectedColor.toString()))
            colorIndex = -1
        }
    }

    Flickable {
        id: viewport
        anchors.fill: parent
        clip: !root.embedded
        interactive: !root.embedded
        contentWidth: width
        contentHeight: fields.implicitHeight + 12
        boundsBehavior: Flickable.StopAtBounds
        Controls.ScrollBar.vertical: Controls.ScrollBar { visible: !root.embedded }

        ColumnLayout {
            id: fields
            x: 8
            y: 8
            width: Math.max(0, viewport.width - 16)
            spacing: 8

            SectionLabel { text: qsTr("FONDU DE FILS") }

            Text {
                objectName: root.prefix + "Summary"
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                text: !root.ready ? qsTr("Outil indisponible.")
                      : root.hasFade ? qsTr("%1 fils, cousus dans l'ordre de la liste.").arg(root.colors.length)
                      : root.canCreate ? qsTr("Transition entre plusieurs fils réels sur cette forme.")
                      : root.controller.incompatibility
            }

            // --- Fils ------------------------------------------------------------------
            // Repère des couleurs seulement : pas une simulation des points ni du tissu.
            Item {
                id: ramp
                objectName: root.prefix + "Ramp"
                visible: (root.hasFade || root.canCreate) && root.paletteValid
                Layout.fillWidth: true
                Layout.preferredHeight: 18
                Accessible.name: qsTr("Aperçu des couleurs du fondu")
                Repeater {
                    model: Math.max(0, root.colors.length - 1)
                    delegate: Rectangle {
                        required property int index
                        x: index * width
                        width: ramp.width / Math.max(1, root.colors.length - 1)
                        height: ramp.height
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: root.colors[index] }
                            GradientStop { position: 1; color: root.colors[index + 1] }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: root.hasFade || root.canCreate
                Repeater {
                    model: root.colors.length
                    delegate: RowLayout {
                        id: colorRow
                        required property int index
                        Layout.fillWidth: true
                        spacing: 5
                        Rectangle {
                            objectName: root.prefix + "Swatch" + colorRow.index
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            radius: 4
                            border.color: Theme.outline
                            color: root.isHex(root.colors[colorRow.index]) ? root.colors[colorRow.index] : Theme.raised
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    colorDialog.colorIndex = colorRow.index
                                    colorDialog.selectedColor = root.colors[colorRow.index]
                                    colorDialog.open()
                                }
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Choisir la couleur du fil %1").arg(colorRow.index + 1)
                        }
                        StudioTextField {
                            objectName: root.prefix + "Color" + colorRow.index
                            Layout.fillWidth: true
                            text: root.colors[colorRow.index]
                            placeholderText: "#rrggbb"
                            invalid: !root.isHex(text)
                            onEditingFinished: {
                                if (root.isHex(text) && text.trim().toLowerCase() !== String(root.colors[colorRow.index]).toLowerCase())
                                    root.commitColors(root.withColor(colorRow.index, text))
                            }
                            Accessible.name: qsTr("Couleur hexadécimale du fil %1").arg(colorRow.index + 1)
                        }
                        StudioButton {
                            objectName: root.prefix + "RemoveColor" + colorRow.index
                            text: qsTr("×")
                            quiet: true
                            enabled: root.colors.length > 2
                            tooltip: qsTr("Retirer ce fil")
                            onClicked: root.removeColor(colorRow.index)
                        }
                    }
                }
                StudioButton {
                    objectName: root.prefix + "AddColor"
                    text: qsTr("+ Ajouter un fil")
                    Layout.fillWidth: true
                    enabled: root.colors.length < 8 && root.paletteValid
                    onClicked: root.addColor()
                }
                Text {
                    visible: !root.paletteValid
                    Layout.fillWidth: true
                    text: qsTr("Chaque fil doit avoir un code #rrggbb, différent du précédent.")
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.smallSize
                }
                Text {
                    visible: root.hasFade
                    Layout.fillWidth: true
                    text: qsTr("Fil de nuancier : choisissez-le pour cette couleur dans le panneau Fils ; le fondu conserve la référence.")
                    color: Theme.faint
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.captionSize
                }
            }

            StudioButton {
                objectName: root.prefix + "Create"
                visible: !root.hasFade && root.canCreate
                text: root.ready && root.controller.satinTarget ? qsTr("Créer le satin entrelacé")
                                                                : qsTr("Créer le fondu")
                emphasized: true
                enabled: root.paletteValid
                Layout.fillWidth: true
                onClicked: root.controller.createMulticolor(root.draftColors, true, 0, 0)
            }

            // --- Réglages d'un fondu existant -------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.hasFade

                StudioLabel { text: qsTr("Technique") }
                StudioComboBox {
                    objectName: root.prefix + "Style"
                    Layout.fillWidth: true
                    model: root.ready ? root.controller.styles : []
                    textRole: "label"
                    currentIndex: root.ready ? root.controller.style : 0
                    onActivated: index => root.controller.setStyle(index)
                    Accessible.name: qsTr("Technique de broderie du fondu")
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.satinStyle
                    StudioLabel { text: qsTr("Densité satin (mm)") }
                    StudioTextField {
                        id: satinDensityField
                        objectName: root.prefix + "SatinDensity"
                        Layout.fillWidth: true
                        text: root.ready ? Number(root.controller.satinDensityMm).toFixed(2) : ""
                        invalid: isNaN(root.numberFromInput(text))
                        onEditingFinished: root.controller.setSatinDensity(root.numberFromInput(text))
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.crosshatchStyle
                    StudioLabel { text: qsTr("Directions") }
                    StudioButton {
                        objectName: root.prefix + "Crosshatch2"
                        text: "2"
                        checkable: true
                        checked: root.ready && root.controller.crosshatchLayers === 2
                        onClicked: root.controller.setCrosshatch(2, root.controller.crosshatchAngleDeg)
                    }
                    StudioButton {
                        objectName: root.prefix + "Crosshatch3"
                        text: "3"
                        checkable: true
                        checked: root.ready && root.controller.crosshatchLayers === 3
                        onClicked: root.controller.setCrosshatch(3, root.controller.crosshatchAngleDeg)
                    }
                    StudioTextField {
                        objectName: root.prefix + "CrosshatchAngle"
                        Layout.fillWidth: true
                        text: root.ready ? Number(root.controller.crosshatchAngleDeg).toFixed(0) : ""
                        invalid: isNaN(root.numberFromInput(text))
                        onEditingFinished: root.controller.setCrosshatch(root.controller.crosshatchLayers,
                                                                         root.numberFromInput(text))
                        Accessible.name: qsTr("Angle de la première direction de hachures, en degrés")
                    }
                }

                StudioLabel { text: qsTr("Répartition des fils") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    StudioButton {
                        objectName: root.prefix + "ModeComplementary"
                        Layout.fillWidth: true
                        text: qsTr("Complémentaire")
                        checkable: true
                        checked: root.ready && root.controller.complementary
                        tooltip: qsTr("Chaque rangée est cousue par un seul fil : couverture régulière, sans surépaisseur")
                        onClicked: root.controller.setComplementary(true)
                    }
                    StudioButton {
                        objectName: root.prefix + "ModeOverlay"
                        Layout.fillWidth: true
                        text: qsTr("Superposition")
                        checkable: true
                        checked: root.ready && !root.controller.complementary
                        tooltip: qsTr("Premier fil plein, les suivants cousus par-dessus à 60 % de densité")
                        onClicked: root.controller.setComplementary(false)
                    }
                }

                // --- Transition (sans objet pour le satin, qui suit ses rails) ---------------
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: !root.satinStyle

                    SectionLabel { text: qsTr("TRANSITION") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: root.geometries
                            delegate: StudioButton {
                                required property var modelData
                                objectName: root.prefix + "Geometry" + modelData.id
                                Layout.fillWidth: true
                                text: modelData.label
                                checkable: true
                                checked: root.ready && root.controller.geometry === modelData.id
                                onClicked: root.controller.setGeometry(modelData.id)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: root.ready && root.controller.geometry !== 1
                        StudioLabel { text: qsTr("Orientation (°)") }
                        StudioTextField {
                            id: angleField
                            objectName: root.prefix + "Angle"
                            Layout.fillWidth: true
                            invalid: isNaN(root.numberFromInput(text))
                            onEditingFinished: {
                                const value = root.numberFromInput(text)
                                if (!isNaN(value) && Math.abs(value - root.controller.angle) > 1e-6)
                                    root.controller.setAngle(value)
                            }
                            Accessible.name: qsTr("Orientation du fondu en degrés, 0 = horizontal")
                        }
                        Repeater {
                            model: [0, 45, 90]
                            delegate: StudioButton {
                                required property int modelData
                                objectName: root.prefix + "Angle" + modelData
                                text: modelData + "°"
                                quiet: true
                                onClicked: root.controller.setAngle(modelData)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        StudioLabel { text: qsTr("Profil") }
                        StudioComboBox {
                            objectName: root.prefix + "Curve"
                            Layout.fillWidth: true
                            model: root.ready ? root.controller.curves : []
                            textRole: "label"
                            currentIndex: root.ready ? root.controller.curve : 0
                            onActivated: index => root.controller.setCurve(index)
                            Accessible.name: qsTr("Profil des transitions entre fils")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        StudioButton {
                            objectName: root.prefix + "EditAxis"
                            Layout.fillWidth: true
                            visible: root.hasGestures
                            text: qsTr("Ajuster l'axe au canevas")
                            checkable: true
                            checked: root.hasGestures && digitizer.mode === "fade"
                            tooltip: qsTr("Poignées sur le canevas : début, fin, déplacement et rotation de la transition")
                            onClicked: digitizer.mode = checked ? "fade" : ""
                        }
                        StudioButton {
                            objectName: root.prefix + "Reverse"
                            Layout.fillWidth: true
                            text: qsTr("Inverser")
                            tooltip: qsTr("Inverser le sens du fondu (en radial : l'ordre des fils)")
                            onClicked: root.controller.reverse()
                        }
                    }

                    StudioButton {
                        objectName: root.prefix + "Advanced"
                        visible: root.colors.length > 2 || root.advancedOpen
                        text: root.advancedOpen ? qsTr("▾ Profil de chaque transition")
                                                : qsTr("▸ Profil de chaque transition")
                        quiet: true
                        onClicked: root.advancedOpen = !root.advancedOpen
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: root.advancedOpen
                        Repeater {
                            model: root.hasFade ? Math.max(0, root.colors.length - 1) : 0
                            delegate: RowLayout {
                                id: transitionRow
                                required property int index
                                Layout.fillWidth: true
                                spacing: 5
                                StudioLabel { text: qsTr("Fil %1 → %2").arg(transitionRow.index + 1).arg(transitionRow.index + 2) }
                                StudioComboBox {
                                    objectName: root.prefix + "LayerCurve" + (transitionRow.index + 1)
                                    Layout.fillWidth: true
                                    model: root.controller.curves
                                    textRole: "label"
                                    currentIndex: Math.max(0, root.controller.layerCurve(transitionRow.index + 1))
                                    onActivated: index => root.controller.setLayerCurve(transitionRow.index + 1, index)
                                }
                            }
                        }
                    }
                }

                StudioButton {
                    objectName: root.prefix + "Remove"
                    Layout.fillWidth: true
                    text: qsTr("Revenir à un remplissage uni")
                    tooltip: qsTr("Supprime le fondu, annulable par Ctrl+Z")
                    onClicked: root.controller.remove()
                }
            }

            Text {
                objectName: root.prefix + "Message"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.ready ? root.controller.message : ""
                color: root.ready && root.controller.lastOk ? Theme.faint : Theme.warning
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.smallSize
            }
        }
    }
}
