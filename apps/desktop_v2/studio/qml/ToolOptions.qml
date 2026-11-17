// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Options de l'outil actif, accrochage et opérations vectorielles sur la sélection. Tolérant à
// l'absence des contextes `tools` et `vectors`.
ColumnLayout {
    id: panel
    objectName: "toolOptions"
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property var vec: typeof vectors !== "undefined" ? vectors : null
    visible: toolCtl !== null
    spacing: 6
    readonly property bool two: vec !== null && vec.selectedCount > 1
    readonly property bool some: vec !== null && vec.selectedCount > 0
    readonly property string needTwo: qsTr("Sélectionnez au moins deux formes (Maj+clic)")
    readonly property string needOne: qsTr("Sélectionnez d'abord une forme")

    // Même grammaire que l'EditBar : grilles à colonnes égales, libellés complets.
    component Mini: StudioButton {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 30
        implicitWidth: Math.max(30, implicitContentWidth + 14)
        showShortcutLabel: false
    }
    component Group: GridLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        columns: 2
        columnSpacing: 4
        rowSpacing: 4
    }

    SectionLabel { text: qsTr("OUTIL") }
    Text {
        objectName: "toolHint"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        text: panel.toolCtl ? (panel.toolCtl.hint !== "" ? panel.toolCtl.hint : qsTr("Choisissez un outil dans le rail.")) : ""
    }
    Text {
        objectName: "toolMessage"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.faint
        font.pixelSize: Theme.smallSize
        text: panel.toolCtl ? panel.toolCtl.message : ""
        visible: text !== ""
    }

    RowLayout {
        visible: panel.toolCtl && panel.toolCtl.activeTool === "draw.polygon-regular"
        spacing: 6
        Text { text: qsTr("Côtés"); color: Theme.muted; font.pixelSize: Theme.smallSize }
        StudioSpinBox {
            objectName: "sidesSpin"
            from: 3; to: 64
            value: panel.toolCtl ? panel.toolCtl.sides : 6
            onValueModified: panel.toolCtl.sides = value
        }
        StudioCheckBox {
            objectName: "starCheck"
            text: qsTr("Étoile")
            checked: panel.toolCtl ? panel.toolCtl.star : false
            onToggled: panel.toolCtl.star = checked
        }
    }

    SectionLabel { text: qsTr("PINCEAU DE DÉFORMATION"); visible: panel.toolCtl && panel.toolCtl.activeTool === "warp-brush" }
    ColumnLayout {
        visible: panel.toolCtl && panel.toolCtl.activeTool === "warp-brush"
        Layout.fillWidth: true
        Text { text: qsTr("Rayon du pinceau (mm)"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
        StudioSpinBox {
            objectName: "warpRadius"
            from: 5; to: 600; stepSize: 5
            value: panel.toolCtl ? Math.round(panel.toolCtl.warp.radiusMm * 10) : 50
            textFromValue: function(value, locale) { return Number(value / 10).toLocaleString(locale, 'f', 1) }
            valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10) }
            editable: true
            Accessible.name: qsTr("Rayon du pinceau en millimètres")
            onValueModified: panel.toolCtl.warp.radiusMm = value / 10
        }
        Text { text: qsTr("Intensité (%)"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
        StudioSpinBox {
            objectName: "warpStrength"
            from: 5; to: 100; value: panel.toolCtl ? Math.round(panel.toolCtl.warp.strength * 100) : 60
            editable: true
            Accessible.name: qsTr("Intensité du pinceau en pourcentage")
            onValueModified: panel.toolCtl.warp.strength = value / 100
        }
    }
    SectionLabel { text: qsTr("ACCROCHAGE") }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        StudioCheckBox {
            objectName: "snapVerticesCheck"
            text: qsTr("Sommets")
            checked: panel.toolCtl ? panel.toolCtl.snapVertices : false
            onToggled: panel.toolCtl.snapVertices = checked
        }
        StudioCheckBox {
            objectName: "snapGridCheck"
            text: qsTr("Grille")
            checked: panel.toolCtl ? panel.toolCtl.snapGrid : false
            onToggled: panel.toolCtl.snapGrid = checked
        }
        StudioSpinBox {
            objectName: "snapRadiusSpin"
            from: 2; to: 40
            value: panel.toolCtl ? panel.toolCtl.snapRadiusPx : 8
            onValueModified: panel.toolCtl.snapRadiusPx = value
            StudioToolTip { text: qsTr("Rayon d'accrochage (px)") }
        }
    }

    SectionLabel { text: qsTr("NŒUDS SÉLECTIONNÉS"); visible: nodeGroup.visible; Layout.topMargin: 4 }
    Group {
        id: nodeGroup
        visible: panel.toolCtl && panel.toolCtl.activeTool === "node.edit"
        readonly property bool any: panel.toolCtl && panel.toolCtl.selectedNodeCount > 0
        Mini { objectName: "nodeCornerButton"; text: qsTr("Anguleux"); tooltip: qsTr("Coins nets sur les nœuds sélectionnés"); shortcutLabel: "C"; enabled: nodeGroup.any; disabledReason: qsTr("Sélectionnez des nœuds"); onClicked: panel.toolCtl.setSelectedNodeKind(false) }
        Mini { objectName: "nodeSmoothButton"; text: qsTr("Lisses"); tooltip: qsTr("Courbes douces sur les nœuds sélectionnés"); shortcutLabel: "S"; enabled: nodeGroup.any; disabledReason: qsTr("Sélectionnez des nœuds"); onClicked: panel.toolCtl.setSelectedNodeKind(true) }
        Mini { objectName: "nodeDeleteButton"; text: qsTr("Supprimer nœud"); enabled: nodeGroup.any; disabledReason: qsTr("Sélectionnez des nœuds"); onClicked: panel.toolCtl.deleteSelectedNodes() }
        Mini { objectName: "nodeHoleButton"; text: qsTr("Supprimer trou"); tooltip: qsTr("Supprime le trou qui contient le nœud"); enabled: nodeGroup.any; disabledReason: qsTr("Sélectionnez un nœud du trou"); onClicked: panel.toolCtl.removeHole() }
    }

    SectionLabel { text: qsTr("COMBINER LES FORMES"); Layout.topMargin: 4 }
    Group {
        enabled: panel.vec !== null
        Mini { objectName: "unionButton"; disabledReason: panel.needTwo; tooltip: qsTr("Réunit les formes en une seule"); shortcutLabel: "Ctrl++"; text: qsTr("Union"); enabled: panel.two; onClicked: panel.vec.booleanOp("union") }
        Mini { objectName: "subtractButton"; disabledReason: panel.needTwo; tooltip: qsTr("Découpe les formes du dessus dans la plus basse de la pile"); shortcutLabel: "Ctrl+-"; text: qsTr("Soustraire"); enabled: panel.two; onClicked: panel.vec.booleanOp("subtract") }
        Mini { objectName: "intersectButton"; disabledReason: panel.needTwo; tooltip: qsTr("Garde seulement la partie commune"); shortcutLabel: "Ctrl+*"; text: qsTr("Intersection"); enabled: panel.two; onClicked: panel.vec.booleanOp("intersect") }
        Mini { objectName: "excludeButton"; disabledReason: panel.needTwo; tooltip: qsTr("Garde tout sauf la partie commune"); shortcutLabel: "Ctrl+^"; text: qsTr("Exclusion"); enabled: panel.two; onClicked: panel.vec.booleanOp("exclude") }
        Mini { objectName: "breakApartButton"; Layout.columnSpan: 2; disabledReason: panel.needOne; tooltip: qsTr("Fait de chaque morceau disjoint une forme indépendante"); shortcutLabel: "Ctrl+Maj+K"; text: qsTr("Séparer les morceaux"); enabled: panel.some; onClicked: panel.vec.breakApart() }
    }

    SectionLabel { text: qsTr("NETTOYER LE CONTOUR"); Layout.topMargin: 4 }
    Group {
        enabled: panel.vec !== null
        Mini { objectName: "simplifyButton"; disabledReason: panel.needOne; tooltip: qsTr("Retire les nœuds superflus (tolérance 0,1 mm)"); text: qsTr("Simplifier"); enabled: panel.some; onClicked: panel.vec.simplify(0.1) }
        Mini { objectName: "smoothButton"; disabledReason: panel.needOne; tooltip: qsTr("Adoucit les angles du contour"); text: qsTr("Lisser"); enabled: panel.some; onClicked: panel.vec.smooth(0.15) }
        Mini { objectName: "fillHolesButton"; disabledReason: panel.needOne; tooltip: qsTr("Bouche les trous de moins de 4 mm²"); text: qsTr("Combler les trous"); enabled: panel.some; onClicked: panel.vec.fillHoles(4) }
        Mini { objectName: "toCurvesButton"; disabledReason: panel.needOne; tooltip: qsTr("Rend les segments éditables en courbes"); text: qsTr("En courbes"); enabled: panel.some; onClicked: panel.vec.convertToCurves() }
    }

    SectionLabel { text: qsTr("DÉCALER"); Layout.topMargin: 4 }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        enabled: panel.vec !== null && panel.some
        Text { text: qsTr("Distance (mm)"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; Layout.fillWidth: true }
        StudioTextField {
            id: offsetField
            objectName: "offsetMmField"
            wheelStep: 0.5
            wheelMin: 0.1
            wheelMax: 50
            implicitWidth: 64
            text: "1.0"
            invalid: isNaN(parseFloat(text.replace(",", ".")))
            Accessible.name: qsTr("Décalage en millimètres")
        }
    }
    GridLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        columns: 3
        columnSpacing: 4
        enabled: panel.vec !== null
        readonly property real mm: Math.abs(parseFloat(offsetField.text.replace(",", ".")))
        Mini { objectName: "offsetInButton"; disabledReason: panel.needOne; tooltip: qsTr("Copie rétrécie de cette distance"); text: qsTr("Rétrécir"); enabled: panel.some && !offsetField.invalid; onClicked: panel.vec.offsetShapes(-parent.mm) }
        Mini { objectName: "offsetOutButton"; disabledReason: panel.needOne; tooltip: qsTr("Copie agrandie de cette distance"); text: qsTr("Agrandir"); enabled: panel.some && !offsetField.invalid; onClicked: panel.vec.offsetShapes(parent.mm) }
        Mini { objectName: "outlineButton"; disabledReason: panel.needOne; tooltip: qsTr("Anneau autour de la forme, de cette épaisseur"); text: qsTr("Contour"); enabled: panel.some && !offsetField.invalid; onClicked: panel.vec.outlineShapes(parent.mm) }
    }
    Text {
        objectName: "vectorOpsMessage"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.faint
        font.pixelSize: Theme.smallSize
        text: panel.vec ? panel.vec.message : ""
        visible: text !== ""
    }
}
