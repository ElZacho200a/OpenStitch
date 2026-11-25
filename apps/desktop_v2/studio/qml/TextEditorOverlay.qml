// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// UX-TEXT-01 lot B : saisie du texte SUR le canevas, sans palette ni dialogue. Aucune logique
// métier : l'état (position, brouillon, hauteur) est celui de TextTool ; la validation appelle
// `TextTool.commit`, qui passe par les actions `text.add` / `text.edit` (une étape d'annulation).
//
//   Entrée                 nouvelle ligne
//   Ctrl+Entrée / Valider  valide ;  clic ailleurs : valide si le texte n'est pas vide
//   Échap / Annuler        abandonne sans rien créer
//
// Tant que l'outil Texte est actif sans saisie ouverte, une pastille d'aide sur le canevas dit quoi
// faire (le texte d'aide de l'outil vit aussi dans l'onglet « Édition », sous le pli à petite taille).
Item {
    id: root
    objectName: "textEditorOverlay"
    property var canvas: null
    property var toolController: null
    property var actionController: null
    readonly property var textTool: toolController ? toolController.text : null
    readonly property bool open: textTool !== null && textTool.editing === true
    readonly property bool hinting: !open && toolController !== null
                                    && toolController.activeTool === "text"
    property real px: 0
    property real py: 0
    // Pixels par mm du canevas : la place libre sous l'ancre est donnée en mm.
    readonly property real zoom: canvas && canvas.zoom > 0 ? canvas.zoom : 1

    // Jamais de capture de la souris sauf saisie ouverte (la zone « clic ailleurs » est dans `open`).
    visible: open || hinting
    z: 20

    function place() {
        if (!root.canvas || !root.textTool)
            return
        const p = root.canvas.modelToScreen([{ x: root.textTool.anchorX, y: root.textTool.anchorY }])
        root.px = p[0]
        root.py = p[1]
    }
    function finish() {
        if (!root.textTool)
            return
        const refusal = root.textTool.commit(field.text, root.actionController)
        if (refusal === "" && root.canvas)
            root.canvas.forceActiveFocus()
    }
    function abort() {
        if (root.textTool)
            root.textTool.cancel()
        if (root.canvas)
            root.canvas.forceActiveFocus()
    }

    onOpenChanged: {
        if (open) {
            place()
            field.text = root.textTool.draft
            field.selectAll()
            field.forceActiveFocus()
        }
    }
    Connections {
        target: root.canvas
        ignoreUnknownSignals: true
        function onViewChanged() { root.place() }
    }
    Connections {
        target: root.textTool
        ignoreUnknownSignals: true
        function onChanged() { if (root.open) root.place() }
    }

    // Pastille d'aide : visible dès que l'outil T est choisi, qu'il y ait ou non un panneau ouvert.
    Rectangle {
        id: hint
        objectName: "textToolHint"
        visible: root.hinting
        // En bas à gauche : le centre porte parfois la barre de vue, la droite la pastille
        // « Aperçu actualisé » (≈ 170 px) ; on lui laisse sa place même à 1000 x 640.
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: Math.max(180, Math.min(parent.width - 200, 460))
        height: hintLabel.implicitHeight + 16
        radius: Theme.cardRadius
        color: Theme.raised
        border.color: Theme.accent
        border.width: Theme.borderWidth
        Text {
            id: hintLabel
            anchors.fill: parent
            anchors.margins: 8
            text: root.toolController ? root.toolController.hint : ""
            color: Theme.foreground
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    // Un clic hors de l'éditeur valide (texte non vide) ou abandonne (texte vide).
    MouseArea {
        anchors.fill: parent
        enabled: root.open
        visible: root.open
        acceptedButtons: Qt.AllButtons
        onPressed: root.finish()
    }

    Rectangle {
        id: card
        objectName: "textEditorCard"
        visible: root.open
        // Sous l'ancre (le texte reste visible) ; au-dessus si le bas du canevas manque de place.
        readonly property real below: root.py + (root.textTool ? root.textTool.clearanceMm : 0)
                                      * root.zoom + 10
        readonly property bool fitsBelow: below + height <= root.height - 8
        x: Math.max(8, Math.min(root.px - 8, root.width - width - 8))
        y: fitsBelow ? below : Math.max(8, root.py - height - 10)
        width: Math.max(220, Math.min(360, root.width - 16))
        height: column.implicitHeight + 16
        radius: Theme.cardRadius
        color: Theme.raised
        border.color: Theme.accent
        border.width: Theme.focusWidth

        // Absorbe les clics dans la carte : ils ne remontent pas à la zone « clic ailleurs ».
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            Controls.ScrollView {
                id: fieldScroll
                objectName: "textEditorScroll"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.controlHeight, Math.min(field.implicitHeight, 140))
                Controls.TextArea {
                    id: field
                    objectName: "textEditorField"
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: Theme.foreground
                    placeholderText: qsTr("Votre texte…")
                    placeholderTextColor: Theme.faint
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.accentInk
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    background: null
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Texte à broder")
                    // Ctrl+Entrée valide ; Entrée seule garde son sens (nouvelle ligne).
                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && (event.modifiers & Qt.ControlModifier)) {
                            root.finish()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            root.abort()
                            event.accepted = true
                        }
                    }
                }
            }

            Text {
                objectName: "textEditorError"
                Layout.fillWidth: true
                visible: root.textTool !== null && root.textTool.error !== ""
                text: root.textTool ? root.textTool.error : ""
                color: Theme.error
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                Accessible.role: Accessible.AlertMessage
            }

            // Ligne 1 : taille (création seulement). Ligne 2 : les deux boutons, à droite. Deux
            // lignes parce que quatre contrôles et deux raccourcis ne tiennent pas dans 360 px.
            RowLayout {
                objectName: "textEditorSizeRow"
                Layout.fillWidth: true
                visible: root.textTool !== null && root.textTool.creating
                spacing: 6
                Text {
                    text: qsTr("Taille du texte")
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                StudioSpinBox {
                    objectName: "textEditorCapHeight"
                    from: 1
                    to: 500
                    stepSize: 1
                    editable: true
                    value: root.textTool ? Math.round(root.textTool.capHeightMm) : 12
                    onValueModified: if (root.textTool) root.textTool.capHeightMm = value
                    Accessible.name: qsTr("Taille du texte : hauteur des majuscules en millimètres")
                }
                Text {
                    text: qsTr("mm")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }

            RowLayout {
                objectName: "textEditorButtonRow"
                Layout.fillWidth: true
                spacing: 6
                Item { Layout.fillWidth: true }
                StudioButton {
                    objectName: "textEditorCancel"
                    text: qsTr("Annuler")
                    tooltip: qsTr("Abandonner le texte")
                    shortcutLabel: "Échap"
                    onClicked: root.abort()
                }
                StudioButton {
                    objectName: "textEditorOk"
                    text: qsTr("Valider")
                    tooltip: qsTr("Créer le texte brodé")
                    shortcutLabel: "Ctrl+Entrée"
                    emphasized: true
                    enabled: field.text.trim().length > 0
                    onClicked: root.finish()
                }
            }
        }
    }
}
