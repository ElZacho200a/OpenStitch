// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Fondu à la frontière entre deux zones voisines (docs/studio-fondu-frontiere-spec.md §8).
// Deux zones sélectionnées : réglages puis « Créer le fondu ». Une zone qui porte un fondu :
// résumé, réglages immédiats et « Retirer ». Chaque action est UN pas d'annulation ; aucune
// logique métier ici (contrôleur `boundaryBlend`).
ColumnLayout {
    id: root
    property var controller: null
    readonly property bool ready: controller !== null && controller !== undefined
    readonly property bool hasBlend: ready && controller.hasBlend
    readonly property bool canCreate: ready && controller.canCreate
    readonly property bool refused: ready && !hasBlend && controller.refusal !== ""
    // Transition simplifiée : mêmes profils que le fondu de fils (linéaire, lissée, nette).
    readonly property var softnessCurves: [0, 1, 2]
    readonly property var rowsKeys: ["per_zone", "shared", "rotating"]

    spacing: 6
    visible: hasBlend || canCreate || refused

    function curveIndex(curve) {
        const i = softnessCurves.indexOf(curve)
        return i < 0 ? 1 : i
    }
    function commit() {
        const here = widthHere.value / 10.0
        const there = widthThere.value / 10.0
        const curve = softnessCurves[softness.currentIndex]
        if (root.hasBlend)
            root.controller.edit(here, there, curve)
        else
            root.controller.create(here, there, curve)
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Fondu à la frontière")
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        font.bold: true
    }
    Text {
        objectName: "boundaryBlendSummary"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        visible: text !== ""
        text: root.hasBlend ? root.controller.summary
              : root.refused ? root.controller.refusal
              : qsTr("Les deux fils s'entremêlent sur une bande de part et d'autre de la limite commune.")
        color: root.refused || (root.hasBlend && !root.controller.active) ? Theme.warning : Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        visible: root.hasBlend || root.canCreate
        columnSpacing: 8
        rowSpacing: 4
        Text {
            text: qsTr("Bande dans cette zone (mm)")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
        }
        StudioSpinBox {
            id: widthHere
            objectName: "boundaryBlendWidthHere"
            from: 5
            to: 150
            value: root.ready ? Math.round(root.controller.widthMm * 10) : 25
            textFromValue: function(v) { return (v / 10).toFixed(1) }
            valueFromText: function(t) { return Math.round(parseFloat(t.replace(",", ".")) * 10) }
            Accessible.name: qsTr("Demi-bande du fondu dans la zone sélectionnée, en millimètres")
            onValueModified: if (root.hasBlend) root.commit()
        }
        Text {
            text: qsTr("Bande chez la voisine (mm)")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
        }
        StudioSpinBox {
            id: widthThere
            objectName: "boundaryBlendWidthThere"
            from: 5
            to: 150
            value: root.ready ? Math.round(root.controller.partnerWidthMm * 10) : 25
            textFromValue: function(v) { return (v / 10).toFixed(1) }
            valueFromText: function(t) { return Math.round(parseFloat(t.replace(",", ".")) * 10) }
            Accessible.name: qsTr("Demi-bande du fondu dans la zone voisine, en millimètres")
            onValueModified: if (root.hasBlend) root.commit()
        }
        Text {
            text: qsTr("Transition")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
        }
        StudioComboBox {
            id: softness
            objectName: "boundaryBlendSoftness"
            model: [qsTr("Douce"), qsTr("Moyenne"), qsTr("Nette")]
            currentIndex: root.ready ? root.curveIndex(root.controller.curve) : 0
            Accessible.name: qsTr("Profil de transition du fondu")
            onActivated: if (root.hasBlend) root.commit()
        }
        Text {
            text: qsTr("Sens des rangées")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
        }
        StudioComboBox {
            id: rowsCombo
            objectName: "blendRowsCombo"
            model: [qsTr("Chaque zone son angle"), qsTr("Angle commun"), qsTr("Rangées qui tournent")]
            currentIndex: root.ready ? Math.max(0, root.rowsKeys.indexOf(root.controller.rows)) : 0
            Accessible.name: qsTr("Sens des rangées dans la bande du fondu")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.text: qsTr("L'angle commun montre une cassure au bord de la bande ; les rangées qui tournent suivent les deux angles en continu.")
            onActivated: root.controller.setRows(root.rowsKeys[currentIndex])
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        StudioButton {
            objectName: "boundaryBlendCreate"
            visible: !root.hasBlend && root.canCreate
            text: qsTr("Créer le fondu")
            tooltip: qsTr("Fondre les deux zones sélectionnées le long de leur limite commune")
            emphasized: true
            onClicked: root.commit()
        }
        StudioButton {
            objectName: "boundaryBlendRemove"
            visible: root.hasBlend
            text: qsTr("Retirer le fondu")
            tooltip: qsTr("Revenir à deux aplats nets")
            onClicked: root.controller.remove()
        }
    }
    Text {
        objectName: "boundaryBlendMessage"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        visible: root.ready && root.controller.message !== "" && !root.controller.lastOk
        text: root.ready ? root.controller.message : ""
        color: Theme.error
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }
}
