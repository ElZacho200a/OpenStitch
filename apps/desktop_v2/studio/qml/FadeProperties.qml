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
    property int selectedTexture: 0
    property int selectedGeometry: 0
    property bool selectedComplementary: true
    property var draftColors: []
    readonly property bool multiColors: draftColors.length > 2 || (ready && controller.multiFade)
    readonly property bool satinTarget: ready && controller.satinTarget
    readonly property bool paletteValid: draftColors.length >= 2 && draftColors.length <= 8
        && draftColors.every(function(value, i) {
            return /^#[0-9a-fA-F]{6}$/.test(value)
                && (i === 0 || value.toLowerCase() !== draftColors[i - 1].toLowerCase())
        })
    readonly property var geometries: [
        { id: 0, label: qsTr("Linéaire") },
        { id: 1, label: qsTr("Radial") },
        { id: 2, label: qsTr("Courbe") }
    ]
    readonly property var textures: [
        { id: 0, label: qsTr("Lisse") },
        { id: 1, label: qsTr("Bords irréguliers") },
        { id: 2, label: qsTr("Tramé") },
        { id: 3, label: qsTr("Passé empiétant") }
    ]
    implicitHeight: fields.implicitHeight + 22

    function updateDraftColor(index, value) {
        const next = draftColors.slice()
        if (index < 0 || index >= next.length)
            return
        next[index] = value.trim()
        draftColors = next
    }
    function addDraftColor() {
        if (draftColors.length >= 8)
            return
        const next = draftColors.slice()
        if (next.length === 2)
            next[1] = colorField.text.trim()
        const last = next[next.length - 1].toLowerCase()
        next.push(last === "#6b9bd1" ? "#c9816c" : "#6b9bd1") // lint-ok: couleur de fil saisie par l'utilisateur, pas couleur du thème
        draftColors = next
    }
    function removeDraftColor() {
        if (draftColors.length <= 2)
            return
        const next = draftColors.slice()
        next.pop()
        draftColors = next
    }
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
            selectedComplementary = controller.complementary
            selectedTexture = controller.texture
            selectedGeometry = controller.geometry
            if (selectedGeometry !== 1)
                angleField.text = Number(controller.angle).toFixed(1)
            draftColors = controller.colors.slice(0)
        } else if (!colorValid || colorField.text.trim().toLowerCase() === controller.background.toString().toLowerCase()) {
            colorField.text = "#c81e1e" // lint-ok: couleur de fil saisie par l'utilisateur, pas couleur du thème
            selectedTexture = 0
            if (colorField.text === controller.background.toString().toLowerCase())
                colorField.text = "#2468a0" // lint-ok: couleur de fil saisie par l'utilisateur, pas couleur du thème
        }
        if (!hasFade) {
            selectedGeometry = 0
            const swatches = controller.colors
            const next = swatches.length >= 2 ? swatches.slice(0) : []
            // Tant que la transition n'existe pas, le champ texte est la
            // source de verite du second fil. Sinon le radial bicolore ou
            // « Ajouter un fil » reutiliserait une couleur perimee.
            if (next.length === 2)
                next[1] = colorField.text.trim()
            draftColors = next
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
        property int colorIndex: -1
        objectName: root.prefix + "ColorDialog"
        title: qsTr("Choisir le fil du fondu")
        onAccepted: {
            if (colorIndex >= 0)
                root.updateDraftColor(colorIndex, selectedColor.toString())
            else
                colorField.text = selectedColor.toString()
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
                        colorDialog.colorIndex = -1
                        colorDialog.selectedColor = root.colorValid ? colorField.text.trim() : "#c81e1e" // lint-ok: couleur de fil saisie par l'utilisateur, pas couleur du thème
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
                    text: "#c81e1e" // lint-ok: couleur de fil saisie par l'utilisateur, pas couleur du thème
                    placeholderText: "#rrggbb"
                    invalid: !root.colorValid
                    onTextChanged: {
                        if (!root.hasFade && root.draftColors.length === 2)
                            root.updateDraftColor(1, text)
                    }
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

            SectionLabel { text: qsTr("CAMAÏEU · 2 À 8 FILS") }
            Text {
                Layout.fillWidth: true
                text: qsTr("Chaque couleur correspond à une vraie couche de couture. "
                           + "Les codes RGB sont des couleurs libres, pas des références de fil.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                visible: root.multiColors
                Repeater {
                    model: root.draftColors.length
                    delegate: RowLayout {
                        required property int index
                        Layout.fillWidth: true
                        spacing: 5
                        Text {
                            text: qsTr("Fil %1").arg(index + 1)
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                        }
                        Rectangle {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            radius: 3
                            color: /^#[0-9a-fA-F]{6}$/.test(root.draftColors[index])
                                   ? root.draftColors[index] : Theme.raised
                            border.color: Theme.outline
                        }
                        StudioTextField {
                            objectName: root.prefix + "Color" + index
                            Layout.fillWidth: true
                            text: root.draftColors[index]
                            placeholderText: "#rrggbb"
                            invalid: !/^#[0-9a-fA-F]{6}$/.test(text)
                            onEditingFinished: root.updateDraftColor(index, text)
                        }
                        StudioButton {
                            objectName: root.prefix + "Pick" + index
                            text: qsTr("…")
                            tooltip: qsTr("Choisir la couleur du fil %1").arg(index + 1)
                            onClicked: {
                                colorDialog.colorIndex = index
                                colorDialog.selectedColor = root.draftColors[index]
                                colorDialog.open()
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 5
                StudioButton {
                    objectName: root.prefix + "AddColor"
                    text: qsTr("+ Ajouter un fil")
                    Layout.fillWidth: true
                    enabled: root.draftColors.length < 8 && root.paletteValid
                    onClicked: root.addDraftColor()
                }
                StudioButton {
                    objectName: root.prefix + "RemoveColor"
                    text: qsTr("−")
                    tooltip: qsTr("Retirer le dernier fil")
                    enabled: root.draftColors.length > 2
                    onClicked: root.removeDraftColor()
                }
            }
            Text {
                visible: root.multiColors && !root.paletteValid
                Layout.fillWidth: true
                text: qsTr("Chaque fil doit avoir un code #rrggbb, différent du précédent.")
                color: Theme.warning
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.smallSize
            }

            SectionLabel {
                text: root.satinTarget ? qsTr("SATIN ENTRELACÉ MULTICOLORE")
                                       : qsTr("TECHNIQUE DE FONDU")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                visible: root.satinTarget
                StudioButton {
                    objectName: root.prefix + "SatinInterlaced"
                    text: qsTr("Entrelacé")
                    checkable: true
                    checked: true
                    enabled: root.ready && (root.hasFade || root.canCreate)
                    tooltip: qsTr("Répartit les traversées entre les fils sans empiler des colonnes satin complètes")
                    onClicked: checked = true
                }
            }
            Text {
                visible: root.satinTarget
                Layout.fillWidth: true
                text: qsTr("Répartition des traversées du satin entre les fils, avec alternance "
                            + "déterministe. Chaque traversée conserve ses points de division.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: !root.satinTarget
                text: qsTr("Mélange optique entre fils distincts, différent du dégradé de densité d'un remplissage monochrome.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: !root.satinTarget
                text: qsTr("Aspect de la transition")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                visible: !root.satinTarget
                Repeater {
                    model: root.textures
                    delegate: StudioButton {
                        required property var modelData
                        objectName: root.prefix + "Texture" + modelData.id
                        text: modelData.label
                        checkable: true
                        checked: root.selectedTexture === modelData.id
                        enabled: root.ready && (root.hasFade || root.canCreate)
                        onClicked: {
                            root.selectedTexture = modelData.id
                            if (root.hasFade)
                                root.controller.setTexture(modelData.id)
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !root.satinTarget
                text: root.selectedTexture === 1
                      ? qsTr("Les limites de couleur s'imbriquent de façon irrégulière, sans bruit aléatoire.")
                      : root.selectedTexture === 2
                        ? qsTr("Un tramage de petites cellules répartit les fils. À contrôler dans la simulation.")
                      : root.selectedTexture === 3
                        ? qsTr("Points de longueurs alternées et terminaison irrégulière, pour une transition picturale.")
                        : qsTr("Transition régulière de densité entre les fils.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.captionSize
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: !root.satinTarget
                text: qsTr("Géométrie de la transition")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                visible: !root.satinTarget
                Repeater {
                    model: root.geometries
                    delegate: StudioButton {
                        required property var modelData
                        objectName: root.prefix + "Geometry" + modelData.id
                        text: modelData.label
                        checkable: true
                        checked: root.selectedGeometry === modelData.id
                        enabled: root.ready && (root.hasFade || root.canCreate)
                        onClicked: {
                            root.selectedGeometry = modelData.id
                            if (root.hasFade)
                                root.controller.setGeometry(modelData.id)
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !root.satinTarget
                text: qsTr("Répartition des couches")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Flow {
                Layout.fillWidth: true
                visible: !root.satinTarget
                spacing: 4
                StudioButton {
                    objectName: root.prefix + "ModeComplementary"
                    text: qsTr("Densités complémentaires")
                    checkable: true
                    checked: root.selectedComplementary
                    onClicked: root.selectedComplementary = true
                }
                StudioButton {
                    objectName: root.prefix + "ModeOverlay"
                    text: qsTr("Superposition")
                    checkable: true
                    checked: !root.selectedComplementary
                    onClicked: root.selectedComplementary = false
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !root.satinTarget
                text: root.selectedComplementary
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
                    text: root.satinTarget ? qsTr("Créer le satin entrelacé")
                                           : qsTr("Créer le fondu")
                    emphasized: true
                    visible: !root.hasFade
                    enabled: root.canCreate && (root.multiColors ? root.paletteValid
                                                                  : root.distinctColor)
                    Layout.fillWidth: true
                    onClicked: {
                        if (root.satinTarget || root.multiColors || root.selectedGeometry !== 0)
                            root.controller.createMulticolor(
                                !root.multiColors
                                    ? [root.controller.background.toString(), colorField.text.trim()]
                                    : root.draftColors,
                                root.selectedComplementary,
                                root.satinTarget ? 0 : root.selectedTexture,
                                root.satinTarget ? 0 : root.selectedGeometry)
                        else
                            root.controller.createWithTexture(colorField.text.trim(),
                                root.selectedComplementary, root.selectedTexture)
                    }
                }
                StudioButton {
                    objectName: root.prefix + "Edit"
                    text: qsTr("Appliquer")
                    emphasized: true
                    visible: root.hasFade
                    enabled: root.hasFade && (root.multiColors ? root.paletteValid
                                                               : root.distinctColor)
                    Layout.fillWidth: true
                    onClicked: {
                        if (root.multiColors)
                            root.controller.editMulticolor(root.draftColors,
                                root.selectedComplementary, root.selectedTexture,
                                root.selectedGeometry)
                        else
                            root.controller.edit(colorField.text.trim(), root.selectedComplementary)
                    }
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

                SectionLabel {
                    visible: !root.satinTarget
                    text: qsTr("PROFIL DE TRANSITION")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: !root.satinTarget
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
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.hasFade && root.multiColors && !root.satinTarget
                    spacing: 6
                    SectionLabel { text: qsTr("PROFIL INDIVIDUEL DE CHAQUE FIL") }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Comme dans un mélange multicouche : chaque fil peut avoir sa propre courbe de densité. Le profil global ci-dessus réinitialise toutes les couches.")
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: root.ready && root.hasFade ? root.controller.colors.length : 0
                        delegate: RowLayout {
                            id: layerRow
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                radius: 3
                                color: root.controller.colors[layerRow.index]
                                border.color: Theme.outline
                            }
                            Text {
                                text: qsTr("Fil %1").arg(layerRow.index + 1)
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.smallSize
                            }
                            StudioComboBox {
                                objectName: root.prefix + "LayerCurve" + layerRow.index
                                Layout.fillWidth: true
                                model: root.controller.curves
                                textRole: "label"
                                valueRole: "id"
                                currentIndex: root.controller.layerCurve(layerRow.index)
                                onActivated: root.controller.setLayerCurve(layerRow.index, currentIndex)
                                Accessible.name: qsTr("Profil de transition du fil %1").arg(layerRow.index + 1)
                            }
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

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !root.satinTarget && root.selectedGeometry !== 1
                    spacing: 5
                    SectionLabel { text: qsTr("ORIENTATION DU FONDU") }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("0° = horizontal, 90° = vertical. L'axe tourne autour du centre sans changer la largeur de transition.")
                        color: Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        StudioTextField {
                            id: angleField
                            objectName: root.prefix + "Angle"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Angle (°)")
                            invalid: root.hasFade && !isFinite(root.numberFromInput(text))
                            Accessible.name: qsTr("Angle du fondu en degrés")
                        }
                        StudioButton {
                            objectName: root.prefix + "AngleApply"
                            text: qsTr("Appliquer")
                            enabled: root.hasFade && isFinite(root.numberFromInput(angleField.text))
                            onClicked: root.controller.setAngle(root.numberFromInput(angleField.text))
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: [0, 45, 90, 135]
                            delegate: StudioButton {
                                required property int modelData
                                objectName: root.prefix + "Angle" + modelData
                                text: modelData + "°"
                                quiet: true
                                onClicked: {
                                    angleField.text = String(modelData)
                                    root.controller.setAngle(modelData)
                                }
                            }
                        }
                    }
                }

                StudioButton {
                    objectName: root.prefix + "Advanced"
                    visible: !root.satinTarget
                    text: root.advancedOpen ? qsTr("Masquer les coordonnées avancées")
                                           : qsTr("Coordonnées avancées…")
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: root.advancedOpen = !root.advancedOpen
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.advancedOpen && !root.satinTarget && !root.multiColors
                             && root.selectedGeometry === 0
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
