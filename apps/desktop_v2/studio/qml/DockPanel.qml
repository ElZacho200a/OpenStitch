// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Window
import "."

// Panneau d'espace déplaçable, masquable, repliable, défilable et détachable. Enveloppe
// StudioCard (surface/ombre inchangées) et ajoute une en-tête avec poignée de glisser + trois
// bascules, un corps toujours défilable, et un mécanisme de détachement vers une fenêtre séparée.
// Remplace les StudioCard ancrés en dur des 4 espaces (ImageSpace, SegmentationSpace, VectorSpace,
// PointsSpace) sans toucher au contenu qu'ils hébergent (contrôleurs, bindings).
//
// Position par défaut : l'appelant garde ses anchors/Layout.* habituels, inchangés au repos.
// Au premier glisser, le panneau quitte son parent/anchors d'origine pour `floatLayer` (l'Item de
// l'espace, pas toute la fenêtre : un déplacement reste visible dans son espace) et se positionne
// en x/y libres. `resetPosition()` le ramène dans son parent d'origine en ré-appliquant
// `applyHomeAnchors` (fourni par l'appelant : ce sont les MÊMES anchors/Layout.* que ceux déclarés
// à la création, le composant ne peut pas les deviner une fois coupés).
Item {
    id: panel
    // Identifiant stable pour le registre (menu « Panneaux ») et la persistance ; par défaut
    // l'objectName existant du panneau (préserve les recherches `findChild` des tests existants).
    property string panelId: objectName
    property string title: ""
    property string spaceId: ""
    // Disponibilité décidée par l'espace (image chargée, outils prêts…). À utiliser à la place de
    // `visible`, qui appartient au composant : le surcharger chez l'appelant écraserait
    // masquer/détacher.
    property bool available: true
    property bool hidden: false
    property bool collapsed: false
    property bool detached: false
    property bool movable: true
    property bool detachable: true
    // true (défaut) : corps défilable, taille naturelle (empilement de réglages, pas de défilement
    // propre). false : le contenu gère déjà son propre défilement et a besoin d'une vraie hauteur à
    // remplir (ListView/Flickable interne — PointsObjectsPanel, Inspector) ; le composant la lui
    // transmet au lieu d'imposer sa taille naturelle, sans ajouter de second niveau de défilement.
    property bool scrollBody: true
    // Item dans lequel le panneau flotte une fois déplacé (racine de l'espace courant). Sans lui,
    // le glisser est ignoré (le panneau reste ancré à sa position par défaut).
    property Item floatLayer: null
    // Ré-applique les anchors/Layout.* par défaut déclarés par l'appelant ; no-op si non fourni.
    property var applyHomeAnchors: function () {}
    readonly property bool freePositioned: _freePositioned
    default property alias content: slot.data

    property bool _freePositioned: false
    property var _homeParent: null
    property var _homeNextSibling: null
    property real _homeWidth: 0
    property real _homeHeight: 0
    property var _detachWindow: null

    // Hauteur du panneau replié : l'en-tête et le rembourrage de la carte, rien d'autre.
    readonly property real collapsedHeight: header.implicitHeight + 2 * panel._chromeMargin
    // Hauteur prise par l'habillage du panneau déplié par rapport à l'ancienne StudioCard
    // (en-tête + séparateur) : à ajouter aux hauteurs que l'appelant réservait au contenu.
    readonly property real chromeHeight: header.implicitHeight + 1
    // Marge de la carte autour de l'en-tête et du contenu : celle des StudioCard remplacées, pour
    // que le contenu garde exactement sa largeur d'avant.
    readonly property int _chromeMargin: 2
    readonly property int _headerButtonSize: 26

    visible: panel.available && !panel.hidden && !panel.detached
    // Un panneau déplacé passe au-dessus du canevas et des autres panneaux de l'espace.
    z: panel._freePositioned ? 50 : 0
    implicitHeight: panel.collapsed ? panel.collapsedHeight
                                    : panel.collapsedHeight + body.implicitHeight
    // Dans un Layout (Vecteurs, Points), le repli passe par la hauteur maximale ; l'appelant
    // ramène sa hauteur minimale à 0 quand `collapsed` (voir PointsSpace.qml).
    Layout.maximumHeight: panel.collapsed ? panel.collapsedHeight : Number.POSITIVE_INFINITY
    implicitWidth: 320
    // À l'état normal (ancré, déplié), ne RIEN imposer ici : laisser l'appelant garder la main sur
    // width/height via ses anchors.* ou ses Layout.preferredWidth/fillHeight habituels — un binding
    // inconditionnel sur `height`/`width` dans ce composant entrerait en conflit avec eux (dernier
    // écrit gagne, tiraillement). Binding.when n'active l'écrasement que pour les deux états que ce
    // composant impose lui-même (replié, flottant) ; sinon il est inactif et ne touche à rien.
    Binding {
        target: panel; property: "height"
        // Flottant : la hauteur d'origine, raccourcie si elle dépasserait le bas de l'espace (le
        // corps défile alors au lieu d'être coupé sous la barre d'état).
        value: panel.collapsed ? panel.collapsedHeight
             : panel.floatLayer ? Math.min(panel._homeHeight,
                                           Math.max(panel.collapsedHeight, panel.floatLayer.height - panel.y))
             : panel._homeHeight
        when: panel.collapsed || (panel._freePositioned && panel._homeHeight > 0)
    }
    Binding {
        target: panel; property: "width"
        value: panel._homeWidth
        when: panel._freePositioned && panel._homeWidth > 0
    }
    Behavior on height { enabled: !Theme.reducedMotion; NumberAnimation { duration: Theme.feedbackFastMs } }

    Accessible.role: Accessible.Pane
    Accessible.name: panel.title

    Component.onCompleted: {
        panel._homeParent = panel.parent
        if (panel.panelId.length > 0) {
            DockPanelRegistry.register(panel.panelId, panel.title, panel.spaceId, panel)
            _restoreState()
        }
    }
    Component.onDestruction: {
        if (panel.panelId.length > 0) DockPanelRegistry.unregister(panel.panelId)
    }

    function _restoreState() {
        if (typeof prefs === "undefined" || !prefs) return
        const state = prefs.panelLayout[panel.panelId]
        if (!state) return
        if (state.hidden === true) panel.hidden = true
        if (state.collapsed === true) panel.collapsed = true
        if (state.detached === true) Qt.callLater(panel.detach)
        if (state.freePositioned === true && panel.floatLayer)
            Qt.callLater(() => panel.floatAt(state.x || 0, state.y || 0))
    }

    // Remet le panneau dans son état d'origine : visible, déplié, ancré, dans la fenêtre principale.
    function resetLayout() {
        panel.reattach()
        panel.resetPosition()
        panel.collapsed = false
        panel.hidden = false
    }

    function _persist() {
        if (typeof prefs === "undefined" || !prefs || panel.panelId.length === 0) return
        prefs.setPanelState(panel.panelId, {
            hidden: panel.hidden,
            collapsed: panel.collapsed,
            detached: panel.detached,
            freePositioned: panel._freePositioned,
            x: panel._freePositioned ? panel.x : 0,
            y: panel._freePositioned ? panel.y : 0
        })
    }
    Timer { id: persistTimer; interval: 400; onTriggered: panel._persist() }
    onHiddenChanged: persistTimer.restart()
    onDetachedChanged: persistTimer.restart()
    // Repli tant que le panneau est ancré (anchors.top/bottom du parent encore actifs) : seul le
    // corps serait masqué sans ça, la hauteur docquée resterait pleine. `applyHomeAnchors()` relie
    // à nouveau anchors.bottom au dépli (idempotent avec les autres anchors déjà actifs).
    onCollapsedChanged: {
        if (!panel._freePositioned) {
            if (panel.collapsed) panel.anchors.bottom = undefined
            else panel.applyHomeAnchors()
        }
        persistTimer.restart()
    }
    on_FreePositionedChanged: persistTimer.restart()

    // Détache les anchors/Layout.* du parent d'origine et flotte à (x, y) dans `floatLayer`, en
    // gardant la taille actuelle (sinon la hauteur s'effondre : elle venait de anchors.top/bottom).
    function floatAt(x, y) {
        if (!panel.movable || !panel.floatLayer) return
        panel._homeWidth = panel.width
        panel._homeHeight = panel.height
        if (!panel._freePositioned && panel.parent === panel._homeParent) {
            const siblings = panel._homeParent.children
            let next = null
            for (let i = 0; i < siblings.length - 1; ++i)
                if (siblings[i] === panel) { next = siblings[i + 1]; break }
            panel._homeNextSibling = next
        }
        panel.anchors.left = undefined
        panel.anchors.right = undefined
        panel.anchors.top = undefined
        panel.anchors.bottom = undefined
        panel.anchors.fill = undefined
        panel.Layout.fillWidth = false
        panel.Layout.fillHeight = false
        if (panel.parent !== panel.floatLayer) panel.parent = panel.floatLayer
        panel._freePositioned = true
        panel._moveTo(x, y)
    }

    // Position libre bornée à l'espace : un panneau ne sort jamais de la zone visible (fenêtre
    // réduite, disposition restaurée depuis un écran plus grand).
    function _moveTo(x, y) {
        const layer = panel.floatLayer
        const maxX = layer && layer.width > 0 ? Math.max(0, layer.width - panel.width) : x
        const maxY = layer && layer.height > 0 ? Math.max(0, layer.height - panel.collapsedHeight) : y
        panel.x = Math.round(Math.min(maxX, Math.max(0, x)))
        panel.y = Math.round(Math.min(maxY, Math.max(0, y)))
        persistTimer.restart()
    }

    // Déplacement au clavier depuis l'en-tête : part de la position affichée, même ancrée.
    function nudge(dx, dy) {
        if (!panel.movable || !panel.floatLayer) return
        const topLeft = panel.mapToItem(panel.floatLayer, 0, 0)
        if (!panel._freePositioned) panel.floatAt(topLeft.x + dx, topLeft.y + dy)
        else panel._moveTo(panel.x + dx, panel.y + dy)
    }

    Connections {
        target: panel.floatLayer
        enabled: panel._freePositioned
        function onWidthChanged() { panel._moveTo(panel.x, panel.y) }
        function onHeightChanged() { panel._moveTo(panel.x, panel.y) }
    }

    function resetPosition() {
        if (!panel._freePositioned) return
        panel._freePositioned = false
        panel._homeWidth = 0
        panel._homeHeight = 0
        if (panel._homeParent) {
            panel.parent = panel._homeParent
            // Un Layout range ses enfants dans l'ordre : revenir en dernier inverserait par exemple
            // « Objets » et « Type de points » (PointsSpace). On reprend la place d'avant le voisin.
            // stackBefore() ne fait pas réagencer un Layout : on remet donc en fin de liste, dans
            // leur ordre, les voisins qui suivaient le panneau.
            const home = panel._homeParent
            const next = panel._homeNextSibling
            if (next && next.parent === home) {
                const kids = home.children
                const tail = []
                let found = false
                for (let i = 0; i < kids.length; ++i) {
                    if (kids[i] === next) found = true
                    if (found && kids[i] !== panel) tail.push(kids[i])
                }
                for (const item of tail) {
                    item.parent = null
                    item.parent = home
                }
            }
        }
        panel.applyHomeAnchors()
        // Replié en flottant : il revient replié, sans être ré-étiré par l'ancre du bas.
        if (panel.collapsed) panel.anchors.bottom = undefined
        persistTimer.restart()
    }

    // Détacher déplace le `body` (le ScrollView existant et son contenu, pas une copie) dans la
    // fenêtre séparée ; ramener le replace tel quel dans `mainColumn`. Un seul ScrollView existe
    // jamais à la fois : pas de double état, pas de contenu dupliqué.
    function detach() {
        if (!panel.detachable || panel.detached) return
        detachWindowComponent.createObject(panel, { panelTitle: panel.title })
    }
    function reattach() {
        if (!panel.detached || !panel._detachWindow) return
        body.anchors.fill = undefined
        body.parent = mainColumn
        panel._detachWindow.close()
        panel._detachWindow.destroy()
        panel._detachWindow = null
        panel.detached = false
    }

    Component {
        id: detachWindowComponent
        Window {
            id: detachWindow
            property string panelTitle: ""
            width: 420
            height: 520
            minimumWidth: 260
            minimumHeight: 200
            title: panelTitle
            visible: true
            color: Theme.background
            Item {
                id: detachHost
                anchors.fill: parent
                anchors.margins: Theme.gap
            }
            Component.onCompleted: {
                panel._detachWindow = detachWindow
                panel.detached = true
                body.parent = detachHost
                body.anchors.fill = detachHost
            }
            onClosing: panel.reattach()
        }
    }

    ThemedSurface {
        anchors.fill: parent
        themeStyle: Theme.style("card", "default", "normal")
        radius: Theme.cardRadius
        fill: Theme.panel
        border: Theme.divider
        borderWidth: Theme.borderWidth
        shadowLayers: Theme.shadowLayers
        shadowEnabled: true
        animated: false
    }

    ColumnLayout {
        id: mainColumn
        anchors.fill: parent
        anchors.margins: panel._chromeMargin
        spacing: 0

        // En-tête : poignée de glisser (toute la zone sauf les boutons), titre, trois bascules.
        Item {
            id: header
            objectName: panel.panelId.length > 0 ? panel.panelId + "_header" : ""
            Layout.fillWidth: true
            implicitHeight: panel._headerButtonSize + 2
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("En-tête du panneau ") + panel.title
                             + qsTr(" — glisser pour déplacer, flèches pour ajuster")
            activeFocusOnTab: panel.movable
            // Flèches et Suppr appartiennent à l'en-tête focalisé : sans ça, les raccourcis
            // globaux de Main.qml déplaceraient ou supprimeraient l'objet sélectionné.
            readonly property bool ownsKeyboard: true
            Rectangle {
                anchors.fill: parent
                visible: header.activeFocus
                color: "transparent"
                radius: Theme.radius
                border.color: Theme.accent
                border.width: Theme.focusWidth
            }
            Keys.onLeftPressed: panel.nudge(-20, 0)
            Keys.onRightPressed: panel.nudge(20, 0)
            Keys.onUpPressed: panel.nudge(0, -20)
            Keys.onDownPressed: panel.nudge(0, 20)
            Keys.onDeletePressed: panel.resetPosition()

            MouseArea {
                id: dragArea
                anchors.fill: parent
                enabled: panel.movable && panel.floatLayer !== null
                cursorShape: enabled ? Qt.SizeAllCursor : Qt.ArrowCursor
                // Le panneau ne quitte sa place qu'au-delà d'un petit seuil : un simple clic ou un
                // double-clic sur l'en-tête ne le décroche pas.
                property point pressScene: Qt.point(0, 0)
                property point pressOffset: Qt.point(0, 0)
                onPressed: mouse => {
                    pressScene = dragArea.mapToItem(panel.floatLayer, mouse.x, mouse.y)
                    pressOffset = dragArea.mapToItem(panel, mouse.x, mouse.y)
                }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const scenePoint = dragArea.mapToItem(panel.floatLayer, mouse.x, mouse.y)
                    if (!panel._freePositioned) {
                        if (Math.abs(scenePoint.x - pressScene.x) + Math.abs(scenePoint.y - pressScene.y) < 8)
                            return
                        const topLeft = panel.mapToItem(panel.floatLayer, 0, 0)
                        panel.floatAt(topLeft.x, topLeft.y)
                    }
                    panel._moveTo(scenePoint.x - pressOffset.x, scenePoint.y - pressOffset.y)
                }
                onDoubleClicked: panel.resetPosition()
            }

            RowLayout {
                id: headerRow
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 2
                spacing: 2
                StudioIcon {
                    visible: panel.movable && panel.floatLayer !== null
                    name: "grip"
                    implicitWidth: 14
                    implicitHeight: 14
                    color: Theme.faint
                }
                StudioLabel {
                    text: panel.title
                    role: "caption"
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                StudioButton {
                    objectName: panel.panelId + "_resetButton"
                    implicitWidth: panel._headerButtonSize
                    implicitHeight: panel._headerButtonSize
                    visible: panel._freePositioned
                    iconName: "undo"
                    quiet: true
                    tooltip: qsTr("Remettre le panneau à sa place (double-clic sur l'en-tête)")
                    onClicked: panel.resetPosition()
                }
                StudioButton {
                    objectName: panel.panelId + "_collapseButton"
                    implicitWidth: panel._headerButtonSize
                    implicitHeight: panel._headerButtonSize
                    iconName: "chevron"
                    iconRotation: panel.collapsed ? 0 : 90
                    quiet: true
                    tooltip: panel.collapsed ? qsTr("Déplier le panneau") : qsTr("Replier le panneau")
                    onClicked: panel.collapsed = !panel.collapsed
                }
                StudioButton {
                    objectName: panel.panelId + "_detachButton"
                    implicitWidth: panel._headerButtonSize
                    implicitHeight: panel._headerButtonSize
                    visible: panel.detachable
                    iconName: "detach"
                    quiet: true
                    tooltip: qsTr("Détacher dans une fenêtre séparée")
                    onClicked: panel.detach()
                }
                StudioButton {
                    objectName: panel.panelId + "_hideButton"
                    implicitWidth: panel._headerButtonSize
                    implicitHeight: panel._headerButtonSize
                    iconName: "close"
                    quiet: true
                    tooltip: qsTr("Masquer ce panneau (menu Panneaux pour le rouvrir)")
                    onClicked: panel.hidden = true
                }
            }
        }

        Rectangle {
            visible: !panel.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Corps : un Flickable écrit à la main (pas Controls.ScrollView) pour pouvoir, en mode
        // scrollBody=false, donner à `slot` la VRAIE hauteur du panneau plutôt que sa taille
        // naturelle — un ScrollView imposerait toujours un contenu « haut comme il veut », ce qui
        // réduirait à zéro les ListView/Flickable internes (PointsObjectsPanel, Inspector) qui
        // comptent sur Layout.fillHeight pour exister.
        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.collapsed
            Flickable {
                id: scrollHost
                anchors.fill: parent
                clip: true
                interactive: panel.scrollBody
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: panel.scrollBody ? slot.implicitHeight + 12 : height
                Controls.ScrollBar.vertical: Controls.ScrollBar {
                    policy: panel.scrollBody ? Controls.ScrollBar.AsNeeded : Controls.ScrollBar.AlwaysOff
                }
                // ColumnLayout, pas Item : le contenu (ImagePrepSection, LayerPanel, ParamSection…)
                // utilise Layout.fillWidth/Layout.margins, ignorés par un simple Item. Hauteur
                // naturelle si scrollBody (empilement défilable), sinon la vraie hauteur du corps
                // (contenu qui gère déjà son propre défilement avec Layout.fillHeight).
                // Empilement défilable : marges intérieures et place pour la barre de défilement,
                // comme l'ancien ScrollView des panneaux. Contenu autonome : toute la surface.
                ColumnLayout {
                    id: slot
                    x: panel.scrollBody ? 8 : 0
                    y: panel.scrollBody ? 6 : 0
                    width: scrollHost.width - (panel.scrollBody ? 20 : 0)
                    height: panel.scrollBody ? implicitHeight : scrollHost.height
                    spacing: panel.scrollBody ? 10 : 8
                }
            }
        }
    }
}
