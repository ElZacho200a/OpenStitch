// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

Controls.Dialog {
    id: dialog
    objectName: "advancedExportDialog"
    property var controller: null
    property var fileController: null
    property var values: ({})
    property string format: "dst"
    modal: true
    focus: true
    title: qsTr("Exporter pour la machine")
    parent: Controls.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(580, parent ? parent.width - 32 : 580)
    height: Math.min(760, parent ? parent.height - 32 : 760)
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
    function setValue(key, value) { var copy = Object.assign({}, values); copy[key] = value; values = copy }
    function showOptions() {
        if (!controller) return
        values = controller.options()
        format = values.format
        open()
    }
    Connections { target: dialog.controller; function onDialogRequested() { dialog.showOptions() } }
    component Flag: Controls.CheckBox {
        required property string option
        checked: dialog.values[option] === true
        onToggled: dialog.setValue(option, checked)
        Layout.fillWidth: true
    }
    FileDialog {
        id: output
        objectName: "advancedExportFile"
        title: qsTr("Enregistrer la broderie machine")
        fileMode: FileDialog.SaveFile
        defaultSuffix: dialog.format
        nameFilters: [dialog.format.toUpperCase() + " (*." + dialog.format + ")"]
        onAccepted: dialog.controller.exportMachine(selectedFile, dialog.values)
    }
    FileDialog {
        id: cutOutput
        title: qsTr("Contour de découpe d'appliqué")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        nameFilters: ["SVG (*.svg)", "DXF (*.dxf)"]
        onAccepted: dialog.controller.exportAppliqueCut(selectedFile)
    }
    contentItem: Controls.ScrollView {
        clip: true
        ColumnLayout {
            width: dialog.availableWidth - 16
            spacing: 8
            Controls.ComboBox {
                objectName: "advancedExportFormat"
                Layout.fillWidth: true
                model: dialog.fileController ? dialog.fileController.machineFormats : []
                textRole: "label"
                onActivated: {
                    dialog.format = model[currentIndex].extension
                    dialog.setValue("format", dialog.format)
                    if (dialog.format !== "dst") dialog.setValue("compensate", false)
                }
            }
            SectionLabel { text: qsTr("RÉGLAGES DU DOCUMENT — ANNULABLES") }
            Flag { option: "finishingEnabled"; text: qsTr("Appliquer les finitions") }
            Flag { option: "trimBeforeColorChange"; text: qsTr("Couper avant les changements de couleur") }
            Flag { option: "filterShortStitches"; text: qsTr("Filtrer les points trop courts") }
            Flag { option: "splitLongStitches"; text: qsTr("Découper les points trop longs") }
            Flag { option: "autoJoin"; text: qsTr("Relier les trajets cachés du document") }
            SectionLabel { text: qsTr("OPTIONS DU FICHIER") }
            Flag { objectName: "exportReduceTrims"; option: "reduceTrims"; text: qsTr("Optimiser les trajets pour réduire coupes et sauts") }
            Flag { option: "betweenObjects"; enabled: dialog.values.reduceTrims === true; text: qsTr("Relier entre les objets si le trajet sera recouvert") }
            Flag { option: "withinObject"; enabled: dialog.values.reduceTrims === true; text: qsTr("Relier sous-couche et remplissage dans un objet") }
            Flag { option: "reorderUnderlay"; enabled: dialog.values.reduceTrims === true; text: qsTr("Réordonner les passes de sous-couche") }
            Flag { option: "reorderRegions"; enabled: dialog.values.reduceTrims === true; text: qsTr("Réordonner les zones d'une même couleur (modifie leur ordre)") }
            Flag { option: "sameColorSurface"; enabled: dialog.values.reduceTrims === true; text: qsTr("Accepter une surface déjà cousue du même fil") }
            RowLayout {
                Controls.Label { text: qsTr("Pont maximal (mm)"); Layout.fillWidth: true }
                Controls.SpinBox { from: 0; to: 100; value: dialog.values.maxBridgeMm !== undefined ? dialog.values.maxBridgeMm : 20; editable: true; onValueModified: dialog.setValue("maxBridgeMm", value) }
            }
            Flag { objectName: "exportCompanion"; option: "colorCompanion"; text: qsTr("Écrire un fichier de couleurs à côté (DST / EXP)") }
            Controls.ComboBox { model: ["CSV", "INF"]; onActivated: dialog.setValue("companionKind", currentIndex === 0 ? "csv" : "inf") }
            Flag { option: "trims"; enabled: dialog.format !== "dst"; text: qsTr("Écrire les coupes de fil dans le fichier") }
            Flag { option: "colorChanges"; enabled: dialog.format !== "dst"; text: qsTr("Écrire les changements de couleur") }
            Controls.ComboBox {
                enabled: dialog.format !== "dst"
                model: [qsTr("Arrêts natifs"), qsTr("Arrêts comme changement de couleur"), qsTr("Supprimer les arrêts")]
                onActivated: dialog.setValue("stops", currentIndex)
            }
            Controls.Label { visible: dialog.format === "dst"; text: qsTr("Le DST conserve son codage des coupes, arrêts et couleurs."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Flag { objectName: "exportCompensated"; option: "compensate"; enabled: dialog.format === "dst"; text: qsTr("DST compensé du décalage entre couleurs") }
            Controls.Label { visible: dialog.values.compensate === true; text: qsTr("Estimation non calibrée : vérifier sur une pièce-test avant production."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                visible: dialog.values.compensate === true
                Controls.Label { text: qsTr("Force (%)"); Layout.fillWidth: true }
                Controls.SpinBox { from: 0; to: 100; value: 100; onValueModified: dialog.setValue("strength", value / 100) }
            }
            Controls.Label { objectName: "advancedExportStatus"; text: dialog.controller ? dialog.controller.message : ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Controls.Label { text: qsTr("Le cadre, le champ machine et les limites du format sont contrôlés avant l'écriture."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            StudioButton { objectName: "exportCut"; text: qsTr("Exporter le contour de découpe d'appliqué…"); enabled: dialog.controller && !dialog.controller.busy; onClicked: cutOutput.open() }
        }
    }
    footer: RowLayout {
        StudioButton { text: qsTr("Fermer"); onClicked: dialog.close() }
        StudioButton { text: qsTr("Annuler le calcul"); visible: dialog.controller && dialog.controller.busy; onClicked: dialog.controller.cancel() }
        StudioButton { objectName: "advancedExportConfirm"; text: qsTr("Exporter…"); emphasized: true; enabled: dialog.controller && !dialog.controller.busy; onClicked: output.open() }
    }
}
