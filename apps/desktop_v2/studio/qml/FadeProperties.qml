// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

// Edition du fondu de FILS : distincte de DensityGradient, qui peut aussi
// varier l'espacement d'un remplissage à un seul fil. Utilisée à la fois
// dans l'inspecteur et dans le tiroir de l'espace Points.
Item {
    id: root
    property var controller
    property string prefix: "fade"
    property bool embedded: false
    readonly property bool ready: controller !== null && controller !== undefined
    readonly property bool hasFade: ready && controller.hasFade
    readonly property bool canCreate: ready && controller.canCreate
    readonly property bool colorValid: /^#[0-9a-fA-F]{6}$/.test(colorField.text.trim())
    readonly property bool distinctColor: ready && colorValid
                                          && colorField.text.trim().toLowerCase() !== controller.background.toString().toLowerCase()
    property bool advancedOpen: false
    implicitHeight: fields.implicitHeight + 22

    function numberFromInput(value) {
        const normalized = value.trim().replace(",", ".")
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized))
            return NaN
        const parsed = Number(normalized)
        return isFinite(parsed) ? parsed : NaN
    }
    function refreshHandle() {
        if (!hasFade)
            return
        const axis = controller.axis
        const point = [axis.start, axis.end, axis.centre, axis.rotate][handle.currentIndex]
        if (point && point.x !== undefined && point.y !== undefined) {
            hx.text = Number(point.x).toFixed(2)
            hy.text = Number(point.y).toFixed(2)
        }
    }
    function refresh() {
        if (!ready)
            return
        if (hasFade) {
            colorField.text = controller.foreground.toString()
            complementary.checked = controller.complementary
        } else if (!colorValid || colorField.text.trim().toLowerCase() === controller.background.toString().toLowerCase()) {
            colorField.text = "#c81e1e"
            if (colorField.text === controller.background.toString().toLowerCase())
                colorField.text = "#2468a0"
        }
        refreshHandle()
    }
    onControllerChanged: Qt.callLater(refresh)
    Component.onCompleted: Qt.callLater(refresh)
    Connections {
        target: root.ready ? root.controller : null
        function onChanged() { root.refresh() }
    }

    ColorDialog {
        id: colorDialog
        objectName: root.prefix + "ColorDialog"
        title: qsTr("Choisir le fil du fondu")
        onAccepted: colorField.text = selectedColor.toString()
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

            SectionLabel { text: qsTr("FONDU DE COULEURS · FILS") }

            Text {
                objectName: root.prefix + "Summary"
                Layout.fillWidth: true
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                text: !root.ready ? qsTr("Outil indisponible.")
                      : root.hasFade ? root.controller.summary
                      : root.canCreate ? qsTr("Créez une transition entre deux fils sur la forme sélectionnée.")
                      : root.controller.incompatibility
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: qsTr("Départ")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                }
                Rectangle {
                    objectName: root.prefix + "BackgroundSwatch"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 5
                    border.color: Theme.outline
                    color: root.ready && (root.hasFade || root.canCreate)
                           ? root.controller.background : Theme.raised
                    Accessible.name: qsTr("Fil de départ, couleur du remplissage sélectionné")
                }
                Text {
                    text: qsTr("→")
                    color: Theme.faint
                    font.family: Theme.fontFamily
                }
                Rectangle {
                    objectName: root.prefix + "ForegroundSwatch"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 5
                    border.color: Theme.outline
                    color: root.colorValid ? colorField.text.trim() : Theme.raised
                    Accessible.name: qsTr("Fil d'arrivée")
                }
                Item { Layout.fillWidth: true }
                StudioButton {
                    objectName: root.prefix + "ColorPicker"
                    text: qsTr("Choisir…")
                    tooltip: qsTr("Choisir la couleur du second fil")
                    onClicked: {
                        colorDialog.selectedColor = root.colorValid ? colorField.text.trim() : "#c81e1e"
                        colorDialog.open()
                    }
                }
            }
            Rectangle {
                objectName: root.prefix + "Ramp"
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                radius: 4
                border.color: Theme.outline
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: root.ready && (root.hasFade || root.canCreate)
                                                    ? root.controller.background : Theme.raised }
                    GradientStop { position: 1; color: root.colorValid ? colorField.text.trim() : Theme.raised }
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Repère des couleurs uniquement : le rendu des fils dépend des points et du tissu.")
                color: Theme.faint
                font.pixelSize: Theme.captionSize
                font.family: Theme.fontFamily
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Fil d'arrivée")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                }
                StudioTextField {
                    id: colorField
                    objectName: root.prefix + "ColorField"
                    Layout.fillWidth: true
                    text: "#c81e1e"
                    placeholderText: "#rrggbb"
                    invalid: !root.colorValid
                    Accessible.name: qsTr("Couleur hexadécimale du fil d'arrivée")
                }
            }
            Text {
                visible: !root.distinctColor
                text: !root.colorValid ? qsTr("Saisissez une couleur #rrggbb.")
                                      : qsTr("Choisissez un fil différent de celui du départ.")
                Layout.fillWidth: true
                color: Theme.warning
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.smallSize
            }

            SectionLabel { text: qsTr("TECHNIQUE DE FONDU") }
            Text {
                Layout.fillWidth: true
                text: qsTr("Variation de densité entre deux fils. À distinguer du dégradé de densité d'un seul remplissage.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
            StudioCheckBox {
                id: complementary
                objectName: root.prefix + "Complementary"
                text: qsTr("Densités complémentaires")
                checked: true
            }
            Text {
                Layout.fillWidth: true
                text: complementary.checked
                      ? qsTr("Le premier fil s'espace progressivement pendant que le second se densifie.")
                      : qsTr("Le premier fil reste uniforme et le second vient en superposition.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                StudioButton {
                    objectName: root.prefix + "Create"
                    text: qsTr("Créer le fondu")
                    emphasized: true
                    visible: !root.hasFade
                    enabled: root.canCreate && root.distinctColor
                    Layout.fillWidth: true
                    onClicked: root.controller.create(colorField.text.trim(), complementary.checked)
                }
                StudioButton {
                    objectName: root.prefix + "Edit"
                    text: qsTr("Appliquer")
                    emphasized: true
                    visible: root.hasFade
                    enabled: root.hasFade && root.distinctColor
                    Layout.fillWidth: true
                    onClicked: root.controller.edit(colorField.text.trim(), complementary.checked)
                }
                StudioButton {
                    objectName: root.prefix + "Remove"
                    text: qsTr("Supprimer")
                    tooltip: qsTr("Revenir à un remplissage uni, annulable par Ctrl+Z")
                    visible: root.hasFade
                    onClicked: root.controller.remove()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.hasFade
                spacing: 7

                SectionLabel { text: qsTr("PROFIL DE TRANSITION") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: root.ready ? root.controller.curves : []
                        delegate: StudioButton {
                            required property var modelData
                            objectName: root.prefix + "Curve" + modelData.id
                            text: modelData.label
                            checkable: true
                            checked: root.hasFade && root.controller.curve === modelData.id
                            onClicked: root.controller.setCurve(modelData.id)
                        }
                    }
                }
                StudioButton {
                    objectName: root.prefix + "Reverse"
                    text: qsTr("Inverser le sens")
                    tooltip: qsTr("Échanger le début et la fin de la transition, en une opération annulable")
                    Layout.fillWidth: true
                    onClicked: root.controller.reverse()
                }

                StudioButton {
                    objectName: root.prefix + "Advanced"
                    text: root.advancedOpen ? qsTr("Masquer les coordonnées avancées")
                                           : qsTr("Coordonnées avancées…")
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: root.advancedOpen = !root.advancedOpen
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.advancedOpen
                    Text {
                        text: qsTr("Modifier l'axe dans le canevas ou saisir des positions en mm.")
                        color: Theme.faint
                        font.pixelSize: Theme.smallSize
                        font.family: Theme.fontFamily
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    StudioComboBox {
                        id: handle
                        objectName: root.prefix + "Handle"
                        Layout.fillWidth: true
                        model: [qsTr("Début"), qsTr("Fin"), qsTr("Déplacer"), qsTr("Rotation")]
                        onActivated: root.refreshHandle()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        StudioTextField {
                            id: hx
                            objectName: root.prefix + "HandleX"
                            Layout.fillWidth: true
                            placeholderText: qsTr("X (mm)")
                            invalid: root.hasFade && !isFinite(root.numberFromInput(text))
                            Accessible.name: qsTr("Abscisse en millimètres")
                        }
                        StudioTextField {
                            id: hy
                            objectName: root.prefix + "HandleY"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Y (mm)")
                            invalid: root.hasFade && !isFinite(root.numberFromInput(text))
                            Accessible.name: qsTr("Ordonnée en millimètres")
                        }
                    }
                    StudioButton {
                        objectName: root.prefix + "HandleApply"
                        text: qsTr("Déplacer la poignée")
                        Layout.fillWidth: true
                        enabled: root.hasFade && isFinite(root.numberFromInput(hx.text))
                                 && isFinite(root.numberFromInput(hy.text))
                        onClicked: root.controller.commitHandle(
                                       handle.currentIndex, root.numberFromInput(hx.text),
                                       root.numberFromInput(hy.text), false)
                    }
                }
            }
            Text {
                objectName: root.prefix + "Message"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.ready ? root.controller.message : ""
                color: root.ready && root.controller.lastOk ? Theme.success : Theme.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
        }
    }
}
