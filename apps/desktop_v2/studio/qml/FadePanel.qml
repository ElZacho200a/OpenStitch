// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "."

// Le tiroir historique et l'inspecteur partagent le MÊME éditeur de fondu.
Rectangle {
    id: panel
    objectName: "fadePanel"
    property var controller
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    FadeProperties {
        anchors.fill: parent
        controller: panel.controller
        prefix: "fade"
    }
}
