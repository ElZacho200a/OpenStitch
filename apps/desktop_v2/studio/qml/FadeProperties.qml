// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

// Éditeur UNIQUE du fondu de fils, dans l'inspecteur de l'objet sélectionné.
// 1. Barre de fils : les fils numérotés dans l'ordre de couture (mêmes numéros que les poignées
//    du canevas) ; un fil désigné se règle dans la carte en dessous.
// 2. Style en tuiles illustrées, 3. direction en flèches, 4. transition Douce / Moyenne / Nette.
// Le reste (géométrie courbe, angle exact, profils détaillés) est replié dans « Plus de
// réglages ». Chaque réglage d'un fondu existant s'applique immédiatement en UN pas
// d'annulation. Clavier : la barre se parcourt aux flèches, Ctrl+flèche déplace le fil, Suppr le
// retire, Entrée ouvre sa couleur, + en ajoute un.
Item {
    id: root
    property var controller
    property string prefix: "fade"
    property bool embedded: false
    readonly property bool ready: controller !== null && controller !== undefined
    readonly property bool hasFade: ready && controller.hasFade
    readonly property bool canCreate: ready && controller.canCreate
    readonly property bool editing: hasFade || canCreate
    readonly property bool satinStyle: ready && controller.style === 5
    readonly property bool crosshatchStyle: ready && controller.style === 4
    readonly property bool hasGestures: typeof digitizer !== "undefined" && digitizer !== null
    // Brouillon des fils avant création ; ensuite la palette vient du document.
    property var draftColors: []
    property int draftSelected: 0
    readonly property var colors: hasFade ? controller.colors : draftColors
    readonly property var labels: hasFade ? controller.threadLabels : []
    readonly property int selected: Math.max(0, Math.min(colors.length - 1,
                                                         hasFade ? controller.selectedThread : draftSelected))
    // Un seul fil : dégradé de densité monochrome (sauf satin, qui alterne au moins deux fils).
    readonly property int minThreads: satinStyle ? 2 : 1
    readonly property bool axial: ready && (controller.geometry === 0 || controller.geometry === 2)
    readonly property bool paletteValid: colors.length >= 1 && colors.length <= 8
        && colors.every(function(value, i) {
            return root.isHex(value) && (i === 0 || value.toLowerCase() !== colors[i - 1].toLowerCase())
        })
    property bool advancedOpen: false
    // Vocabulaire montré à l'utilisateur ; le nom technique reste dans la description accessible.
    readonly property var styleTexts: [
        { name: qsTr("Lisse"), help: qsTr("Les fils se mêlent rangée par rangée : transition fondue, la plus courante.") },
        { name: qsTr("Dentelé"), help: qsTr("Limite irrégulière entre les fils, plus naturelle sur les grandes surfaces.") },
        { name: qsTr("Peint"), help: qsTr("Points de longueurs variées qui s'imbriquent, effet peinture à l'aiguille.") },
        { name: qsTr("Moucheté"), help: qsTr("Fils dispersés en petits points : transition granuleuse.") },
        { name: qsTr("Hachuré"), help: qsTr("Couches croisées dans plusieurs directions, effet gravure.") },
        { name: qsTr("Satin"), help: qsTr("Colonnes satin dont les fils alternent : pour colonnes et lettres.") }
    ]
    readonly property var directions: [
        { angle: 0, glyph: "→", name: qsTr("De gauche à droite") },
        { angle: 45, glyph: "↗", name: qsTr("Vers le haut à droite") },
        { angle: 90, glyph: "↑", name: qsTr("De bas en haut") },
        { angle: 135, glyph: "↖", name: qsTr("Vers le haut à gauche") }
    ]
    readonly property var softnessTexts: [
        { name: qsTr("Douce"), help: qsTr("Les fils se mêlent sur toute la longueur de la transition") },
        { name: qsTr("Moyenne"), help: qsTr("Mélange resserré au milieu de chaque transition") },
        { name: qsTr("Nette"), help: qsTr("Passage rapide d'un fil au suivant") }
    ]
    readonly property var geometries: [
        { id: 0, label: qsTr("Droite") },
        { id: 1, label: qsTr("Radiale") },
        { id: 2, label: qsTr("Courbe") }
    ]
    // Formes de transition proposées d'emblée (la courbe reste dans « Plus de réglages »).
    readonly property var shapes: [
        { id: 0, label: qsTr("Droite"), help: qsTr("Les fils se succèdent le long d'une direction") },
        { id: 1, label: qsTr("Radiale"), help: qsTr("Du centre vers le bord, en cercles") },
        { id: 3, label: qsTr("Spirale"), help: qsTr("Bandes de fils enroulées en spirale autour du centre") },
        { id: 4, label: qsTr("Contour"), help: qsTr("Du bord de la forme vers son cœur, en suivant le contour") }
    ]
    implicitHeight: fields.implicitHeight + 16

    function isHex(value) { return /^#[0-9a-fA-F]{6}$/.test(String(value).trim()) }
    function hexOf(c) {
        const part = v => ("0" + Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16)).slice(-2)
        return "#" + part(c.r) + part(c.g) + part(c.b)
    }
    function threadName(index) {
        const label = index < labels.length ? labels[index] : ""
        return label.length > 0 ? label : qsTr("Couleur libre")
    }
    function threadTitle(index) {
        return qsTr("Fil %1 · %2").arg(index + 1).arg(threadName(index))
    }
    function numberFromInput(value) {
        const normalized = String(value).trim().replace(",", ".")
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized))
            return NaN
        const parsed = Number(normalized)
        return isFinite(parsed) ? parsed : NaN
    }

    // --- Opérations sur les fils : document si le fondu existe, brouillon sinon -------------
    function select(index) {
        const clamped = Math.max(0, Math.min(colors.length - 1, index))
        if (hasFade)
            controller.selectedThread = clamped
        else
            draftSelected = clamped
    }
    function setColorAt(index, value) {
        if (!isHex(value))
            return
        if (hasFade) {
            controller.setThreadColor(index, String(value).trim())
            return
        }
        const next = draftColors.slice(0)
        next[index] = String(value).trim()
        draftColors = next
    }
    function insertAfter(index) {
        if (colors.length >= 8)
            return
        if (hasFade) {
            controller.insertThread(index)
            return
        }
        const next = draftColors.slice(0)
        const a = Qt.color(next[index])
        const b = index + 1 < next.length ? Qt.color(next[index + 1]) : Qt.color(a.hslLightness > 0.5 ? "#202020" : "#f0f0f0") // lint-ok: teinte de départ d'un fil ajouté
        next.splice(index + 1, 0, hexOf(Qt.rgba((a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2, 1)))
        draftColors = next
        draftSelected = index + 1
    }
    function removeAt(index) {
        if (colors.length <= minThreads)
            return
        if (hasFade) {
            controller.removeThread(index)
            return
        }
        const next = draftColors.slice(0)
        next.splice(index, 1)
        draftColors = next
        draftSelected = Math.min(index, next.length - 1)
    }
    function moveBy(index, offset) {
        const target = index + offset
        if (target < 0 || target >= colors.length)
            return
        if (hasFade) {
            controller.moveThread(index, offset)
            return
        }
        const next = draftColors.slice(0)
        const kept = next[index]
        next[index] = next[target]
        next[target] = kept
        draftColors = next
        draftSelected = target
    }
    function openColor(index) {
        colorDialog.colorIndex = index
        colorDialog.selectedColor = colors[index]
        colorDialog.open()
    }
    function chooseDirection(angle) {
        if (!axial)
            controller.setGeometry(0)
        controller.setAngle(angle)
    }
    function directionChecked(angle) {
        return ready && hasFade && axial && Math.abs(controller.angle - angle) < 0.5
    }

    function refresh() {
        if (!ready)
            return
        if (!hasFade) {
            const start = controller.colors
            if (draftColors.length < 2 || draftColors[0] !== start[0]) {
                draftColors = start.length >= 2 ? start.slice(0) : []
                draftSelected = 0
            }
        }
        if (hasFade && axial && !angleField.activeFocus)
            angleField.text = Number(controller.angle).toFixed(1)
        if (hasFade && !spacingField.activeFocus)
            spacingField.text = Number(controller.spacingMm).toFixed(2)
        if (hasFade && !stitchAngleField.activeFocus)
            stitchAngleField.text = Number(controller.stitchAngleDeg).toFixed(0)
        if (hasFade && !endDensityField.activeFocus)
            endDensityField.text = Number(controller.endDensity * 100).toFixed(0)
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
        title: qsTr("Couleur du fil %1").arg(colorIndex + 1)
        onAccepted: {
            if (colorIndex >= 0)
                root.setColorAt(colorIndex, selectedColor.toString())
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
                      : root.hasFade && root.colors.length === 1 ? qsTr("Un seul fil : dégradé de densité, de plein à clairsemé.")
                      : root.hasFade ? qsTr("%1 fils, cousus du n° 1 au n° %1.").arg(root.colors.length)
                      : root.canCreate ? qsTr("Passez d'un fil à l'autre sur cette forme : choisissez les fils, puis créez le fondu.")
                      : root.controller.incompatibility
            }

            // --- 1. Barre de fils ------------------------------------------------------------
            // Repère des couleurs seulement : pas une simulation des points ni du tissu.
            Item {
                id: threadBar
                objectName: root.prefix + "ThreadBar"
                visible: root.editing && root.colors.length >= 2
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                activeFocusOnTab: true
                // Flèches, Début/Fin, Suppr : pour la barre, pas pour l'objet (cf. Main.qml).
                readonly property bool ownsKeyboard: true
                readonly property real stopSize: 28
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Fils du fondu : %1 fils. Fil désigné : %2.")
                                     .arg(root.colors.length).arg(root.threadTitle(root.selected))
                Accessible.description: qsTr("Flèches : changer de fil. Ctrl+flèches : déplacer le fil. Entrée : sa couleur. Suppr : le retirer. Plus : ajouter un fil.")
                Keys.onPressed: event => {
                    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        const step = event.key === Qt.Key_Left ? -1 : 1
                        if (ctrl)
                            root.moveBy(root.selected, step)
                        else
                            root.select(root.selected + step)
                    } else if (event.key === Qt.Key_Home) {
                        root.select(0)
                    } else if (event.key === Qt.Key_End) {
                        root.select(root.colors.length - 1)
                    } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                        root.removeAt(root.selected)
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        root.openColor(root.selected)
                    } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Insert) {
                        root.insertAfter(root.selected)
                    } else {
                        return
                    }
                    event.accepted = true
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius
                    color: "transparent"
                    border.width: threadBar.activeFocus ? 2 : 0
                    border.color: Theme.accent
                }
                Item {
                    id: ramp
                    objectName: root.prefix + "Ramp"
                    x: threadBar.stopSize / 2
                    width: parent.width - threadBar.stopSize
                    height: 12
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: Math.max(0, root.colors.length - 1)
                        delegate: Rectangle {
                            required property int index
                            x: index * width
                            width: ramp.width / Math.max(1, root.colors.length - 1)
                            height: ramp.height
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: root.isHex(root.colors[index]) ? root.colors[index] : Theme.raised }
                                GradientStop { position: 1; color: root.isHex(root.colors[index + 1]) ? root.colors[index + 1] : Theme.raised }
                            }
                        }
                    }
                }
                Repeater {
                    model: root.colors.length
                    delegate: Controls.AbstractButton {
                        id: stop
                        required property int index
                        readonly property bool current: index === root.selected
                        readonly property color fill: root.isHex(root.colors[index]) ? root.colors[index] : Theme.raised
                        objectName: root.prefix + "Thread" + index
                        width: threadBar.stopSize
                        height: threadBar.stopSize
                        anchors.verticalCenter: parent.verticalCenter
                        x: root.colors.length > 1 ? index * (threadBar.width - width) / (root.colors.length - 1) : 0
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        onClicked: { root.select(index); threadBar.forceActiveFocus() }
                        onDoubleClicked: root.openColor(index)
                        Accessible.role: Accessible.ListItem
                        Accessible.name: root.threadTitle(index)
                        Accessible.selected: current
                        StudioToolTip { text: root.threadTitle(index) + "\n" + root.colors[index] }
                        background: Rectangle {
                            radius: width / 2
                            color: stop.fill
                            border.width: stop.current ? 3 : 2
                            border.color: stop.current ? Theme.accent : Theme.foreground
                        }
                        contentItem: Text {
                            text: String(stop.index + 1)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                            font.weight: Font.DemiBold
                            // Numéro lisible sur n'importe quel fil.
                            color: 0.2126 * stop.fill.r + 0.7152 * stop.fill.g + 0.0722 * stop.fill.b > 0.55
                                   ? "#141821" : "#ffffff" // lint-ok: contraste du numéro sur la couleur du fil
                        }
                    }
                }
            }

            // Carte du fil désigné.
            Rectangle {
                objectName: root.prefix + "ThreadCard"
                visible: threadBar.visible
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight + 16
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.divider
                ColumnLayout {
                    id: card
                    x: 8
                    y: 8
                    width: parent.width - 16
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Controls.AbstractButton {
                            objectName: root.prefix + "Swatch"
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            focusPolicy: Qt.StrongFocus
                            onClicked: root.openColor(root.selected)
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Changer la couleur du fil %1").arg(root.selected + 1)
                            background: Rectangle {
                                radius: Theme.radius
                                color: root.isHex(root.colors[root.selected]) ? root.colors[root.selected] : Theme.raised
                                border.width: parent.activeFocus ? 2 : 1
                                border.color: parent.activeFocus ? Theme.accent : Theme.outline
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                objectName: root.prefix + "ThreadTitle"
                                Layout.fillWidth: true
                                text: root.threadTitle(root.selected)
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.bodySize
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            StudioTextField {
                                id: colorField
                                objectName: root.prefix + "SelectedColor"
                                Layout.fillWidth: true
                                text: root.colors.length > 0 ? root.colors[root.selected] : ""
                                placeholderText: "#rrggbb"
                                invalid: !root.isHex(text)
                                onEditingFinished: {
                                    if (root.isHex(text) && text.trim().toLowerCase() !== String(root.colors[root.selected]).toLowerCase())
                                        root.setColorAt(root.selected, text)
                                }
                                Accessible.name: qsTr("Code couleur du fil %1, au format #rrggbb").arg(root.selected + 1)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        StudioButton {
                            objectName: root.prefix + "MoveLeft"
                            text: "◀"
                            enabled: root.selected > 0
                            tooltip: qsTr("Coudre ce fil plus tôt (Ctrl+←)")
                            onClicked: root.moveBy(root.selected, -1)
                        }
                        StudioButton {
                            objectName: root.prefix + "MoveRight"
                            text: "▶"
                            enabled: root.selected < root.colors.length - 1
                            tooltip: qsTr("Coudre ce fil plus tard (Ctrl+→)")
                            onClicked: root.moveBy(root.selected, 1)
                        }
                        StudioButton {
                            objectName: root.prefix + "AddColor"
                            Layout.fillWidth: true
                            text: qsTr("+ Fil")
                            enabled: root.colors.length < 8 && root.paletteValid
                            tooltip: qsTr("Ajouter un fil après le n° %1 (touche +)").arg(root.selected + 1)
                            onClicked: root.insertAfter(root.selected)
                        }
                        StudioButton {
                            objectName: root.prefix + "RemoveThread"
                            Layout.fillWidth: true
                            text: qsTr("Retirer")
                            enabled: root.colors.length > root.minThreads
                            disabledReason: root.minThreads > 1 ? qsTr("Le satin entrelacé garde au moins 2 fils")
                                                                : qsTr("Un fondu garde au moins 1 fil")
                            tooltip: qsTr("Retirer le fil %1 (Suppr)").arg(root.selected + 1)
                            onClicked: root.removeAt(root.selected)
                        }
                    }
                    Text {
                        visible: root.hasFade
                        Layout.fillWidth: true
                        text: qsTr("Pour un fil de nuancier, choisissez-le dans le panneau Fils : le fondu garde sa référence.")
                        color: Theme.faint
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                    }
                }
            }
            Text {
                visible: root.editing && !root.paletteValid
                Layout.fillWidth: true
                text: qsTr("Chaque fil doit avoir un code #rrggbb, différent du précédent.")
                color: Theme.warning
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.smallSize
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

            // --- Fondu depuis l'image : parts des fils lues dans l'image source -------------
            ColumnLayout {
                id: imageFade
                objectName: root.prefix + "ImageFade"
                Layout.fillWidth: true
                spacing: 6
                visible: root.editing
                readonly property double cellValue: root.numberFromInput(imageCellField.text)
                readonly property bool cellValid: !isNaN(cellValue) && cellValue >= 0.5 && cellValue <= 5.0

                StudioLabel { text: qsTr("Fondu depuis l'image") }
                RowLayout {
                    Layout.fillWidth: true
                    StudioLabel { text: qsTr("Nombre de fils") }
                    StudioSpinBox {
                        id: imageThreads
                        objectName: root.prefix + "ImageThreads"
                        from: 2; to: 8; value: 6
                        editable: true
                        Accessible.name: qsTr("Nombre de fils du fondu depuis l'image, de 2 à 8")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    StudioLabel { text: qsTr("Case de grille (mm)") }
                    StudioTextField {
                        id: imageCellField
                        objectName: root.prefix + "ImageCell"
                        Layout.fillWidth: true
                        text: "3"
                        invalid: !imageFade.cellValid
                        Accessible.name: qsTr("Taille de la case de lecture de l'image, en millimètres, de 0,5 à 5")
                        StudioToolTip { text: qsTr("Plus petite = suit mieux l'image, mais plus de coupes. 3 mm convient à la plupart des images.") }
                    }
                }
                StudioCheckBox {
                    id: imageContinuous
                    objectName: root.prefix + "ImageContinuous"
                    text: qsTr("Rangées continues (moins de coupes)")
                    checked: true
                }
                StudioButton {
                    objectName: root.prefix + "ImageFadeApply"
                    Layout.fillWidth: true
                    text: root.ready && root.controller.imageFadeBusy ? qsTr("Calcul en cours…")
                                                                       : qsTr("Fondu depuis l'image")
                    enabled: root.ready && root.controller.canImageFade
                             && !root.controller.imageFadeBusy && imageFade.cellValid
                    tooltip: root.ready && !root.controller.canImageFade
                             ? root.controller.imageFadeReason
                             : qsTr("Lit les parts de chaque fil dans l'image source ; annulable en une étape.")
                    onClicked: root.controller.fadeFromImage(imageThreads.value, imageFade.cellValue,
                                                             imageContinuous.checked)
                }
                Text {
                    objectName: root.prefix + "ImageFadeReason"
                    Layout.fillWidth: true
                    visible: root.ready && !root.controller.canImageFade
                    text: root.ready ? root.controller.imageFadeReason : ""
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }

            // --- Réglages d'un fondu existant -------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.hasFade

                // 2. Style
                StudioLabel { text: qsTr("Style") }
                GridLayout {
                    id: styleGrid
                    objectName: root.prefix + "StyleGrid"
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 4
                    rowSpacing: 4
                    Repeater {
                        model: root.ready ? root.controller.styles : []
                        delegate: Controls.AbstractButton {
                            id: tile
                            required property int index
                            required property var modelData
                            readonly property bool current: root.ready && root.controller.style === index
                            objectName: root.prefix + "Style" + index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            checkable: false
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true
                            onClicked: root.controller.setStyle(index)
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: current
                            Accessible.name: root.styleTexts[index].name
                            Accessible.description: root.styleTexts[index].help + " (" + modelData.label + ")"
                            StudioToolTip { text: root.styleTexts[index].help }
                            background: Rectangle {
                                radius: Theme.radius
                                color: tile.current ? Theme.accentSoft : tile.hovered ? Theme.hover : Theme.raised
                                border.width: tile.current || tile.activeFocus ? 2 : 1
                                border.color: tile.current || tile.activeFocus ? Theme.accent : Theme.outline
                            }
                            contentItem: ColumnLayout {
                                spacing: 3
                                FadeStylePreview {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 26
                                    styleIndex: tile.index
                                    first: root.colors.length > 0 ? root.colors[0] : Theme.raised
                                    last: root.colors.length > 0 ? root.colors[root.colors.length - 1] : Theme.foreground
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.styleTexts[tile.index].name
                                    horizontalAlignment: Text.AlignHCenter
                                    color: tile.current ? Theme.accent : Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.smallSize
                                    font.weight: tile.current ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                Text {
                    objectName: root.prefix + "StyleHelp"
                    Layout.fillWidth: true
                    text: root.ready ? root.styleTexts[Math.max(0, root.controller.style)].help : ""
                    color: Theme.faint
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.captionSize
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.satinStyle
                    StudioLabel { text: qsTr("Densité satin (mm)") }
                    StudioTextField {
                        objectName: root.prefix + "SatinDensity"
                        Layout.fillWidth: true
                        text: root.ready ? Number(root.controller.satinDensityMm).toFixed(2) : ""
                        invalid: isNaN(root.numberFromInput(text))
                        onEditingFinished: root.controller.setSatinDensity(root.numberFromInput(text))
                        Accessible.name: qsTr("Écart entre les points satin, en millimètres")
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
                        tooltip: qsTr("Deux directions croisées")
                        onClicked: root.controller.setCrosshatch(2, root.controller.crosshatchAngleDeg)
                    }
                    StudioButton {
                        objectName: root.prefix + "Crosshatch3"
                        text: "3"
                        checkable: true
                        checked: root.ready && root.controller.crosshatchLayers === 3
                        tooltip: qsTr("Trois directions croisées")
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
                RowLayout {
                    Layout.fillWidth: true
                    visible: !root.satinStyle
                    StudioLabel { text: qsTr("Espacement des rangées (mm)") }
                    StudioTextField {
                        id: spacingField
                        objectName: root.prefix + "Spacing"
                        Layout.fillWidth: true
                        invalid: isNaN(root.numberFromInput(text))
                                 || root.numberFromInput(text) < 0.1
                                 || root.numberFromInput(text) > 4.0
                        onEditingFinished: {
                            const value = root.numberFromInput(text)
                            if (!isNaN(value) && Math.abs(value - root.controller.spacingMm) > 1e-6)
                                root.controller.setSpacing(value)
                        }
                        Accessible.name: qsTr("Écart entre les rangées de points du fondu, en millimètres")
                        StudioToolTip { text: qsTr("Plus petit = plus dense. 0,40 mm est l'écart habituel d'un remplissage.") }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.colors.length === 1
                    StudioLabel { text: qsTr("Densité d'arrivée (%)") }
                    StudioTextField {
                        id: endDensityField
                        objectName: root.prefix + "EndDensity"
                        Layout.fillWidth: true
                        invalid: isNaN(root.numberFromInput(text))
                                 || root.numberFromInput(text) < 0
                                 || root.numberFromInput(text) > 100
                        onEditingFinished: {
                            const value = root.numberFromInput(text)
                            if (!isNaN(value) && Math.abs(value - root.controller.endDensity * 100) > 0.5)
                                root.controller.setEndDensity(value)
                        }
                        Accessible.name: qsTr("Densité du fil à la fin du dégradé, en pourcentage du plein")
                    }
                }

                // Fils côte à côte ou superposés.
                StudioLabel { text: qsTr("Les fils sont…") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    StudioButton {
                        objectName: root.prefix + "ModeComplementary"
                        Layout.fillWidth: true
                        text: qsTr("Côte à côte")
                        checkable: true
                        checked: root.ready && root.controller.complementary
                        tooltip: qsTr("Côte à côte (complémentaire) : chaque rangée est cousue par un seul fil, couverture régulière")
                        onClicked: root.controller.setComplementary(true)
                    }
                    StudioButton {
                        objectName: root.prefix + "ModeOverlay"
                        Layout.fillWidth: true
                        text: qsTr("Superposés")
                        checkable: true
                        checked: root.ready && !root.controller.complementary
                        tooltip: qsTr("Superposés : premier fil plein, les suivants cousus par-dessus à 60 % de densité")
                        onClicked: root.controller.setComplementary(false)
                    }
                }

                // 3. Direction et 4. transition (sans objet pour le satin, qui suit ses rails).
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: !root.satinStyle

                    StudioLabel { text: qsTr("Forme") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: root.shapes
                            delegate: StudioButton {
                                required property var modelData
                                objectName: root.prefix + "Shape" + modelData.id
                                Layout.fillWidth: true
                                text: modelData.label
                                checkable: true
                                checked: root.ready && (root.controller.geometry === modelData.id
                                                        || (modelData.id === 0 && root.controller.geometry === 2))
                                tooltip: modelData.help
                                onClicked: root.controller.setGeometry(modelData.id)
                            }
                        }
                    }

                    StudioLabel { text: root.axial ? qsTr("Direction") : qsTr("Sens") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: root.axial ? root.directions : []
                            delegate: StudioButton {
                                required property var modelData
                                objectName: root.prefix + "Direction" + modelData.angle
                                Layout.fillWidth: true
                                text: modelData.glyph
                                checkable: true
                                checked: root.directionChecked(modelData.angle)
                                tooltip: modelData.name
                                onClicked: root.chooseDirection(modelData.angle)
                            }
                        }
                        StudioButton {
                            objectName: root.prefix + "Reverse"
                            Layout.fillWidth: true
                            text: root.axial ? "⇄" : qsTr("⇄ Inverser l'ordre des fils")
                            tooltip: root.axial ? qsTr("Inverser le sens du fondu")
                                                : qsTr("Inverser l'ordre des fils (le premier passe au bout)")
                            onClicked: root.controller.reverse()
                        }
                    }

                    // Direction des POINTS, indépendante de l'axe des couleurs (hachures : leurs
                    // propres directions).
                    StudioLabel {
                        visible: !root.crosshatchStyle && !root.satinStyle
                        text: qsTr("Points")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: !root.crosshatchStyle && !root.satinStyle
                        StudioButton {
                            objectName: root.prefix + "StitchAuto"
                            Layout.fillWidth: true
                            text: qsTr("Auto")
                            checkable: true
                            checked: root.ready && !root.controller.customStitchAngle
                            tooltip: qsTr("Points perpendiculaires au sens du fondu")
                            onClicked: root.controller.setStitchAngle(false, 0)
                        }
                        StudioButton {
                            objectName: root.prefix + "StitchCustom"
                            Layout.fillWidth: true
                            text: qsTr("Angle")
                            checkable: true
                            checked: root.ready && root.controller.customStitchAngle
                            enabled: root.ready && !isNaN(root.numberFromInput(stitchAngleField.text))
                            tooltip: qsTr("Choisir l'angle des points, quel que soit le sens du fondu")
                            onClicked: root.controller.setStitchAngle(true, root.numberFromInput(stitchAngleField.text))
                        }
                        StudioTextField {
                            id: stitchAngleField
                            objectName: root.prefix + "StitchAngle"
                            Layout.fillWidth: true
                            enabled: root.ready && root.controller.customStitchAngle
                            invalid: isNaN(root.numberFromInput(text))
                            onEditingFinished: {
                                const value = root.numberFromInput(text)
                                if (!isNaN(value) && Math.abs(value - root.controller.stitchAngleDeg) > 1e-6)
                                    root.controller.setStitchAngle(true, value)
                            }
                            Accessible.name: qsTr("Angle des points en degrés, 0 = horizontal")
                        }
                    }

                    StudioLabel { text: qsTr("Transition") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: root.softnessTexts
                            delegate: StudioButton {
                                required property int index
                                required property var modelData
                                objectName: root.prefix + "Softness" + index
                                Layout.fillWidth: true
                                text: modelData.name
                                checkable: true
                                checked: root.ready && root.controller.softness === index
                                tooltip: modelData.name + " : " + modelData.help
                                onClicked: root.controller.setSoftness(index)
                            }
                        }
                    }
                    Text {
                        visible: root.ready && root.controller.softness < 0
                        Layout.fillWidth: true
                        text: qsTr("Profil personnalisé : voir « Plus de réglages ».")
                        color: Theme.faint
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                    }

                    StudioButton {
                        objectName: root.prefix + "EditAxis"
                        Layout.fillWidth: true
                        visible: root.hasGestures && root.ready && root.controller.geometry !== 4
                        text: qsTr("Régler sur le canevas")
                        checkable: true
                        checked: root.hasGestures && digitizer.mode === "fade"
                        tooltip: qsTr("Fils numérotés sur la forme : glissez le premier ou le dernier pour placer la transition")
                        onClicked: digitizer.mode = checked ? "fade" : ""
                    }

                    StudioButton {
                        objectName: root.prefix + "Advanced"
                        Layout.fillWidth: true
                        text: root.advancedOpen ? qsTr("▾ Plus de réglages") : qsTr("▸ Plus de réglages")
                        quiet: true
                        Accessible.checked: root.advancedOpen
                        onClicked: root.advancedOpen = !root.advancedOpen
                    }
                    ColumnLayout {
                        objectName: root.prefix + "AdvancedSection"
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.advancedOpen

                        StudioLabel { text: qsTr("Forme de la transition") }
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
                            visible: root.axial
                            StudioLabel { text: qsTr("Angle exact (°)") }
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
                                Accessible.name: qsTr("Orientation du fondu en degrés, 0 = de gauche à droite")
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
                                Accessible.name: qsTr("Profil de toutes les transitions entre fils")
                            }
                        }
                        Repeater {
                            model: root.hasFade && root.colors.length > 2 ? root.colors.length - 1 : 0
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
                                    Accessible.name: qsTr("Profil de la transition du fil %1 au fil %2")
                                                         .arg(transitionRow.index + 1).arg(transitionRow.index + 2)
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
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }
}
