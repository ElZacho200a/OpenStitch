// SPDX-License-Identifier: Apache-2.0
// Combo box themed by Theme: field, chevron, and popup list all use the dark tokens.
import QtQuick
import QtQuick.Controls as Controls
import "."

Controls.ComboBox {
    id: control
    property bool invalid: false

    implicitHeight: Math.max(28, Math.round(Theme.controlHeight * 0.75))
    implicitWidth: 140
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.smallSize
    leftPadding: 8
    rightPadding: 8 + 20
    Accessible.role: Accessible.ComboBox

    delegate: Controls.ItemDelegate {
        id: item
        // Same text resolution as the Basic style delegate (arrays, ListModel, textRole).
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole]
                                                               : model[control.textRole])
                               : modelData
        width: control.popup.availableWidth
        height: Math.max(28, Math.round(Theme.controlHeight * 0.75))
        highlighted: control.highlightedIndex === index
        padding: 8
        contentItem: Text {
            text: item.text
            font: control.font
            color: item.highlighted ? Theme.accentInk : Theme.foreground
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: item.highlighted ? Theme.accent : "transparent"
        }
    }

    indicator: Item {
        x: control.width - width - 6
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 12
        height: 12
        StudioIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            rotation: 90
            name: "chevron"
            color: control.enabled ? Theme.muted : Theme.faint
        }
    }

    contentItem: Text {
        leftPadding: 0
        rightPadding: control.indicator.width + control.spacing
        text: control.displayText
        font: control.font
        color: !control.enabled ? Theme.faint : control.invalid ? Theme.error : Theme.foreground
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 5
        color: control.enabled ? (control.pressed ? Theme.hover : Theme.raised) : Theme.surface
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.invalid ? Theme.error
                     : control.visualFocus || control.popup.visible ? Theme.accent
                     : control.hovered ? Theme.muted
                     : Theme.outline
        opacity: control.enabled ? 1 : 0.7
    }

    popup: Controls.Popup {
        id: dropdown
        // Position in window coordinates, not inside a clipped inspector/Flickable.
        // The popup must remain readable and clickable close to any window edge.
        parent: Controls.Overlay.overlay
        readonly property real edgeMargin: 8
        readonly property point fieldOrigin: parent ? control.mapToItem(parent, 0, 0) : Qt.point(0, 0)
        x: parent ? Math.max(edgeMargin,
                             Math.min(fieldOrigin.x, parent.width - width - edgeMargin)) : 0
        y: {
            if (!parent)
                return control.height + 2
            const below = fieldOrigin.y + control.height + 2
            const above = fieldOrigin.y - height - 2
            if (below + height <= parent.height - edgeMargin)
                return below
            if (above >= edgeMargin)
                return above
            return Math.max(edgeMargin, Math.min(below, parent.height - height - edgeMargin))
        }
        width: parent ? Math.min(Math.max(control.width, 220),
                                  Math.max(1, parent.width - 2 * edgeMargin)) : control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320,
                                  parent ? Math.max(32, parent.height - 2 * edgeMargin) : 320)
        padding: 1
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            Controls.ScrollIndicator.vertical: Controls.ScrollIndicator { }
        }
        background: Rectangle {
            color: Theme.raised
            radius: 5
            border.width: 1
            border.color: Theme.outline
        }
    }
}
