// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Actions d'édition de la sélection : sélection, transformations, ordre, alignement, placement en mm.
// Sections titrées et grilles à colonnes égales : chaque action a la même largeur et un libellé
// complet ; les raccourcis vont dans l'info-bulle (dans le bouton, ils rendaient tout illisible).
ColumnLayout {
    id: bar
    objectName: "editBar"
    spacing: 6
    readonly property bool some: editor.selectedCount > 0
    // Espace Vecteur : « Supprimer » retire la FORME (et ses broderies), pas la seule broderie.
    property bool shapeScope: false
    // Canevas à recadrer sur la miette passée en revue (facultatif).
    property var canvas: null
    // Seuil des miettes, en dixièmes de mm² (valeur du champ) ; recalculé à chaque changement du
    // document (`editor.tree`).
    property int crumbTenths: 30
    readonly property real crumbMm2: crumbTenths / 10
    readonly property int crumbCount: { editor.tree; return editor.smallObjectCount(crumbMm2) }
    readonly property bool many: editor.selectedCount > 1
    // Commandes propres aux POINTS (ordre de couture, grouper, aligner, couleur, calque) : seuls les
    // objets brodés comptent ; une forme non brodée les laisse grisées avec la raison.
    readonly property bool stitched: editor.selectedEmbroideryCount > 0
    readonly property bool manyStitched: editor.selectedEmbroideryCount > 1
    readonly property string needStitched: editor.selectedCount > 0
        ? qsTr("Réservé aux objets brodés : convertissez d'abord la forme en broderie")
        : qsTr("Sélectionnez d'abord un objet brodé")
    readonly property var box: editor.selectionBox
    readonly property bool hasBox: box.width !== undefined
    readonly property string needOne: qsTr("Sélectionnez d'abord un objet")
    readonly property string needTwo: qsTr("Sélectionnez au moins deux objets")

    component Mini: StudioButton {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 30
        implicitWidth: Math.max(30, implicitContentWidth + 14)
        showShortcutLabel: false
        disabledReason: bar.needOne
    }
    component Group: GridLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        columns: 2
        columnSpacing: 4
        rowSpacing: 4
    }
    component Label: Text {
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }
    component Num: StudioTextField {
        Layout.fillWidth: true
        implicitWidth: 52
        implicitHeight: 28
        color: Theme.foreground
        font.pixelSize: Theme.smallSize
        selectByMouse: true
        validator: DoubleValidator { notation: DoubleValidator.StandardNotation; locale: "C" }
        background: Rectangle { color: Theme.raised; border.color: Theme.border; radius: 3 }
    }

    // L'état d'abord : on sait sur quoi on agit avant de lire les boutons.
    Text {
        objectName: "editSelectionSummary"
        Layout.fillWidth: true
        text: editor.selectedCount === 0 ? qsTr("Aucun objet sélectionné")
                                         : qsTr("%1 objet(s) sélectionné(s)").arg(editor.selectedCount)
        color: bar.some ? Theme.foreground : Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.bodySize
        font.weight: Font.DemiBold
    }

    SectionLabel { text: qsTr("SÉLECTIONNER"); Layout.topMargin: 4 }
    Group {
        objectName: "editSelectionFlow"
        Mini { objectName: "selectAllButton"; text: qsTr("Tout"); tooltip: qsTr("Tout sélectionner"); shortcutLabel: "Ctrl+A"; onClicked: editor.selectAll() }
        Mini { objectName: "invertButton"; text: qsTr("Inverser"); tooltip: qsTr("Inverser la sélection"); shortcutLabel: "Ctrl+I"; onClicked: editor.invertSelection() }
        Mini { objectName: "sameColorButton"; text: qsTr("Même couleur"); tooltip: qsTr("Ajouter les objets de la même couleur"); enabled: bar.stitched; disabledReason: bar.needStitched; onClicked: editor.selectSameColor() }
        Mini { objectName: "neighborsButton"; text: qsTr("Voisines"); tooltip: qsTr("Ajouter les objets qui touchent la sélection"); enabled: bar.stitched; disabledReason: bar.needStitched; onClicked: editor.selectNeighbors() }
        Mini { text: qsTr("Par type ▾"); tooltip: qsTr("Sélectionner par type de points"); onClicked: typeMenu.popup() }
        Mini { text: qsTr("Rappeler ▾"); tooltip: qsTr("Restaurer une sélection mémorisée"); enabled: editor.savedSelections.length > 0; disabledReason: qsTr("Aucune sélection mémorisée"); onClicked: recallMenu.popup() }
        Mini { Layout.columnSpan: 2; text: qsTr("Mémoriser la sélection"); enabled: bar.stitched; disabledReason: bar.needStitched; onClicked: editor.saveSelection(qsTr("Sélection %1").arg(editor.savedSelections.length + 1)) }
    }

    SectionLabel { text: qsTr("MIETTES"); Layout.topMargin: 4 }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text {
            text: qsTr("Sous (mm²)")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
        }
        StudioSpinBox {
            objectName: "crumbThreshold"
            from: 1
            to: 1000
            value: bar.crumbTenths
            textFromValue: function(v) { return (v / 10).toFixed(1) }
            valueFromText: function(t) { return Math.round(parseFloat(t.replace(",", ".")) * 10) }
            Accessible.name: qsTr("Aire en dessous de laquelle un objet est une miette, en millimètres carrés")
            onValueModified: bar.crumbTenths = value
        }
        Text {
            objectName: "crumbCountLabel"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            text: bar.crumbCount === 0 ? qsTr("aucune") : qsTr("%1 miette(s)").arg(bar.crumbCount)
            color: bar.crumbCount > 0 ? Theme.warning : Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
            font.weight: Font.DemiBold
        }
    }
    Group {
        objectName: "editCrumbFlow"
        columns: 3
        Mini { objectName: "crumbPrevButton"; text: "◀"; tooltip: qsTr("Miette précédente (recadrée)"); enabled: bar.crumbCount > 0; disabledReason: qsTr("Aucune miette sous ce seuil"); onClicked: { if (editor.selectNextSmallObject(bar.crumbMm2, false) && bar.canvas) bar.canvas.zoomToSelection() } }
        Mini { objectName: "crumbAllButton"; text: qsTr("Toutes"); tooltip: qsTr("Sélectionner toutes les miettes"); enabled: bar.crumbCount > 0; disabledReason: qsTr("Aucune miette sous ce seuil"); onClicked: editor.selectSmallObjects(bar.crumbMm2) }
        Mini { objectName: "crumbNextButton"; text: "▶"; tooltip: qsTr("Miette suivante (recadrée)"); enabled: bar.crumbCount > 0; disabledReason: qsTr("Aucune miette sous ce seuil"); onClicked: { if (editor.selectNextSmallObject(bar.crumbMm2, true) && bar.canvas) bar.canvas.zoomToSelection() } }
    }
    Group {
        objectName: "editCrumbFixFlow"
        // Fondre plutôt que supprimer : la miette rejoint sa voisine (même fil d'abord), sans trou.
        Mini { objectName: "crumbAbsorbButton"; text: qsTr("Absorber"); tooltip: qsTr("Fondre chaque miette dans sa voisine (même fil d'abord), sans laisser de trou"); enabled: bar.crumbCount > 0; disabledReason: qsTr("Aucune miette sous ce seuil"); onClicked: actions.run("shape.absorb_crumbs", { "max_area_mm2": String(bar.crumbMm2), "priority": "same_thread", "reach_mm": "0.2" }) }
        Mini { objectName: "gapFillButton"; text: qsTr("Reboucher"); tooltip: qsTr("Reboucher les vides fermés entre formes (les trous internes d'une forme restent)"); onClicked: actions.run("shape.fill_gaps_between", { "max_area_mm2": "4", "reach_mm": "0.2" }) }
    }

    SectionLabel { text: qsTr("TRANSFORMER"); Layout.topMargin: 4 }
    Group {
        objectName: "editTransformFlow"
        Mini { objectName: "rotateCwButton"; text: qsTr("↻ 90°"); enabled: bar.some; tooltip: qsTr("Pivoter de 90° (horaire)"); shortcutLabel: "Ctrl+R"; onClicked: editor.rotate90(true) }
        Mini { objectName: "rotateCcwButton"; text: qsTr("↺ 90°"); enabled: bar.some; tooltip: qsTr("Pivoter de 90° (antihoraire)"); shortcutLabel: "Ctrl+Maj+R"; onClicked: editor.rotate90(false) }
        Mini { objectName: "mirrorHButton"; text: qsTr("Miroir ↔"); enabled: bar.some; tooltip: qsTr("Miroir horizontal"); shortcutLabel: "Ctrl+Maj+H"; onClicked: editor.mirror(true) }
        Mini { objectName: "mirrorVButton"; text: qsTr("Miroir ↕"); enabled: bar.some; tooltip: qsTr("Miroir vertical"); shortcutLabel: "Ctrl+Maj+V"; onClicked: editor.mirror(false) }
        Mini { text: qsTr("Aligner ▾"); enabled: bar.manyStitched; tooltip: qsTr("Aligner ou répartir"); disabledReason: bar.stitched || editor.selectedCount === 0 ? bar.needTwo : bar.needStitched; onClicked: alignMenu.popup() }
        Mini { objectName: "duplicateButton"; text: qsTr("Dupliquer"); enabled: bar.some; tooltip: qsTr("Dupliquer la sélection"); shortcutLabel: "Ctrl+D"; onClicked: editor.duplicate(5) }
    }

    SectionLabel { text: qsTr("ORGANISER"); Layout.topMargin: 4 }
    Group {
        Mini { objectName: "frontButton"; text: qsTr("Premier plan"); enabled: bar.stitched; disabledReason: bar.needStitched; tooltip: qsTr("Coudre en dernier (dessus)"); shortcutLabel: "Fin"; onClicked: editor.stack("front") }
        Mini { objectName: "backButton"; text: qsTr("Arrière-plan"); enabled: bar.stitched; disabledReason: bar.needStitched; tooltip: qsTr("Coudre en premier (dessous)"); shortcutLabel: "Origine"; onClicked: editor.stack("back") }
        Mini { objectName: "groupButton"; text: qsTr("Grouper"); enabled: bar.manyStitched; disabledReason: bar.stitched || editor.selectedCount === 0 ? bar.needTwo : bar.needStitched; tooltip: qsTr("Grouper la sélection"); shortcutLabel: "Ctrl+G"; onClicked: editor.group("") }
        Mini { objectName: "ungroupButton"; text: qsTr("Dégrouper"); enabled: bar.stitched; disabledReason: bar.needStitched; tooltip: qsTr("Dissoudre le groupe"); shortcutLabel: "Ctrl+Maj+G"; onClicked: editor.ungroup() }
        Mini { objectName: "pipetteButton"; text: qsTr("Pipette"); checkable: true; checked: editor.eyedropper; enabled: bar.stitched; disabledReason: bar.needStitched; tooltip: qsTr("Copier la couleur d'un objet vers la sélection"); shortcutLabel: "I"; onClicked: editor.eyedropper = !editor.eyedropper }
        Mini { objectName: "snapButton"; text: qsTr("Accrochage"); checkable: true; checked: editor.snapEnabled; tooltip: qsTr("Accrochage et repères au glisser (Alt = temporaire)"); onClicked: editor.snapEnabled = !editor.snapEnabled }
        Mini { objectName: "deleteButton"; Layout.columnSpan: 2; text: qsTr("Supprimer"); enabled: bar.shapeScope ? editor.selectedShapeCount > 0 : bar.some; tooltip: bar.shapeScope ? qsTr("Supprimer la forme et ses broderies") : qsTr("Supprimer l'objet : sa forme part aussi, sauf si une autre couche de points l'utilise"); shortcutLabel: "Suppr"; onClicked: bar.shapeScope ? editor.deleteShapes() : editor.deleteSelection() }
        Mini { objectName: "removeStitchesButton"; visible: !bar.shapeScope; Layout.columnSpan: 2; text: qsTr("Retirer les points (garder la forme)"); enabled: bar.stitched; disabledReason: bar.needStitched; tooltip: qsTr("Retirer les points des objets sélectionnés ; la forme reste, reconvertible"); shortcutLabel: "Maj+Suppr"; onClicked: editor.removeStitchesOnly() }
    }

    // Placement numérique (centre et taille de la boîte, en mm modèle : Y vers le haut).
    SectionLabel { text: qsTr("POSITION ET TAILLE (MM)"); Layout.topMargin: 4; visible: bar.hasBox }
    GridLayout {
        objectName: "editPlacementFlow"
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        visible: bar.hasBox
        columns: 4
        columnSpacing: 6
        rowSpacing: 4
        Label { text: qsTr("X") }
        Num { id: xField; objectName: "placeX"; text: bar.hasBox ? bar.box.cx.toFixed(2) : ""; Accessible.name: qsTr("Centre X en millimètres") }
        Label { text: qsTr("Y") }
        Num { id: yField; objectName: "placeY"; text: bar.hasBox ? (-bar.box.cy).toFixed(2) : ""; Accessible.name: qsTr("Centre Y en millimètres") }
        Label { text: qsTr("Larg.") }
        Num { id: wField; objectName: "placeW"; text: bar.hasBox ? bar.box.width.toFixed(2) : ""; Accessible.name: qsTr("Largeur en millimètres") }
        Label { text: qsTr("Haut.") }
        Num { id: hField; objectName: "placeH"; text: bar.hasBox ? bar.box.height.toFixed(2) : ""; Accessible.name: qsTr("Hauteur en millimètres") }
        Label { text: qsTr("Angle") }
        Num { id: angleField; objectName: "placeAngle"; text: "0"; Accessible.name: qsTr("Angle de rotation en degrés") }
        StudioButton {
            objectName: "placeApply"
            Layout.columnSpan: 2
            Layout.fillWidth: true
            implicitHeight: 30
            text: qsTr("Appliquer")
            tooltip: qsTr("Appliquer position, taille et rotation (mm)")
            onClicked: {
                const same = function(field, value) { return Math.abs(parseFloat(field.text) - value) < 0.005 }
                editor.placeSelection(
                    same(xField, bar.box.cx) ? NaN : parseFloat(xField.text),
                    same(yField, -bar.box.cy) ? NaN : parseFloat(yField.text),
                    same(wField, bar.box.width) ? NaN : parseFloat(wField.text),
                    same(hField, bar.box.height) ? NaN : parseFloat(hField.text),
                    parseFloat(angleField.text) || 0)
                angleField.text = "0"
            }
        }
    }
    // Rotation autour d'un centre choisi (mm modèle, Y vers le haut) : angle du champ Angle.
    SectionLabel { text: qsTr("PIVOTER AUTOUR D'UN POINT"); Layout.topMargin: 4; visible: bar.hasBox }
    GridLayout {
        objectName: "editPivotFlow"
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        visible: bar.hasBox
        columns: 4
        columnSpacing: 6
        rowSpacing: 4
        Label { text: qsTr("X") }
        Num { id: pivotX; objectName: "pivotX"; text: bar.hasBox ? editor.selectionCentreMm().x.toFixed(2) : ""; Accessible.name: qsTr("Centre de rotation X") }
        Label { text: qsTr("Y") }
        Num { id: pivotY; objectName: "pivotY"; text: bar.hasBox ? editor.selectionCentreMm().y.toFixed(2) : ""; Accessible.name: qsTr("Centre de rotation Y") }
        StudioButton {
            objectName: "pivotCentreButton"
            Layout.columnSpan: 2
            Layout.fillWidth: true
            implicitHeight: 30
            text: qsTr("Au centre")
            tooltip: qsTr("Utiliser le centre de la sélection")
            onClicked: {
                const centre = editor.selectionCentreMm()
                pivotX.text = centre.x.toFixed(2)
                pivotY.text = centre.y.toFixed(2)
            }
        }
        StudioButton {
            objectName: "pivotApply"
            Layout.columnSpan: 2
            Layout.fillWidth: true
            implicitHeight: 30
            text: qsTr("Pivoter")
            tooltip: qsTr("Pivoter de l'angle indiqué autour de ce point")
            enabled: bar.some
            onClicked: editor.rotateAround(parseFloat(angleField.text) || 0, parseFloat(pivotX.text), parseFloat(pivotY.text))
        }
    }
    Text {
        Layout.fillWidth: true
        visible: editor.message.length > 0
        text: editor.message
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.Wrap
    }

    Controls.Menu {
        id: typeMenu
        Instantiator {
            model: editor.stitchTypes
            Controls.MenuItem {
                required property string modelData
                text: modelData
                onTriggered: editor.selectByType(modelData)
            }
            onObjectAdded: function(index, object) { typeMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { typeMenu.removeItem(object) }
        }
    }
    Controls.Menu {
        id: recallMenu
        Instantiator {
            model: editor.savedSelections
            Controls.MenuItem {
                required property string modelData
                text: modelData
                onTriggered: editor.recallSelection(modelData)
            }
            onObjectAdded: function(index, object) { recallMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { recallMenu.removeItem(object) }
        }
    }
    Controls.Menu {
        id: alignMenu
        Controls.MenuItem { text: qsTr("À gauche"); onTriggered: editor.align("left") }
        Controls.MenuItem { text: qsTr("Centrer horizontalement"); onTriggered: editor.align("centerH") }
        Controls.MenuItem { text: qsTr("À droite"); onTriggered: editor.align("right") }
        Controls.MenuItem { text: qsTr("En haut"); onTriggered: editor.align("top") }
        Controls.MenuItem { text: qsTr("Centrer verticalement"); onTriggered: editor.align("centerV") }
        Controls.MenuItem { text: qsTr("En bas"); onTriggered: editor.align("bottom") }
        Controls.MenuItem { text: qsTr("Répartir horizontalement"); enabled: editor.selectedEmbroideryCount > 2; onTriggered: editor.distribute(true) }
        Controls.MenuItem { text: qsTr("Répartir verticalement"); enabled: editor.selectedEmbroideryCount > 2; onTriggered: editor.distribute(false) }
    }
}
