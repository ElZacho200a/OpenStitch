// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Réglage rapide du cadre, ouvert depuis l'en-tête ou le clic droit de n'importe quel canevas :
// forme, largeur, hauteur, rayon des coins, marge, pivoter, modèles et « Mes cadres ». Chaque
// réglage s'applique en direct (après une courte pause de saisie) en UNE commande annulable via
// `FileController::applyHoopSpec` ; aucun calcul ici.
Controls.Popup {
    id: pop
    objectName: "hoopPopover"
    required property var ctl // FileController (« files »)
    // Cadre du document, relu à chaque changement du document (annuler compris).
    property var hoop: ({})
    property string shape: "rectangle"
    property bool syncing: false

    width: 340
    padding: 14
    modal: false
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
    background: StudioPopupBackground { }
    enter: StudioPopupEnter { }
    exit: StudioPopupExit { }

    // Ouvre le panneau au point (x, y) de `item`, gardé dans la fenêtre.
    function openAt(item, x, y) {
        const host = parent ? parent : item
        const p = item.mapToItem(host, x, y)
        pop.x = Math.max(8, Math.min(p.x, host.width - pop.width - 8))
        pop.y = Math.max(8, Math.min(p.y, host.height - pop.implicitHeight - 8))
        reload()
        open()
    }
    function reload() {
        if (!ctl || applyTimer.running)
            return
        syncing = true
        hoop = ctl.currentHoop()
        shape = hoop.shape || "rectangle"
        widthSpin.value = Math.round(hoop.widthMm || 100)
        heightSpin.value = Math.round(hoop.heightMm || 100)
        radiusSpin.value = Math.round(hoop.cornerRadiusMm || 0)
        marginSpin.value = Math.round(hoop.marginMm || 0)
        syncing = false
    }
    function schedule() {
        if (!syncing)
            applyTimer.restart()
    }
    function applyNow() {
        applyTimer.stop()
        if (!ctl)
            return
        ctl.applyHoopSpec(shape, widthSpin.value, heightSpin.value,
                          shape === "rounded" ? radiusSpin.value : 0, marginSpin.value)
        reload()
    }
    function applyEntry(entry, keepMargin) {
        applyTimer.stop()
        ctl.applyHoopSpec(entry.shape || "rectangle", entry.widthMm, entry.heightMm,
                          entry.cornerRadiusMm || 0,
                          keepMargin ? marginSpin.value : (entry.marginMm || 0))
        reload()
    }
    function shapeLabel(s) {
        return s === "ellipse" ? qsTr("rond/ovale") : s === "rounded" ? qsTr("coins arrondis") : qsTr("rectangle")
    }

    Timer { id: applyTimer; interval: 350; onTriggered: pop.applyNow() }
    Connections {
        target: typeof studio !== "undefined" ? studio : null
        function onChanged() { if (pop.opened) pop.reload() }
    }
    onClosed: if (applyTimer.running) applyNow()

    component FieldLabel: Text {
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Cadre")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
            }
            Text {
                objectName: "hoopPopoverLabel"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                text: (pop.hoop.label && pop.hoop.label.length > 0 ? pop.hoop.label : qsTr("Personnalisé"))
                      + " · " + pop.shapeLabel(pop.shape)
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }
        }

        // Forme du cadre.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [{ id: "rectangle", text: qsTr("Rectangle") },
                        { id: "ellipse", text: qsTr("Rond / ovale") },
                        { id: "rounded", text: qsTr("Arrondi") }]
                StudioButton {
                    required property var modelData
                    objectName: "hoopShape_" + modelData.id
                    Layout.fillWidth: true
                    text: modelData.text
                    active: pop.shape === modelData.id
                    tooltip: qsTr("Forme du cadre : %1").arg(modelData.text)
                    onClicked: { pop.shape = modelData.id; pop.schedule() }
                }
            }
        }

        // Dimensions : saisie directe, appliquée en direct.
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 6
            FieldLabel { text: qsTr("Largeur (mm)") }
            FieldLabel { text: qsTr("Hauteur (mm)") }
            Item { implicitWidth: 1; implicitHeight: 1 }
            StudioSpinBox {
                id: widthSpin
                objectName: "hoopWidthSpin"
                Layout.fillWidth: true
                from: 10; to: 1000; editable: true
                onValueModified: pop.schedule()
            }
            StudioSpinBox {
                id: heightSpin
                objectName: "hoopHeightSpin"
                Layout.fillWidth: true
                from: 10; to: 1000; editable: true
                onValueModified: pop.schedule()
            }
            StudioButton {
                objectName: "hoopRotateButton"
                text: qsTr("Pivoter")
                tooltip: qsTr("Échanger largeur et hauteur (cadre en paysage ou en portrait)")
                onClicked: {
                    const w = widthSpin.value
                    widthSpin.value = heightSpin.value
                    heightSpin.value = w
                    pop.applyNow()
                }
            }
            FieldLabel { text: qsTr("Rayon des coins (mm)"); opacity: pop.shape === "rounded" ? 1 : 0.5 }
            FieldLabel { text: qsTr("Marge de sécurité (mm)") }
            Item { implicitWidth: 1; implicitHeight: 1 }
            StudioSpinBox {
                id: radiusSpin
                objectName: "hoopRadiusSpin"
                Layout.fillWidth: true
                from: 0; to: 200; editable: true
                enabled: pop.shape === "rounded"
                onValueModified: pop.schedule()
            }
            StudioSpinBox {
                id: marginSpin
                objectName: "hoopMarginSpin"
                Layout.fillWidth: true
                from: 0; to: 100; editable: true
                onValueModified: pop.schedule()
            }
            Item { implicitWidth: 1; implicitHeight: 1 }
        }

        // Modèles : Mes cadres d'abord, puis la bibliothèque générique.
        StudioComboBox {
            id: presetCombo
            objectName: "hoopPresetCombo"
            Layout.fillWidth: true
            readonly property var entries: {
                const out = []
                const mine = pop.ctl ? pop.ctl.userHoops : []
                for (let i = 0; i < mine.length; ++i)
                    out.push(Object.assign({ mine: true, label: "★ " + mine[i].name }, mine[i]))
                const lib = pop.ctl ? pop.ctl.hoops : []
                for (let j = 0; j < lib.length; ++j)
                    out.push(Object.assign({ mine: false, label: lib[j].family + " — " + lib[j].name }, lib[j]))
                return out
            }
            model: entries
            textRole: "label"
            displayText: qsTr("Choisir un modèle…")
            onActivated: function(index) { pop.applyEntry(entries[index], !entries[index].mine) }
        }

        // Mes cadres : enregistrer le cadre actuel, retirer un cadre.
        SectionLabel { text: qsTr("MES CADRES") }
        RowLayout {
            Layout.fillWidth: true
            StudioTextField {
                id: hoopName
                objectName: "hoopSaveName"
                Layout.fillWidth: true
                placeholderText: qsTr("Nom (ex. « Brother 5×7 »)")
                onAccepted: saveButton.clicked()
            }
            StudioButton {
                id: saveButton
                objectName: "hoopSaveButton"
                text: qsTr("Enregistrer")
                enabled: hoopName.text.trim().length > 0
                tooltip: qsTr("Ajouter le cadre actuel à « Mes cadres »")
                onClicked: {
                    pop.applyNow()
                    if (pop.ctl.saveCurrentHoopAs(hoopName.text) === "") {
                        hoopName.text = ""
                        pop.reload()
                    }
                }
            }
        }
        Repeater {
            model: pop.ctl ? pop.ctl.userHoops : []
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 4
                StudioButton {
                    Layout.fillWidth: true
                    quiet: true
                    text: modelData.name + " — " + Math.round(modelData.widthMm) + " × "
                          + Math.round(modelData.heightMm) + " mm"
                    tooltip: qsTr("Appliquer ce cadre")
                    onClicked: pop.applyEntry(modelData, false)
                }
                StudioButton {
                    quiet: true
                    text: "×"
                    tooltip: qsTr("Retirer « %1 » de Mes cadres").arg(modelData.name)
                    onClicked: pop.ctl.removeUserHoop(modelData.name)
                }
            }
        }
    }
}
