// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.AbstractButton {
    id: button
    property string iconName: ""
    property real iconRotation: 0
    property string tooltip: ""
    property string disabledReason: qsTr("Action indisponible dans le contexte actuel")
    property string shortcutLabel: ""
    property bool emphasized: false
    property bool quiet: false
    property bool active: false
    property bool showShortcutLabel: true
    property int textSize: Theme.bodySize
    // Moteur de thèmes : variante et état lus dans Theme.style("button", …). `forcedState` fige
    // un état pour la galerie des composants (ThemeGallery) ; vide en usage normal.
    property string forcedState: ""
    readonly property string themeVariant: emphasized ? "emphasized" : quiet ? "quiet" : "default"
    readonly property string themeState: forcedState === "focus" ? "normal"
        : forcedState.length > 0 ? forcedState
        : Theme.stateOf(enabled, down, checked || active, hovered)
    readonly property var themeStyle: Theme.style("button", themeVariant, themeState)
    readonly property var themeBase: Theme.style("button", themeVariant, "normal")
    property color contentColor: themeStyle.text !== undefined && enabled ? themeStyle.text
                                       : !enabled ? Theme.faint
                                       : emphasized ? Theme.accentInk
                                       : checked || active ? Theme.accent
                                       : Theme.foreground
    readonly property real themePadding: themeBase.paddingX !== undefined ? themeBase.paddingX : (text.length > 0 ? 11 : 8)
    implicitWidth: Math.max(implicitHeight, content.implicitWidth + 2 * themePadding)
    implicitHeight: themeBase.height !== undefined ? Math.round(themeBase.height * Theme.densityScale) : Theme.controlHeight
    // Réactions au survol et à l'appui : levée et échelle (désactivées en mouvement réduit).
    readonly property real themeLift: Theme.reducedMotion ? 0
        : themeStyle.lift !== undefined ? themeStyle.lift
        : (hovered && enabled && !down ? Theme.hoverLift : 0)
    readonly property real themeScale: Theme.reducedMotion ? 1
        : themeStyle.scale !== undefined ? themeStyle.scale
        : (down && enabled ? Theme.pressScale : 1)
    transform: Translate {
        y: -button.themeLift
        Behavior on y { NumberAnimation { duration: Theme.feedbackSlowMs; easing.type: Theme.easingEmphasis } }
    }
    scale: themeScale
    Behavior on scale { NumberAnimation { duration: Theme.feedbackFastMs; easing.type: Theme.easingEmphasis } }
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // Journal d'usage local : l'identifiant est l'objectName du bouton, à défaut son texte.
    Connections {
        target: button
        function onClicked() {
            if (typeof usage !== "undefined" && usage)
                usage.click(button.objectName.length > 0 ? button.objectName
                                                         : (button.text.length > 0 ? button.text : button.iconName))
        }
    }
    Accessible.name: tooltip.length > 0 ? tooltip : text
    // The visual `active` state must also be available to assistive technology.
    Accessible.checked: button.checked || button.active
    Accessible.description: !enabled && disabledReason.length > 0 ? disabledReason
                            : shortcutLabel.length > 0 ? qsTr("Raccourci : ") + shortcutLabel : ""

    background: ThemedSurface {
        themeStyle: button.themeStyle
        focusStyle: Theme.style("button", button.themeVariant, "focus")
        focused: button.activeFocus || button.forcedState === "focus"
        radius: Theme.radius
        fill: button.emphasized ? (button.down ? Theme.accentPressed : Theme.accent)
               : button.checked || button.active ? Theme.accentSoft
               : button.down ? Theme.raised
               : button.hovered ? Theme.hover
               : button.quiet ? "transparent" : Theme.raised
        // Hiérarchie : plein (emphasized) > aplat tonal (défaut) > discret (quiet). Le contour
        // n'apparaît qu'au survol ou au focus, pour que l'écran ne soit pas une grille de cadres.
        borderWidth: button.hovered && button.enabled && !button.quiet && !button.emphasized ? Theme.borderWidth : 0
        border: Theme.outline
        opacity: button.themeStyle.opacity !== undefined ? button.themeStyle.opacity
                 : button.enabled ? (button.down ? 0.88 : 1) : 0.55
        Behavior on opacity { NumberAnimation { duration: Theme.animationMs; easing.type: Theme.easingType } }
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        Row {
            id: content
            spacing: 8
            anchors.centerIn: parent
            StudioIcon {
                name: button.iconName
                rotation: button.iconRotation
                Behavior on rotation { NumberAnimation { duration: Theme.feedbackSlowMs } }
                color: button.themeStyle.icon !== undefined && button.enabled ? button.themeStyle.icon : button.contentColor
                visible: name.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSize
                height: Theme.iconSize
            }
            Text {
                text: button.text
                visible: text.length > 0
                color: button.contentColor
                font.family: Theme.fontFamily
                font.pixelSize: button.textSize
                font.letterSpacing: Theme.letterSpacing
                font.weight: button.themeStyle.fontWeight !== undefined ? button.themeStyle.fontWeight
                             : button.emphasized || button.active || button.checked ? Theme.strongWeight : Theme.bodyWeight
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: button.shortcutLabel
                visible: button.showShortcutLabel && text.length > 0 && button.text.length > 0
                color: button.emphasized ? Theme.accentInk : Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    HoverHandler { id: buttonHintHover; acceptedDevices: PointerDevice.Mouse }
    StudioToolTip {
        text: !button.enabled && button.disabledReason.length > 0 ? button.disabledReason
              : button.tooltip.length > 0
                ? button.tooltip + (button.shortcutLabel.length > 0 ? "  ·  " + button.shortcutLabel : "")
                : ""
        visible: (buttonHintHover.hovered || button.activeFocus) && text.length > 0
        delay: button.activeFocus ? 0 : 600
    }
}
