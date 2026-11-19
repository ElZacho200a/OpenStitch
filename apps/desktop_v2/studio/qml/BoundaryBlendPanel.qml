// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import "."

// Fondu à la frontière entre deux zones voisines (docs/studio-fondu-frontiere-spec.md §8).
// Une zone peut fondre avec plusieurs voisines : la liste montre ses fondus, une ligne choisit le
// fondu édité, la croix d'une ligne le retire. Les réglages s'appliquent au fondu courant. Deux
// zones sélectionnées sans fondu entre elles : réglages puis « Créer le fondu ». Chaque action est
// UN pas d'annulation ; aucune logique métier ici (contrôleur `boundaryBlend`).
ColumnLayout {
    id: root
    property var controller: null
    readonly property bool ready: controller !== null && controller !== undefined
    readonly property bool hasBlend: ready && controller.hasBlend
    readonly property bool canCreate: ready && controller.canCreate
    readonly property bool refused: ready && !hasBlend && controller.refusal !== ""
    readonly property var blends: ready ? controller.blends : []
    readonly property bool canCreateAll: ready && controller.canCreateAll
    // Transition simplifiée : mêmes profils que le fondu de fils (linéaire, lissée, nette).
    readonly property var softnessCurves: [0, 1, 2]
    readonly property var rowsKeys: ["per_zone", "shared", "rotating", "interlaced"]

    spacing: 6
    visible: hasBlend || canCreate || canCreateAll || refused || blends.length > 0

    function curveIndex(curve) {
        const i = softnessCurves.indexOf(curve)
        return i < 0 ? 1 : i
    }
    function rowsLabel(key) {
        const i = rowsKeys.indexOf(key)
        return i <= 0 ? "" : [qsTr("chaque zone son angle"), qsTr("angle commun"), qsTr("rangées qui tournent"), qsTr("entrelacé")][i]
    }
    function blendDetails(b) {
        if (!b.active)
            return qsTr("inactif : les zones ne se touchent plus")
        let text = qsTr("%1 mm ici, %2 mm chez la voisine").arg(b.widthMm.toFixed(1)).arg(b.partnerWidthMm.toFixed(1))
        text += b.manualFrontier ? qsTr(" · frontière tracée") : qsTr(" · frontière calculée")
        const rows = rowsLabel(b.rows)
        if (rows !== "")
            text += " · " + rows
        return text
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
    // Fondus de la zone principale, un par voisine ; le fondu édité est surligné.
    ColumnLayout {
        objectName: "boundaryBlendList"
        Layout.fillWidth: true
        spacing: 2
        visible: root.blends.length > 1 || (root.blends.length === 1 && !root.hasBlend)
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Fondus de la zone sélectionnée")
        Repeater {
            model: root.blends
            delegate: Rectangle {
                id: blendRow
                required property var modelData
                readonly property bool current: modelData.current
                function choose() { root.controller.selectBlend(modelData.partnerId) }
                objectName: "boundaryBlendRow_" + modelData.partnerId
                Layout.fillWidth: true
                implicitHeight: rowContent.implicitHeight + 8
                radius: 4
                color: current ? Theme.accentSoft : rowMouse.containsMouse ? Theme.hover : Theme.raised
                border.width: activeFocus ? 2 : 0
                border.color: Theme.accent
                activeFocusOnTab: true
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Fondu avec %1").arg(modelData.partnerName !== "" ? modelData.partnerName : qsTr("la zone voisine"))
                Accessible.description: root.blendDetails(modelData)
                Accessible.selected: current
                Accessible.onPressAction: choose()
                Keys.onReturnPressed: choose()
                Keys.onSpacePressed: choose()
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: blendRow.choose()
                }
                StudioToolTip {
                    visible: (rowMouse.containsMouse || blendRow.activeFocus) && text.length > 0
                    text: qsTr("Régler ce fondu avec les réglages ci-dessous")
                }
                RowLayout {
                    id: rowContent
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 2
                    spacing: 4
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: blendRow.modelData.partnerName !== "" ? blendRow.modelData.partnerName : qsTr("Zone voisine")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.captionSize
                            font.bold: blendRow.current
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: root.blendDetails(blendRow.modelData)
                            color: blendRow.modelData.active ? Theme.faint : Theme.warning
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.captionSize
                        }
                    }
                    StudioButton {
                        objectName: "boundaryBlendRowRemove_" + blendRow.modelData.partnerId
                        iconName: "close"
                        tooltip: qsTr("Retirer ce fondu")
                        Accessible.name: qsTr("Retirer le fondu avec %1").arg(blendRow.modelData.partnerName)
                        quiet: true
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: root.controller.removeBlend(blendRow.modelData.partnerId)
                    }
                }
            }
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        visible: root.hasBlend || root.canCreate || root.canCreateAll
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
            model: [qsTr("Chaque zone son angle"), qsTr("Angle commun"), qsTr("Rangées qui tournent"), qsTr("Entrelacé")]
            currentIndex: root.ready ? Math.max(0, root.rowsKeys.indexOf(root.controller.rows)) : 0
            Accessible.name: qsTr("Sens des rangées dans la bande du fondu")
            onActivated: root.controller.setRows(root.rowsKeys[currentIndex])
            StudioToolTip { text: qsTr("L'angle commun montre une cassure au bord de la bande ; les rangées qui tournent suivent les deux angles en continu ; l'entrelacé traverse la frontière et mêle les deux couleurs dans chaque rangée.") }
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
            objectName: "boundaryBlendCreateAll"
            visible: root.canCreateAll
            text: qsTr("Fondre les %1 zones").arg(root.ready ? root.controller.zoneCount : 0)
            tooltip: qsTr("Poser un fondu sur chaque limite commune entre les zones sélectionnées (les fondus existants sont conservés)")
            emphasized: true
            onClicked: root.controller.createAll(widthHere.value / 10.0, root.softnessCurves[softness.currentIndex])
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
