// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

Item {
    id: hub
    objectName: "threadInventoryHub"
    Loader {
        anchors.fill: parent
        active: typeof threadInventory !== "undefined" && threadInventory !== null
        sourceComponent: Item {
            readonly property var ctl: threadInventory
            StudioButton {
                id: toggle
                objectName: "threadInventoryToggle"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 18
                text: qsTr("Mes fils et coloris")
                tooltip: qsTr("Stock personnel, comparaison des fils et aperçus de coloris")
                onClicked: dialog.open()
            }
            FileDialog {
                id: importDialog
                title: qsTr("Importer un nuancier")
                nameFilters: [qsTr("Nuanciers (*.csv *.json)")]
                onAccepted: ctl.importChart(selectedFile)
            }
            Controls.Popup {
                id: dialog
                objectName: "threadInventoryDialog"
                property var searchResults: []
                property var comparison: ({})
                property var suggestion: ({})
                modal: true
                focus: true
                parent: Controls.Overlay.overlay
                anchors.centerIn: parent
                width: Math.min(800, parent ? parent.width - 32 : 800)
                height: Math.min(760, parent ? parent.height - 32 : 760)
                padding: 18
                onClosed: toggle.forceActiveFocus()
                background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
                function refreshSearch() {
                    searchResults = ctl.search(chart.currentIndex >= 0 ? ctl.charts[chart.currentIndex].id : "", query.text)
                }
                Connections {
                    target: ctl
                    function onStockChanged() { dialog.refreshSearch() }
                    function onChartsChanged() { dialog.refreshSearch() }
                }
                contentItem: ColumnLayout {
                    Accessible.name: qsTr("Mes fils et coloris")
                    spacing: 12
                    RowLayout {
                        Text { text: qsTr("Mes fils et coloris"); color: Theme.foreground; font.pixelSize: Theme.headingSize; font.family: Theme.fontFamily; Layout.fillWidth: true }
                        StudioButton { objectName: "threadInventoryClose"; text: qsTr("Fermer"); onClicked: dialog.close() }
                    }
                    Controls.ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 12
                            SectionLabel { text: qsTr("STOCK PERSONNEL") }
                            Controls.Label { text: qsTr("Le stock est enregistré dans votre profil. Il ne modifie pas la broderie."); color: Theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            ListView {
                                objectName: "ownedThreads"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 120
                                clip: true
                                model: ctl.stock
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 36
                                    Rectangle { implicitWidth: 22; implicitHeight: 22; color: modelData.color; border.color: Theme.border }
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.code + " · " + (modelData.name || modelData.chartId); color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                                    StudioButton { objectName: "stockRemove_" + modelData.code; text: qsTr("Retirer"); onClicked: ctl.removeStock(modelData.chartId, modelData.code) }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: ctl.loaded
                                Controls.TextField { id: personalCode; objectName: "personalThreadCode"; Layout.fillWidth: true; placeholderText: qsTr("Référence"); Accessible.name: qsTr("Référence du fil personnel") }
                                Controls.TextField { id: personalName; objectName: "personalThreadName"; Layout.fillWidth: true; placeholderText: qsTr("Nom"); Accessible.name: qsTr("Nom du fil personnel") }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: ctl.loaded
                                Controls.TextField { id: personalBrand; objectName: "personalThreadBrand"; Layout.fillWidth: true; placeholderText: qsTr("Marque (facultatif)"); Accessible.name: qsTr("Marque du fil personnel") }
                                Controls.TextField { id: personalColor; objectName: "personalThreadColor"; text: "#cc3355"; Layout.preferredWidth: 100; Accessible.name: qsTr("Couleur RGB du fil personnel") }
                                StudioButton { objectName: "personalThreadAdd"; text: qsTr("Ajouter / modifier"); onClicked: ctl.addPersonal(personalCode.text, personalName.text, personalColor.text, personalBrand.text) }
                            }
                            StudioButton { objectName: "stockReload"; text: qsTr("Recharger le stock"); onClicked: ctl.reload() }
                            SectionLabel { text: qsTr("NUANCIERS") }
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.ComboBox { id: chart; objectName: "inventoryChart"; Layout.fillWidth: true; model: ctl.charts; textRole: "name"; Accessible.name: qsTr("Nuancier à parcourir"); onActivated: dialog.refreshSearch() }
                                StudioButton { objectName: "inventoryImport"; text: qsTr("Importer…"); onClicked: importDialog.open() }
                                StudioButton { objectName: "inventoryRemoveChart"; text: qsTr("Retirer le nuancier"); enabled: chart.currentIndex >= 0 && ctl.charts[chart.currentIndex].user; onClicked: ctl.removeChart(ctl.charts[chart.currentIndex].id) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.TextField { id: query; objectName: "inventorySearchText"; Layout.fillWidth: true; placeholderText: qsTr("Référence ou nom"); Accessible.name: qsTr("Rechercher un fil dans le nuancier"); onAccepted: dialog.refreshSearch() }
                                StudioButton { objectName: "inventorySearch"; text: qsTr("Chercher"); onClicked: dialog.refreshSearch() }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 120
                                clip: true
                                model: dialog.searchResults
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 36
                                    Rectangle { implicitWidth: 22; implicitHeight: 22; color: modelData.color; border.color: Theme.border }
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.code + " · " + modelData.name; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                                    Controls.CheckBox { objectName: "ownThread_" + modelData.code; text: qsTr("Je possède ce fil"); checked: ctl.owns(modelData.chartId, modelData.code); enabled: ctl.loaded; Accessible.name: qsTr("Je possède le fil %1").arg(modelData.code); onToggled: ctl.setOwned(modelData.chartId, modelData.code, checked) }
                                }
                            }
                            SectionLabel { text: qsTr("COMPARER ET MÉLANGER") }
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.TextField { id: colorA; objectName: "inventoryColorA"; text: "#000000"; Layout.fillWidth: true; Accessible.name: qsTr("Couleur du premier fil") }
                                Controls.TextField { id: colorB; objectName: "inventoryColorB"; text: "#ffffff"; Layout.fillWidth: true; Accessible.name: qsTr("Couleur du second fil") }
                                Controls.SpinBox { id: ratio; objectName: "inventoryRatio"; from: 0; to: 100; value: 50; editable: true; Accessible.name: qsTr("Pourcentage du second fil") }
                                StudioButton { objectName: "inventoryCompare"; text: qsTr("Comparer"); onClicked: dialog.comparison = ctl.compareColors(colorA.text, colorB.text, ratio.value / 100.0) }
                            }
                            RowLayout {
                                Rectangle { implicitWidth: 42; implicitHeight: 26; color: dialog.comparison.mixed !== undefined ? dialog.comparison.mixed : "transparent"; border.color: Theme.border }
                                Text { objectName: "inventoryDeltaE"; text: dialog.comparison.difference !== undefined ? "ΔE00 : " + dialog.comparison.difference.toFixed(3) : "ΔE00 : —"; color: Theme.foreground; font.family: Theme.fontFamily }
                                StudioButton { objectName: "inventorySuggest"; text: qsTr("Suggérer un mélange pour la première couleur"); enabled: chart.currentIndex >= 0; onClicked: dialog.suggestion = ctl.suggest(colorA.text, ctl.charts[chart.currentIndex].id) }
                            }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: dialog.suggestion.a !== undefined ? dialog.suggestion.a.code + " + " + dialog.suggestion.b.code + " · " + Math.round(dialog.suggestion.ratioB * 100) + " % B · ΔE00 " + dialog.suggestion.mixedError.toFixed(3) : ""; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                            Controls.Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Le mélange est une estimation en lumière linéaire. Vérifiez le résultat sur tissu."); color: Theme.muted }
                            SectionLabel { text: qsTr("APERÇUS DES COLORIS DU DOCUMENT") }
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.ComboBox { id: variant; objectName: "inventoryColorway"; Layout.fillWidth: true; model: ctl.colorways; textRole: "name"; Accessible.name: qsTr("Coloris à prévisualiser") }
                                StudioButton { objectName: "inventoryPreview"; text: qsTr("Prévisualiser"); enabled: variant.currentIndex >= 0 && !ctl.busy; onClicked: ctl.previewColorway(variant.currentIndex) }
                                StudioButton { objectName: "inventoryCancelPreview"; text: qsTr("Annuler"); enabled: ctl.busy || ctl.previewIndex >= 0; onClicked: ctl.clearPreview() }
                                StudioButton { objectName: "inventoryActivate"; text: qsTr("Activer ce coloris"); enabled: ctl.previewIndex >= 0 && !ctl.busy; onClicked: ctl.activatePreview() }
                            }
                            Image { objectName: "inventoryColorwayImage"; Layout.fillWidth: true; Layout.preferredHeight: 210; source: ctl.previewUrl; fillMode: Image.PreserveAspectFit; cache: false; Accessible.name: qsTr("Aperçu du coloris sans modification du document") }
                        }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.message; color: ctl.messageOk ? Theme.muted : Theme.accent; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize }
                }
            }
        }
    }
}
