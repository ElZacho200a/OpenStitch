// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Deux textures immuables (image / masque), quelle que soit la résolution de la source.
Item {
    id: root
    objectName: "detailTools"
    property var canvasItem: null
    readonly property var ctl: typeof detailTools !== "undefined" ? detailTools : null
    readonly property var toolCtl: typeof tools !== "undefined" ? tools : null
    readonly property string tool: toolCtl ? toolCtl.activeTool : ""
    readonly property bool active: ctl !== null &&
        (tool === "detail-brush" || tool === "detail-erase" || tool === "click-to-stitch")
    property point imageOrigin: Qt.point(0, 0)
    property point imageEnd: Qt.point(0, 0)

    function updateImageRect() {
        if (!ctl || !canvasItem || !ctl.ready) return
        const size = ctl.imageSizeMm
        imageOrigin = canvasItem.sceneToCanvas(-size.width / 2, -size.height / 2)
        imageEnd = canvasItem.sceneToCanvas(size.width / 2, size.height / 2)
    }
    Connections {
        target: root.ctl
        function onChanged() { root.updateImageRect() }
    }
    Connections {
        target: root.canvasItem
        function onViewChanged() { root.updateImageRect() }
    }
    Component.onCompleted: updateImageRect()

    Image {
        objectName: "detailSourceImage"
        visible: root.active && root.ctl.ready
        x: root.imageOrigin.x; y: root.imageOrigin.y
        width: root.imageEnd.x - x; height: root.imageEnd.y - y
        source: root.ctl ? root.ctl.imageUrl : ""
        opacity: 0.7
        cache: false
        smooth: false
    }
    Image {
        objectName: "detailMaskImage"
        visible: root.active && root.ctl.showMask
        x: root.imageOrigin.x; y: root.imageOrigin.y
        width: root.imageEnd.x - x; height: root.imageEnd.y - y
        source: root.ctl ? root.ctl.maskUrl : ""
        cache: false
        smooth: false
    }
    Rectangle {
        visible: root.active
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 58
        anchors.topMargin: 10
        width: 260
        height: settings.implicitHeight + 20
        color: Theme.panel
        border.color: Theme.border
        radius: Theme.radius
        ColumnLayout {
            id: settings
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 10
            spacing: 6
            SectionLabel { text: qsTr("DÉTAILS ET CLIC-POUR-BRODER") }
            Text {
                Layout.fillWidth: true
                text: root.ctl ? root.ctl.message : ""
                color: Theme.muted; font.pixelSize: Theme.smallSize
                wrapMode: Text.Wrap
            }
            RowLayout {
                visible: root.tool !== "click-to-stitch"
                Text { text: qsTr("Rayon (0,1 mm)"); color: Theme.foreground }
                Controls.SpinBox {
                    objectName: "detailRadius"
                    from: 2; to: 500; editable: true
                    value: root.ctl ? Math.round(root.ctl.radiusMm * 10) : 20
                    onValueModified: root.ctl.radiusMm = value / 10
                }
            }
            Controls.CheckBox {
                objectName: "detailShowMask"
                text: qsTr("Afficher le masque")
                checked: root.ctl ? root.ctl.showMask : true
                onToggled: root.ctl.showMask = checked
            }
            StudioButton {
                objectName: "detailClearMask"
                text: qsTr("Tout effacer")
                onClicked: root.ctl.clearMask()
            }
            RowLayout {
                visible: root.tool === "click-to-stitch"
                Text { text: qsTr("Tolérance"); color: Theme.foreground }
                Controls.SpinBox {
                    objectName: "detailTolerance"
                    from: 0; to: 100; editable: true
                    value: root.ctl ? root.ctl.tolerance : 20
                    onValueModified: root.ctl.tolerance = value
                }
            }
            Controls.ComboBox {
                objectName: "detailStitchKind"
                visible: root.tool === "click-to-stitch"
                Layout.fillWidth: true
                model: [qsTr("Automatique"), qsTr("Tatami"), qsTr("Satin"), qsTr("Contour")]
                currentIndex: root.ctl ? root.ctl.stitchKind : 0
                onActivated: root.ctl.stitchKind = currentIndex
            }
            Controls.CheckBox {
                objectName: "detailReplaceSelected"
                visible: root.tool === "click-to-stitch"
                text: qsTr("Régénérer l'objet sélectionné")
                checked: root.ctl ? root.ctl.replaceSelected : false
                onToggled: root.ctl.replaceSelected = checked
            }
            RowLayout {
                visible: root.ctl && root.ctl.confirmationPending
                StudioButton {
                    objectName: "detailConfirmReplace"
                    text: qsTr("Supprimer les retouches")
                    onClicked: root.ctl.confirmReplacement(true)
                }
                StudioButton {
                    objectName: "detailCancelReplace"
                    text: qsTr("Annuler")
                    onClicked: root.ctl.confirmReplacement(false)
                }
            }
            StudioButton {
                objectName: "detailCancelTask"
                visible: root.ctl && root.ctl.busy
                text: qsTr("Annuler le calcul")
                onClicked: root.ctl.cancel()
            }
        }
    }
}
