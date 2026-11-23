// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQml
import QtQuick.Controls as Controls
import "."

// Menu contextuel du canevas (clic droit). Ouvert par `openAt` ; chaque entrée appelle l'EditController.
Controls.Menu {
    id: menu
    objectName: "canvasContextMenu"
    readonly property bool hasSelection: editor.selectedCount > 0
    // Espace Vecteur : « Supprimer » retire la forme et ses broderies (sinon : la broderie seule).
    property bool shapeScope: false
    property real atX: 0
    property real atY: 0
    // Réglage du cadre, commun à tous les canevas (ouvert par SpaceHost).
    signal hoopRequested(real x, real y)
    function openAt(x, y) { atX = x; atY = y; popup(x, y) }
    // Largeur = entrée la plus large : sans cela le style Basic tronque les libellés longs
    // (« Séparer en formes indépendantes », raccourcis) au lieu d'élargir le menu.
    width: { var w = 0; for (var i = 0; i < count; ++i) w = Math.max(w, itemAt(i).implicitWidth); return Math.max(w, implicitWidth) }

    Controls.Menu {
        title: qsTr("Sélection")
        Controls.MenuItem { text: qsTr("Tout sélectionner"); onTriggered: editor.selectAll() }
        Controls.MenuItem { text: qsTr("Inverser la sélection"); onTriggered: editor.invertSelection() }
        Controls.MenuItem { text: qsTr("Même couleur"); enabled: menu.hasSelection; onTriggered: editor.selectSameColor() }
        Controls.MenuItem { text: qsTr("Avec les voisines"); enabled: menu.hasSelection; onTriggered: editor.selectNeighbors() }
        Controls.MenuItem { text: qsTr("Désélectionner"); enabled: menu.hasSelection; onTriggered: editor.selectNone() }
    }
    Controls.Menu {
        id: typeMenu
        title: qsTr("Type de points")
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
    Controls.MenuSeparator { }
    Controls.MenuItem { objectName: "contextDuplicate"; text: qsTr("Dupliquer"); enabled: menu.hasSelection; onTriggered: editor.duplicate(5) }
    Controls.MenuItem {
        objectName: "contextDelete"
        text: menu.shapeScope ? qsTr("Supprimer %1 forme(s)").arg(editor.selectedShapeCount)
                              : qsTr("Supprimer %1 objet(s)").arg(editor.selectedCount)
        enabled: menu.shapeScope ? editor.selectedShapeCount > 0 : menu.hasSelection
        onTriggered: menu.shapeScope ? editor.deleteShapes() : editor.deleteSelection()
    }
    Controls.MenuSeparator { }
    // Opérations sur les formes : aussi au clic droit, là où la main est déjà.
    Controls.Menu {
        id: shapesMenu
        objectName: "contextShapesMenu"
        readonly property var vec: typeof vectors !== "undefined" ? vectors : null
        readonly property bool two: vec !== null && vec.selectedCount > 1
        readonly property bool some: vec !== null && vec.selectedCount > 0
        title: qsTr("Formes")
        // Largeur = entrée la plus large : sans cela le style Basic tronque les libellés longs
        // (« Séparer en formes indépendantes », raccourcis) au lieu d'élargir le menu.
        width: { var w = 0; for (var i = 0; i < count; ++i) w = Math.max(w, itemAt(i).implicitWidth); return Math.max(w, implicitWidth) }
        enabled: some
        Controls.MenuItem { objectName: "contextUnion"; text: qsTr("Union\tCtrl++"); enabled: shapesMenu.two; onTriggered: shapesMenu.vec.booleanOp("union") }
        Controls.MenuItem { objectName: "contextSubtract"; text: qsTr("Soustraire\tCtrl+-"); enabled: shapesMenu.two; onTriggered: shapesMenu.vec.booleanOp("subtract") }
        Controls.MenuItem { text: qsTr("Intersection\tCtrl+*"); enabled: shapesMenu.two; onTriggered: shapesMenu.vec.booleanOp("intersect") }
        Controls.MenuItem { text: qsTr("Exclusion\tCtrl+^"); enabled: shapesMenu.two; onTriggered: shapesMenu.vec.booleanOp("exclude") }
        Controls.MenuSeparator { }
        Controls.MenuItem { objectName: "contextBreakApart"; text: qsTr("Séparer en formes indépendantes\tCtrl+Maj+K"); enabled: shapesMenu.some; onTriggered: shapesMenu.vec.breakApart() }
        Controls.MenuItem { text: qsTr("Simplifier"); enabled: shapesMenu.some; onTriggered: shapesMenu.vec.simplify(0.1) }
        Controls.MenuItem { text: qsTr("Lisser"); enabled: shapesMenu.some; onTriggered: shapesMenu.vec.smooth(0.15) }
        Controls.MenuItem { text: qsTr("Combler les trous"); enabled: shapesMenu.some; onTriggered: shapesMenu.vec.fillHoles(4) }
        Controls.MenuItem { text: qsTr("Convertir en courbes"); enabled: shapesMenu.some; onTriggered: shapesMenu.vec.convertToCurves() }
    }
    Controls.Menu {
        title: qsTr("Transformer")
        enabled: menu.hasSelection
        Controls.MenuItem { text: qsTr("Pivoter de 90° (horaire)"); onTriggered: editor.rotate90(true) }
        Controls.MenuItem { text: qsTr("Pivoter de 90° (antihoraire)"); onTriggered: editor.rotate90(false) }
        Controls.MenuItem { text: qsTr("Miroir horizontal"); onTriggered: editor.mirror(true) }
        Controls.MenuItem { text: qsTr("Miroir vertical"); onTriggered: editor.mirror(false) }
    }
    Controls.Menu {
        title: qsTr("Ordre de couture")
        enabled: menu.hasSelection
        Controls.MenuItem { text: qsTr("Mettre au premier plan"); onTriggered: editor.stack("front") }
        Controls.MenuItem { text: qsTr("Avancer"); onTriggered: editor.stack("forward") }
        Controls.MenuItem { text: qsTr("Reculer"); onTriggered: editor.stack("backward") }
        Controls.MenuItem { text: qsTr("Mettre à l'arrière-plan"); onTriggered: editor.stack("back") }
    }
    Controls.Menu {
        title: qsTr("Aligner")
        enabled: editor.selectedCount > 1
        Controls.MenuItem { text: qsTr("À gauche"); onTriggered: editor.align("left") }
        Controls.MenuItem { text: qsTr("Centrer horizontalement"); onTriggered: editor.align("centerH") }
        Controls.MenuItem { text: qsTr("À droite"); onTriggered: editor.align("right") }
        Controls.MenuItem { text: qsTr("En haut"); onTriggered: editor.align("top") }
        Controls.MenuItem { text: qsTr("Centrer verticalement"); onTriggered: editor.align("centerV") }
        Controls.MenuItem { text: qsTr("En bas"); onTriggered: editor.align("bottom") }
        Controls.MenuItem { text: qsTr("Répartir horizontalement"); enabled: editor.selectedCount > 2; onTriggered: editor.distribute(true) }
        Controls.MenuItem { text: qsTr("Répartir verticalement"); enabled: editor.selectedCount > 2; onTriggered: editor.distribute(false) }
    }
    Controls.MenuItem { text: qsTr("Grouper"); enabled: editor.selectedCount > 1; onTriggered: editor.group("") }
    Controls.MenuItem { text: qsTr("Dégrouper"); enabled: menu.hasSelection; onTriggered: editor.ungroup() }
    Controls.MenuSeparator { }
    Controls.MenuItem {
        objectName: "contextHoop"
        text: qsTr("Cadre…")
        onTriggered: menu.hoopRequested(menu.atX, menu.atY)
    }
    Controls.Menu {
        id: layerMenu
        title: qsTr("Déplacer vers le calque")
        enabled: menu.hasSelection && editor.layers.length > 0
        Instantiator {
            model: editor.layers
            Controls.MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: editor.moveSelectionToLayer(modelData.id)
            }
            onObjectAdded: function(index, object) { layerMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { layerMenu.removeItem(object) }
        }
    }
}
