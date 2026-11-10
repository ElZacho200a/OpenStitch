// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

// Thread plan, sewing order, charts and colourways. Adapter over ThreadController: nothing is
// computed here and every change is one undo step on the shared document.
Rectangle {
    id: panel
    objectName: "threadPanel"
    property var controller
    property string page: "plan"
    property var reductionPreview: ({})
    property int renameColorwayIndex: -1
    property int movedThreadIndex: -1
    Timer { id: threadMoveTimer; interval: 850; onTriggered: panel.movedThreadIndex = -1 }
    function moveSelected(delta) {
        const destination = current + delta
        if (controller && controller.moveThread(current, destination)) {
            movedThreadIndex = destination
            threadMoveTimer.restart()
        }
    }
    property string currentId
    readonly property int current: entryList.findIndex(entry => entry.id === currentId)
    property string chartId: controller && controller.charts.length > 0 ? controller.charts[0].id : ""
    readonly property var entryList: controller ? controller.entries : []
    color: Theme.panel
    border.color: Theme.divider
    radius: Theme.radius

    ColorDialog {
        id: recolorDialog
        title: qsTr("Couleur du fil")
        onAccepted: {
            if (panel.current >= 0 && selectedColor.toString() !== panel.entryList[panel.current].color.toString())
                panel.controller.recolorThread(panel.current, selectedColor.toString())
        }
    }

    ColorDialog {
        id: personalColorDialog
        title: qsTr("Couleur du fil personnel")
        onAccepted: personalColorField.text = selectedColor.toString()
    }
    ColorDialog {
        id: fabricColorDialog
        title: qsTr("Couleur du tissu")
        onAccepted: fabricColorField.text = selectedColor.toString()
    }

    Controls.Popup {
        id: reductionDialog
        objectName: "threadReductionDialog"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(360, parent ? parent.width - 32 : 360)
        padding: 16
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.foreground
                font.pixelSize: Theme.bodySize
                text: qsTr("Réduire de %1 à %2 fils (%3 objets modifiés) ?")
                      .arg(panel.reductionPreview.before).arg(panel.reductionPreview.after)
                      .arg(panel.reductionPreview.affected)
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: Theme.smallSize
                text: qsTr("Les couleurs et les références seront remplacées. Ctrl+Z annulera cette opération.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                StudioButton { text: qsTr("Annuler"); onClicked: reductionDialog.close() }
                StudioButton {
                    objectName: "threadReduceConfirm"
                    text: qsTr("Appliquer")
                    emphasized: true
                    onClicked: {
                        panel.controller.applyReduction()
                        reductionDialog.close()
                    }
                }
            }
        }
    }
    Controls.Popup {
        id: filmDialog
        objectName: "threadFilmConfirm"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(360, parent ? parent.width - 32 : 360)
        padding: 16
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                color: Theme.foreground
                wrapMode: Text.WordWrap
                text: qsTr("Regrouper les passages d'un même fil réduit les changements de bobine, mais peut modifier les superpositions.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                StudioButton { text: qsTr("Annuler"); onClicked: filmDialog.close() }
                StudioButton {
                    objectName: "threadFilmGroupConfirm"
                    text: qsTr("Regrouper")
                    emphasized: true
                    onClicked: {
                        panel.controller.groupFilmByColor()
                        filmDialog.close()
                    }
                }
            }
        }
    }
    Controls.Popup {
        id: renameDialog
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(330, parent ? parent.width - 32 : 330)
        padding: 16
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: qsTr("Renommer le coloris"); color: Theme.foreground }
            StudioTextField {
                id: renameField
                objectName: "threadColorwayRenameField"
                Layout.fillWidth: true
                placeholderText: qsTr("Nom du coloris")
            }
            RowLayout {
                StudioButton { text: qsTr("Annuler"); onClicked: renameDialog.close() }
                StudioButton {
                    text: qsTr("Renommer")
                    onClicked: {
                        if (panel.controller.renameColorway(panel.renameColorwayIndex, renameField.text))
                            renameDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Importer un nuancier (CSV ou JSON)")
        nameFilters: [qsTr("Nuanciers (*.csv *.json)")]
        onAccepted: panel.controller.importChart(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Exporter la liste des fils")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)")]
        defaultSuffix: "csv"
        onAccepted: panel.controller.exportThreadList(selectedFile)
    }

    Flickable {
        id: threadSections
        objectName: "threadSections"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        height: 34
        contentWidth: threadTabs.childrenRect.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Row {
            id: threadTabs
            spacing: 4
            Repeater {
                model: [
                    { id: "plan", text: qsTr("Projet") },
                    { id: "catalog", text: qsTr("Nuanciers") },
                    { id: "stock", text: qsTr("Mes fils") },
                    { id: "film", text: qsTr("Film couleur") },
                    { id: "colorways", text: qsTr("Coloris") },
                    { id: "fabric", text: qsTr("Tissu") },
                    { id: "order", text: qsTr("Ordre") }
                ]
                delegate: StudioButton {
                    required property var modelData
                    objectName: "threadTab_" + modelData.id
                    text: modelData.text
                    checkable: true
                    checked: panel.page === modelData.id
                    font.bold: checked
                    active: checked
                    onClicked: panel.page = modelData.id
                }
            }
        }
    }

    Flickable {
        objectName: "threadPageScroll"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: threadSections.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.bottomMargin: 10
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        Controls.ScrollBar.vertical: Controls.ScrollBar {}

        ColumnLayout {
            id: content
            width: parent.width - 12
            spacing: 8

            ColumnLayout {
                objectName: "threadProjectSection"
                visible: panel.page === "plan"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("PLAN DE FILS") + "  ·  " + panel.entryList.length }
            Text {
                objectName: "threadEmptyState"
                visible: panel.entryList.length === 0
                text: qsTr("Aucun fil dans ce projet. Les fils apparaîtront ici après la numérisation.")
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Repeater {
                model: panel.entryList
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    objectName: "threadRow" + modelData.index
                    activeFocusOnTab: true
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.label
                    Accessible.selected: panel.current === modelData.index
                    Keys.onReturnPressed: panel.currentId = row.modelData.id
                    Keys.onSpacePressed: panel.currentId = row.modelData.id
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: Theme.radius
                    color: panel.current === modelData.index ? Theme.accentSoft : (modelData.selected ? Theme.raised : (row.activeFocus || threadRowMouse.containsMouse) ? Theme.hover : "transparent")
                    border.width: panel.movedThreadIndex === modelData.index ? 2
                                  : panel.current === modelData.index || row.activeFocus ? 1 : 0
                    border.color: panel.movedThreadIndex === modelData.index ? Theme.success
                                  : row.activeFocus ? Theme.info : Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.animationMs } }
                    Behavior on border.width { NumberAnimation { duration: Theme.feedbackFastMs } }
                    Rectangle {
                        width: 3
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        radius: 1
                        color: Theme.accent
                        visible: panel.current === row.modelData.index
                    }
                    opacity: modelData.visible ? 1 : 0.5
                    Behavior on opacity { NumberAnimation { duration: Theme.feedbackFastMs } }
                    MouseArea { id: threadRowMouse; anchors.fill: parent; hoverEnabled: true; onClicked: panel.currentId = row.modelData.id }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8
                        Rectangle {
                            implicitWidth: 18; implicitHeight: 18; radius: 3
                            color: row.modelData.color
                            border.color: Theme.border
                            Behavior on color { ColorAnimation { duration: Theme.feedbackSlowMs } }
                        }
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: row.modelData.label; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                        Text { text: qsTr("%1 obj.").arg(row.modelData.objectCount); color: Theme.faint; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                    }
                }
            }
            Flow {
                objectName: "threadActionsFlow"
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 4
                enabled: panel.current >= 0
                StudioButton { objectName: "threadSelectButton"; text: qsTr("Sélectionner les objets"); onClicked: panel.controller.selectThreadObjects(panel.current) }
                StudioButton {
                    objectName: "threadMoveUpButton"
                    text: qsTr("Monter")
                    enabled: panel.current > 0
                    disabledReason: qsTr("Sélectionnez un fil qui n'est pas déjà en première position.")
                    onClicked: panel.moveSelected(-1)
                }
                StudioButton {
                    objectName: "threadMoveDownButton"
                    text: qsTr("Descendre")
                    enabled: panel.current >= 0 && panel.current < panel.entryList.length - 1
                    disabledReason: qsTr("Sélectionnez un fil qui n'est pas déjà en dernière position.")
                    onClicked: panel.moveSelected(1)
                }
                StudioButton {
                    objectName: "threadToggleButton"
                    readonly property bool shown: panel.current >= 0 && panel.current < panel.entryList.length ? panel.entryList[panel.current].visible : true
                    text: shown ? qsTr("Masquer ce fil") : qsTr("Afficher ce fil")
                    onClicked: panel.controller.setThreadVisible(panel.current, !shown)
                }
                StudioComboBox {
                    id: mergeBox
                    objectName: "threadMergeBox"
                    implicitWidth: 170
                    displayText: qsTr("Fusionner dans…")
                    model: panel.entryList
                    textRole: "label"
                    onActivated: {
                        panel.controller.mergeThreads(panel.current, currentIndex)
                        panel.currentId = ""
                    }
                }
                StudioButton {
                    objectName: "threadRecolorButton"
                    text: qsTr("Changer la couleur…")
                    enabled: panel.current >= 0 && panel.current < panel.entryList.length
                    onClicked: {
                        recolorDialog.selectedColor = panel.entryList[panel.current].color
                        recolorDialog.open()
                    }
                }
            }
            Flow {
                objectName: "threadReductionActions"
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 6
                StudioSpinBox {
                    id: reductionTarget
                    objectName: "threadReductionTarget"
                    from: 1; to: 64
                    value: 8
                    editable: true
                }
                StudioButton {
                    objectName: "threadReductionPreview"
                    text: qsTr("Réduire à N fils…")
                    enabled: panel.entryList.length > 1
                    onClicked: {
                        const result = panel.controller.previewReduction(reductionTarget.value)
                        if (result.before !== undefined) {
                            panel.reductionPreview = result
                            reductionDialog.open()
                        }
                    }
                }
            }
            StudioButton { text: qsTr("Exporter la liste (CSV)"); onClicked: exportDialog.open() }

            }
            ColumnLayout {
                objectName: "threadCatalogSection"
                visible: panel.page === "catalog"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("NUANCIERS") }
            Flow {
                objectName: "threadChartActionsFlow"
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 6
                StudioComboBox {
                    id: chartBox
                    objectName: "chartBox"
                    width: Math.min(240, Math.max(150, parent.width - 160))
                    model: panel.controller ? panel.controller.charts : []
                    textRole: "name"
                    onActivated: panel.chartId = model[currentIndex].id
                }
                StudioButton { text: qsTr("Importer"); onClicked: importDialog.open() }
                StudioButton {
                    objectName: "chartRemoveButton"
                    text: qsTr("Retirer")
                    enabled: chartBox.currentIndex >= 0 && panel.controller.charts[chartBox.currentIndex].user
                    onClicked: panel.controller.removeChart(panel.chartId)
                }
            }
            Text {
                objectName: "demoChartWarning"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: panel.controller && chartBox.currentIndex >= 0
                         && chartBox.currentIndex < panel.controller.charts.length
                         && panel.controller.charts[chartBox.currentIndex].source.indexOf("DONNEES PLACEHOLDER") === 0
                text: qsTr("Nuancier de démonstration : noms, références et couleurs fictifs. "
                           + "Ne pas l'utiliser pour choisir des fils réels ; importez un nuancier vérifié.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
            Flow {
                objectName: "threadSearchActionsFlow"
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 6
                StudioTextField {
                    id: searchField
                    objectName: "chartSearchField"
                    width: Math.min(220, Math.max(140, parent.width - 180))
                    placeholderText: qsTr("Nom ou référence")
                }
                StudioButton { objectName: "nearestButton"; text: qsTr("Plus proches"); enabled: panel.current >= 0; onClicked: resultList.model = panel.controller.nearestToEntry(panel.current, panel.chartId, 8) }
                StudioButton { objectName: "searchButton"; text: qsTr("Chercher"); onClicked: resultList.model = panel.controller.searchThreads(panel.chartId, searchField.text) }
            }
            StudioButton { text: qsTr("Couleurs libres → fil le plus proche"); onClicked: panel.controller.snapFreeColours(panel.chartId) }
            ListView {
                id: resultList
                objectName: "chartResults"
                Layout.fillWidth: true
                implicitHeight: Math.min(180, contentHeight)
                clip: true
                model: []
                delegate: Rectangle {
                    id: result
                    required property var modelData
                    width: ListView.view.width
                    height: 28
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Rectangle { implicitWidth: 16; implicitHeight: 16; radius: 3; color: result.modelData.color; border.color: Theme.border }
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: result.modelData.code + "  " + result.modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                        StudioButton {
                            objectName: "threadCatalogOwn"
                            text: {
                                const count = panel.controller.stockCount
                                return panel.controller.ownsThread(result.modelData.chartId,
                                                                  result.modelData.code)
                                       ? qsTr("Possédé") : qsTr("+ Mes fils")
                            }
                            quiet: true
                            onClicked: panel.controller.setOwnedThread(
                                result.modelData.chartId, result.modelData.code,
                                !panel.controller.ownsThread(result.modelData.chartId,
                                                             result.modelData.code))
                        }
                        StudioButton {
                            text: qsTr("Assigner")
                            enabled: panel.current >= 0
                            quiet: true
                            onClicked: panel.controller.assignChartThread(panel.current, result.modelData.chartId, result.modelData.code)
                        }
                    }
                }
            }

            }
            ColumnLayout {
                objectName: "threadColorwaysSection"
                visible: panel.page === "colorways"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("COLORIS") }
            Repeater {
                model: panel.controller ? panel.controller.colorways : []
                delegate: RowLayout {
                    id: way
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: (way.modelData.active ? "● " : "○ ") + way.modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                    StudioButton { objectName: "colorwayActivate" + way.index; text: qsTr("Activer"); quiet: true; enabled: !way.modelData.active; onClicked: panel.controller.activateColorway(way.index) }
                    StudioButton {
                        objectName: "colorwayRename" + way.index
                        text: qsTr("Renommer")
                        quiet: true
                        onClicked: {
                            panel.renameColorwayIndex = way.index
                            renameField.text = way.modelData.name
                            renameDialog.open()
                        }
                    }
                    StudioButton { objectName: "colorwayRemove" + way.index; text: qsTr("Supprimer"); quiet: true; enabled: !way.modelData.active; onClicked: panel.controller.removeColorway(way.index) }
                }
            }
            Flow {
                objectName: "threadColorwayActionsFlow"
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 6
                StudioTextField { id: colorwayName; objectName: "colorwayNameField"; width: Math.min(250, Math.max(140, parent.width - 88)); placeholderText: qsTr("Nom du nouveau coloris") }
                StudioButton { objectName: "colorwayAddButton"; text: qsTr("Créer"); onClicked: { if (panel.controller.addColorway(colorwayName.text)) colorwayName.text = "" } }
            }

            }
            ColumnLayout {
                objectName: "threadOrderSection"
                visible: panel.page === "order"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("ORDRE DE COUTURE") }
            Repeater {
                model: panel.controller ? panel.controller.sewingOrder : []
                delegate: RowLayout {
                    id: ord
                    required property var modelData
                    Layout.fillWidth: true
                    Rectangle { implicitWidth: 14; implicitHeight: 14; radius: 3; color: ord.modelData.color; border.color: Theme.border }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: (ord.modelData.position + 1) + ". " + ord.modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize }
                    StudioButton { objectName: "orderUp" + ord.modelData.position; text: "▲"; quiet: true; enabled: ord.modelData.position > 0; onClicked: panel.controller.moveInOrder(ord.modelData.id, ord.modelData.position - 1) }
                    StudioButton { objectName: "orderDown" + ord.modelData.position; text: "▼"; quiet: true; onClicked: panel.controller.moveInOrder(ord.modelData.id, ord.modelData.position + 1) }
                }
            }
            }
            ColumnLayout {
                objectName: "threadStockSection"
                visible: panel.page === "stock"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("MES FILS · %1 références").arg(panel.controller ? panel.controller.stockCount : 0) }
                Text {
                    Layout.fillWidth: true
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    text: qsTr("Vos bobines sont conservées sur cet ordinateur, pas dans le projet OSP.")
                }
                StudioCheckBox {
                    objectName: "threadLimitToStock"
                    text: qsTr("Limiter la recherche et les couleurs proches à mes fils")
                    checked: panel.controller ? panel.controller.limitToStock : false
                    onToggled: if (panel.controller) panel.controller.limitToStock = checked
                }
                Repeater {
                    model: panel.controller ? panel.controller.ownedThreads : []
                    delegate: RowLayout {
                        id: stockRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            implicitWidth: 16; implicitHeight: 16; radius: 3
                            color: stockRow.modelData.color
                            border.color: Theme.border
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Theme.foreground
                            font.pixelSize: Theme.smallSize
                            text: stockRow.modelData.code + " · " + stockRow.modelData.name
                        }
                        StudioButton {
                            text: qsTr("Assigner")
                            quiet: true
                            enabled: panel.current >= 0
                            onClicked: panel.controller.assignOwnedThread(
                                panel.current, stockRow.modelData.chartId, stockRow.modelData.code)
                        }
                        StudioButton {
                            text: qsTr("Retirer")
                            quiet: true
                            onClicked: panel.controller.removeOwnedThread(
                                stockRow.modelData.chartId, stockRow.modelData.code)
                        }
                    }
                }
                SectionLabel { text: qsTr("AJOUTER UN FIL PERSONNEL") }
                StudioTextField {
                    id: personalCodeField
                    objectName: "threadPersonalCode"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Référence (obligatoire)")
                }
                StudioTextField {
                    id: personalNameField
                    objectName: "threadPersonalName"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nom du fil")
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 6
                    StudioTextField {
                        id: personalColorField
                        objectName: "threadPersonalColor"
                        width: 125
                        text: Qt.rgba(1, 1, 1, 1).toString()
                        placeholderText: qsTr("#RRGGBB")
                    }
                    StudioButton {
                        objectName: "threadPersonalColorPick"
                        text: qsTr("Choisir…")
                        onClicked: {
                            personalColorDialog.selectedColor = personalColorField.text
                            personalColorDialog.open()
                        }
                    }
                    StudioButton {
                        objectName: "threadPersonalAdd"
                        text: qsTr("Ajouter")
                        emphasized: true
                        onClicked: {
                            if (panel.controller.addPersonalThread(personalCodeField.text,
                                                                   personalNameField.text,
                                                                   personalColorField.text)) {
                                personalCodeField.text = ""
                                personalNameField.text = ""
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                objectName: "threadFilmSection"
                visible: panel.page === "film"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("FILM COULEUR") }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    text: qsTr("%1 changements de fil · passages dans l'ordre réel de couture")
                          .arg(panel.controller ? panel.controller.filmChanges : 0)
                }
                Repeater {
                    model: panel.controller ? panel.controller.colorFilm : []
                    delegate: RowLayout {
                        id: filmRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 5
                        Rectangle {
                            implicitWidth: 15; implicitHeight: 15; radius: 3
                            color: filmRow.modelData.color
                            border.color: Theme.border
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: Theme.foreground
                                font.pixelSize: Theme.bodySize
                                text: (filmRow.modelData.index + 1) + ". " + filmRow.modelData.label
                            }
                            Text {
                                Layout.fillWidth: true
                                color: Theme.faint
                                font.pixelSize: Theme.smallSize
                                text: qsTr("%1 obj. · %2 points%3")
                                      .arg(filmRow.modelData.objectCount)
                                      .arg(filmRow.modelData.stitches)
                                      .arg(filmRow.modelData.locked ? qsTr(" · ordre figé") : "")
                            }
                        }
                        StudioButton {
                            objectName: "filmSelect" + filmRow.modelData.index
                            quiet: true
                            text: qsTr("Voir")
                            onClicked: panel.controller.selectFilmBlock(filmRow.modelData.index)
                        }
                        StudioButton {
                            objectName: "filmUp" + filmRow.modelData.index
                            quiet: true
                            text: "↑"
                            enabled: filmRow.modelData.index > 0 && !filmRow.modelData.locked
                            onClicked: panel.controller.moveFilmBlock(filmRow.modelData.index,
                                                                       filmRow.modelData.index - 1)
                        }
                        StudioButton {
                            objectName: "filmDown" + filmRow.modelData.index
                            quiet: true
                            text: "↓"
                            enabled: !filmRow.modelData.locked &&
                                     panel.controller &&
                                     filmRow.modelData.index < panel.controller.colorFilm.length - 1
                            onClicked: panel.controller.moveFilmBlock(filmRow.modelData.index,
                                                                       filmRow.modelData.index + 1)
                        }
                    }
                }
                StudioButton {
                    objectName: "filmGroupByColor"
                    text: qsTr("Regrouper les passages d'une même couleur…")
                    enabled: panel.controller && panel.controller.colorFilm.length > 1
                    onClicked: filmDialog.open()
                }
            }
            ColumnLayout {
                objectName: "threadFabricSection"
                visible: panel.page === "fabric"
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: qsTr("TISSU DU PROJET") }
                Text {
                    Layout.fillWidth: true
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    text: qsTr("Couleur de fond, matière et stabilisateur, conservés dans le projet OSP.")
                }
                StudioTextField {
                    id: fabricNameField
                    objectName: "threadFabricName"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nom du tissu")
                    text: panel.controller ? panel.controller.fabric.name : ""
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 6
                    StudioTextField {
                        id: fabricColorField
                        objectName: "threadFabricColor"
                        width: 126
                        text: panel.controller ? panel.controller.fabric.color.toString() : Qt.rgba(1, 1, 1, 1).toString()
                    }
                    StudioButton {
                        objectName: "threadFabricColorPick"
                        text: qsTr("Choisir une couleur")
                        onClicked: {
                            fabricColorDialog.selectedColor = fabricColorField.text
                            fabricColorDialog.open()
                        }
                    }
                }
                StudioTextField {
                    id: fabricMaterialField
                    objectName: "threadFabricMaterial"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Matière (woven, jersey, denim…)")
                    text: panel.controller ? panel.controller.fabric.material : ""
                }
                StudioTextField {
                    id: fabricStabilizerField
                    objectName: "threadFabricStabilizer"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Stabilisateur (cut-away, tear-away…)")
                    text: panel.controller ? panel.controller.fabric.stabilizer : ""
                }
                Text { text: qsTr("Angle de chaîne (°)"); color: Theme.muted }
                StudioSpinBox {
                    id: fabricGrainField
                    objectName: "threadFabricGrain"
                    from: -360; to: 360
                    value: panel.controller ? Math.round(panel.controller.fabric.grainAngleDeg) : 0
                    editable: true
                }
                Text { text: qsTr("Souplesse trame × 100 (25 à 400)"); color: Theme.muted }
                StudioSpinBox {
                    id: fabricSoftnessField
                    objectName: "threadFabricSoftness"
                    from: 25; to: 400
                    value: panel.controller ? Math.round(panel.controller.fabric.weftSoftness * 100) : 100
                    editable: true
                }
                StudioButton {
                    objectName: "threadFabricApply"
                    text: qsTr("Appliquer au projet")
                    emphasized: true
                    onClicked: panel.controller.setFabric(
                        fabricColorField.text, fabricNameField.text,
                        fabricMaterialField.text, fabricStabilizerField.text,
                        fabricGrainField.value, fabricSoftnessField.value / 100)
                }
            }
            Text {
                objectName: "threadMessage"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.controller ? panel.controller.message : ""
                color: panel.controller && !panel.controller.lastOk ? Theme.error : Theme.success
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
        }
    }
}
