// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

// Famille A : menu Fichier, contrôle des modifications non enregistrées, récents, glisser-déposer,
// options d'import et d'export, cadre/machine, récupération après interruption. Tout passe par le
// contrôleur `files` (FileController) ; sans lui (tests de la coque seule) chaque entrée retombe
// sur `studio`, et le panneau ne fait alors que les gestes de base.
Item {
    id: panel
    objectName: "filePanel"
    anchors.fill: parent

    readonly property var ctl: (typeof files !== "undefined") ? files : null
    property string exportKind: ""
    property var importValues: ({})
    property string importPath: ""
    property var exportValues: ({})

    // --- API pour Main.qml -----------------------------------------------------------------
    function requestNew() { if (ctl) ctl.requestNew(); else studio.newProject() }
    function requestOpen() { openDialog.open() }
    function openUrl(url) { if (ctl) ctl.openUrl(url); else studio.openProject(url) }
    function requestQuit() { return ctl ? ctl.requestQuit() : true }
    function openMenu() { menu.open() }
    function openExportDialog() { if (typeof machineExport !== "undefined") machineExport.openDialog(); else if (ctl) { exportValues = ctl.exportOptions(); exportDialog.open() } }
    function openHoopDialog() { if (ctl) hoopDialog.open() }
    function importImageDialog() { imageDialog.open() }
    function importVectorDialog() { vectorDialog.open() }
    function importMachineDialog() { machineDialog.open() }

    function guardText(reason) {
        switch (reason) {
        case "nouveau": return qsTr("Créer un nouveau projet")
        case "ouvrir": return qsTr("Ouvrir un autre fichier")
        case "importer": return qsTr("Importer une image")
        case "quitter": return qsTr("Quitter OpenStitch Studio")
        case "récupérer": return qsTr("Récupérer la sauvegarde automatique")
        default: return qsTr("Continuer")
        }
    }

    function startExport(kind, title, filters, suffix) {
        exportKind = kind
        saveExportDialog.title = title
        saveExportDialog.nameFilters = filters
        saveExportDialog.defaultSuffix = suffix
        saveExportDialog.open()
    }

    function finishExport(url) {
        switch (exportKind) {
        case "machine": ctl.exportMachine(url, exportValues); break
        case "threads": ctl.exportThreadList(url); break
        case "dxf": ctl.exportDxf(url); break
        case "svg": ctl.exportSvg(url); break
        case "image": ctl.exportImage(url, 6, 92); break
        case "sheet": ctl.exportProductionSheet(url); break
        case "template": ctl.exportPlacementTemplate(url, {}); break
        }
    }

    // Cadre commun des fenêtres modales du panneau.
    component Frame: Controls.Popup {
        id: frame
        property string heading: ""
        default property alias content: body.data
        modal: true
        focus: true
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent ? parent.width - 32 : 460)
        padding: 18
        closePolicy: Controls.Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.panel
            radius: Theme.radius
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: frame.heading
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.headingSize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ColumnLayout { id: body; spacing: 10; Layout.fillWidth: true }
        }
    }

    component Entry: StudioButton {
        Layout.fillWidth: true
        quiet: true
        onClicked: menu.close()
    }

    component Label: Text {
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.bodySize
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // --- Dialogues de fichiers ---------------------------------------------------------------
    FileDialog {
        id: openDialog
        objectName: "openAnyDialog"
        title: qsTr("Ouvrir ou importer un fichier")
        fileMode: FileDialog.OpenFile
        nameFilters: {
            var filters = [qsTr("Tous les fichiers pris en charge (*.osp *.svg *.dxf *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.dst *.pes *.jef *.exp *.vp3)"),
                           qsTr("Projet OpenStitch (*.osp)"),
                           qsTr("Image (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)"),
                           qsTr("Vecteur (*.svg *.dxf)")]
            if (panel.ctl) {
                var f = panel.ctl.readableFilters
                for (var i = 0; i < f.length; ++i)
                    filters.push(f[i].filter)
            }
            return filters
        }
        onAccepted: panel.openUrl(selectedFile)
    }
    FileDialog {
        id: imageDialog
        title: qsTr("Importer une image")
        nameFilters: [qsTr("Image (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)")]
        onAccepted: if (panel.ctl) panel.ctl.openUrl(selectedFile)
    }
    FileDialog {
        id: vectorDialog
        title: qsTr("Importer un SVG ou un DXF")
        nameFilters: [qsTr("Vecteur (*.svg *.dxf)")]
        onAccepted: if (panel.ctl) panel.ctl.openUrl(selectedFile)
    }
    FileDialog {
        id: machineDialog
        title: qsTr("Importer une broderie machine")
        nameFilters: {
            var out = []
            if (panel.ctl) {
                var f = panel.ctl.readableFilters
                for (var i = 0; i < f.length; ++i)
                    out.push(f[i].filter)
            }
            return out
        }
        onAccepted: if (panel.ctl) panel.ctl.openUrl(selectedFile)
    }
    FileDialog {
        id: guardSaveDialog
        objectName: "guardSaveDialog"
        title: qsTr("Enregistrer le projet")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "osp"
        nameFilters: [qsTr("Projet OpenStitch (*.osp)")]
        onAccepted: panel.ctl.guardSaveAs(selectedFile)
        onRejected: panel.ctl.resolveGuard("cancel")
    }
    FileDialog {
        id: saveExportDialog
        fileMode: FileDialog.SaveFile
        onAccepted: panel.finishExport(selectedFile)
    }

    // --- Glisser-déposer ----------------------------------------------------------------------
    DropArea {
        objectName: "fileDropArea"
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function (drop) {
            if (panel.ctl && drop.hasUrls) {
                panel.ctl.openDropped(drop.urls)
                drop.accept()
            }
        }
    }

    // --- Menu Fichier -------------------------------------------------------------------------
    Controls.Popup {
        id: menu
        objectName: "fileMenu"
        modal: true
        focus: true
        parent: Controls.Overlay.overlay
        x: 16
        y: 60
        width: 330
        padding: 8
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 2
            Entry { text: qsTr("Nouveau"); shortcutLabel: "Ctrl+N"; onClicked: panel.requestNew() }
            Entry { text: qsTr("Ouvrir…"); shortcutLabel: "Ctrl+O"; onClicked: panel.requestOpen() }
            SectionLabel { text: qsTr("RÉCENTS"); visible: panel.ctl && panel.ctl.recent.length > 0 }
            Repeater {
                model: panel.ctl ? panel.ctl.recent : []
                delegate: Entry {
                    objectName: "recentEntry" + index
                    text: modelData.name
                    tooltip: modelData.path
                    onClicked: panel.ctl.requestOpenRecent(index)
                }
            }
            Entry {
                objectName: "clearRecentEntry"
                text: qsTr("Vider la liste des récents")
                visible: panel.ctl && panel.ctl.recent.length > 0
                onClicked: panel.ctl.clearRecent()
            }
            SectionLabel { text: qsTr("IMPORTER") }
            Entry { text: qsTr("Une image…"); onClicked: panel.importImageDialog() }
            Entry { text: qsTr("Un SVG ou un DXF…"); onClicked: panel.importVectorDialog() }
            Entry { text: qsTr("Une broderie machine…"); onClicked: panel.importMachineDialog() }
            SectionLabel { text: qsTr("EXPORTER") }
            Entry {
                text: qsTr("Pour la machine…")
                shortcutLabel: "Ctrl+E"
                onClicked: panel.openExportDialog()
            }
            Entry {
                objectName: "exportAgainEntry"
                text: panel.ctl && panel.ctl.hasLastExport
                      ? qsTr("Exporter de nouveau (%1)").arg(panel.ctl.lastExportName)
                      : qsTr("Exporter de nouveau")
                enabled: panel.ctl && panel.ctl.hasLastExport
                shortcutLabel: "F10"
                onClicked: panel.ctl.exportAgain()
            }
            Entry { text: qsTr("Image (PNG, JPEG)…"); onClicked: panel.startExport("image", qsTr("Exporter l'image"), [qsTr("Image (*.png *.jpg)")], "png") }
            Entry { text: qsTr("Fiche de production (PDF)…"); onClicked: panel.startExport("sheet", qsTr("Fiche de production"), [qsTr("PDF (*.pdf)")], "pdf") }
            Entry { text: qsTr("Gabarit de placement 1:1 (PDF)…"); onClicked: panel.startExport("template", qsTr("Gabarit de placement"), [qsTr("PDF (*.pdf)")], "pdf") }
            Entry { text: qsTr("Liste des fils (CSV)…"); onClicked: panel.startExport("threads", qsTr("Liste des fils"), [qsTr("CSV (*.csv)")], "csv") }
            Entry { text: qsTr("Contours en DXF…"); onClicked: panel.startExport("dxf", qsTr("Contours DXF"), [qsTr("DXF (*.dxf)")], "dxf") }
            Entry { text: qsTr("Points en SVG…"); onClicked: panel.startExport("svg", qsTr("Points SVG"), [qsTr("SVG (*.svg)")], "svg") }
            SectionLabel { text: qsTr("PROJET") }
            Entry { text: qsTr("Cadre et machine…"); onClicked: panel.openHoopDialog() }
            Entry {
                text: qsTr("Ouvrir le dossier du projet")
                enabled: studio.hasPath()
                onClicked: panel.ctl.revealFolder()
            }
        }
    }
    Shortcut { sequence: "Ctrl+E"; enabled: panel.ctl && !studio.busy; onActivated: panel.openExportDialog() }
    Shortcut { sequence: "F10"; enabled: panel.ctl && panel.ctl.hasLastExport; onActivated: panel.ctl.exportAgain() }

    // --- Enregistrer / Ignorer / Annuler ------------------------------------------------------
    Frame {
        id: guardDialog
        objectName: "guardDialog"
        heading: qsTr("Enregistrer les modifications ?")
        closePolicy: Controls.Popup.NoAutoClose
        visible: panel.ctl ? panel.ctl.pendingReason.length > 0 && !guardSaveDialog.visible : false
        Label {
            text: panel.ctl ? qsTr("« %1 » a des modifications non enregistrées. %2 ?")
                                  .arg(studio.title).arg(panel.guardText(panel.ctl.pendingReason)) : ""
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton { objectName: "guardCancel"; text: qsTr("Annuler"); onClicked: panel.ctl.resolveGuard("cancel") }
            StudioButton { objectName: "guardDiscard"; text: qsTr("Ignorer"); onClicked: panel.ctl.resolveGuard("discard") }
            StudioButton { objectName: "guardSave"; text: qsTr("Enregistrer"); emphasized: true; onClicked: panel.ctl.resolveGuard("save") }
        }
    }
    Connections {
        target: panel.ctl
        ignoreUnknownSignals: true
        function onSaveAsRequested() { guardSaveDialog.open() }
        function onQuitApproved() { Qt.quit() }
        function onImageImportRequested(path) {
            panel.importPath = path
            panel.importValues = panel.ctl.imageImportDefaults()
            importOptions.open()
        }
    }

    // --- Récupération après interruption ------------------------------------------------------
    Frame {
        id: recoveryDialog
        objectName: "recoveryDialog"
        heading: qsTr("Récupérer le travail interrompu ?")
        closePolicy: Controls.Popup.NoAutoClose
        visible: panel.ctl ? panel.ctl.hasRecovery : false
        Label { text: qsTr("OpenStitch Studio s'est arrêté avant l'enregistrement : une sauvegarde automatique est disponible.") }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton { objectName: "recoveryDiscard"; text: qsTr("Ignorer"); onClicked: panel.ctl.discardRecovery() }
            StudioButton { objectName: "recoveryRecover"; text: qsTr("Récupérer"); emphasized: true; onClicked: panel.ctl.recoverAutosave() }
        }
    }

    // --- Options d'import d'image -------------------------------------------------------------
    Frame {
        id: importOptions
        objectName: "importOptions"
        heading: qsTr("Importer une image")
        Label { text: panel.importPath }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8
            Layout.fillWidth: true
            Label { text: qsTr("Hauteur du motif (mm)") }
            Controls.SpinBox {
                objectName: "importHeight"
                from: 5; to: 1000
                value: panel.importValues.heightMm || 100
                editable: true
                onValueModified: panel.importValues.heightMm = value
            }
            Label { text: qsTr("Nombre de couleurs") }
            Controls.SpinBox {
                objectName: "importColors"
                from: 2; to: 32
                value: panel.importValues.maxColors || 8
                editable: true
                onValueModified: panel.importValues.maxColors = value
            }
            Label { text: qsTr("Lissage (px)") }
            Controls.SpinBox {
                from: 0; to: 20
                value: panel.importValues.smoothingPx === undefined ? 3 : panel.importValues.smoothingPx
                editable: true
                onValueModified: panel.importValues.smoothingPx = value
            }
            Label { text: qsTr("Mode") }
            Controls.ComboBox {
                model: [{ t: qsTr("Formes remplies"), v: "shapes" }, { t: qsTr("Contours"), v: "contours" }, { t: qsTr("Pointillé"), v: "stipple" }]
                textRole: "t"
                onActivated: panel.importValues.mode = model[currentIndex].v
            }
            Label { text: qsTr("Fond") }
            Controls.ComboBox {
                model: [{ t: qsTr("Automatique"), v: -1 }, { t: qsTr("Ne pas retirer"), v: 0 }, { t: qsTr("Retirer cette couleur"), v: 1 }, { t: qsTr("Retirer à l'extérieur seulement"), v: 2 }]
                textRole: "t"
                onActivated: panel.importValues.skipBackground = model[currentIndex].v
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton { text: qsTr("Annuler"); onClicked: importOptions.close() }
            StudioButton {
                objectName: "importConfirm"
                text: qsTr("Importer")
                emphasized: true
                onClicked: { importOptions.close(); panel.ctl.importImage(panel.importPath, panel.importValues) }
            }
        }
    }

    // --- Export pour la machine ---------------------------------------------------------------
    Frame {
        id: exportDialog
        objectName: "exportDialog"
        heading: qsTr("Exporter pour la machine")
        property var formats: panel.ctl ? panel.ctl.machineFormats : []
        property string format: formats.length > 0 ? formats[0].extension : "dst"
        Controls.ComboBox {
            objectName: "exportFormat"
            Layout.fillWidth: true
            model: exportDialog.formats
            textRole: "label"
            onActivated: exportDialog.format = exportDialog.formats[currentIndex].extension
        }
        Controls.CheckBox {
            text: qsTr("Écrire les coupes de fil")
            checked: panel.exportValues.finishingEnabled === true
            onToggled: panel.exportValues.finishingEnabled = checked
        }
        Controls.CheckBox {
            text: qsTr("Couper avant chaque changement de couleur")
            checked: panel.exportValues.trimBeforeColorChange === true
            onToggled: panel.exportValues.trimBeforeColorChange = checked
        }
        Controls.CheckBox {
            text: qsTr("Supprimer les points trop courts")
            checked: panel.exportValues.filterShortStitches === true
            onToggled: panel.exportValues.filterShortStitches = checked
        }
        Controls.CheckBox {
            text: qsTr("Découper les points trop longs")
            checked: panel.exportValues.splitLongStitches === true
            onToggled: panel.exportValues.splitLongStitches = checked
        }
        Controls.CheckBox {
            text: qsTr("Relier sans couper quand le trajet sera recouvert")
            checked: panel.exportValues.autoJoin === true
            onToggled: panel.exportValues.autoJoin = checked
        }
        Label { objectName: "exportCheckText"; text: panel.ctl ? panel.ctl.exportCheck(exportDialog.format) : "" }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton { text: qsTr("Annuler"); onClicked: exportDialog.close() }
            StudioButton {
                objectName: "exportConfirm"
                text: qsTr("Exporter…")
                emphasized: true
                onClicked: {
                    exportDialog.close()
                    var values = panel.exportValues
                    values.format = exportDialog.format
                    panel.exportValues = values
                    panel.startExport("machine", qsTr("Exporter pour la machine"),
                                      [exportDialog.format.toUpperCase() + " (*." + exportDialog.format + ")"],
                                      exportDialog.format)
                }
            }
        }
    }

    // --- Cadre et machine ---------------------------------------------------------------------
    Frame {
        id: hoopDialog
        objectName: "hoopDialog"
        heading: qsTr("Cadre et machine")
        property string hoopId: ""
        property real margin: 0
        Controls.ComboBox {
            Layout.fillWidth: true
            model: panel.ctl ? panel.ctl.hoops : []
            textRole: "name"
            onActivated: hoopDialog.hoopId = model[currentIndex].id
        }
        RowLayout {
            Label { text: qsTr("Marge de sécurité (mm)"); Layout.fillWidth: true }
            Controls.SpinBox { from: 0; to: 50; value: 0; editable: true; onValueModified: hoopDialog.margin = value }
            StudioButton {
                text: qsTr("Appliquer")
                enabled: hoopDialog.hoopId.length > 0
                onClicked: panel.ctl.applyHoop(hoopDialog.hoopId, hoopDialog.margin)
            }
        }
        Controls.ComboBox {
            Layout.fillWidth: true
            model: panel.ctl ? panel.ctl.machines : []
            textRole: "name"
            onActivated: panel.ctl.applyMachine(model[currentIndex].id)
        }
        SectionLabel { text: qsTr("MES CADRES") }
        Repeater {
            model: panel.ctl ? panel.ctl.userHoops : []
            delegate: RowLayout {
                Layout.fillWidth: true
                StudioButton { Layout.fillWidth: true; text: modelData.name + " — " + modelData.widthMm + " × " + modelData.heightMm; onClicked: panel.ctl.applyUserHoop(modelData.name) }
                StudioButton { text: qsTr("Retirer"); onClicked: panel.ctl.removeUserHoop(modelData.name) }
            }
        }
        RowLayout {
            Controls.TextField { id: hoopName; Layout.fillWidth: true; placeholderText: qsTr("Nom du cadre actuel") }
            StudioButton { text: qsTr("Enregistrer ce cadre"); enabled: hoopName.text.length > 0; onClicked: { if (panel.ctl.saveCurrentHoopAs(hoopName.text) === "") hoopName.text = "" } }
        }
        StudioButton { Layout.alignment: Qt.AlignRight; text: qsTr("Fermer"); onClicked: hoopDialog.close() }
    }

    // --- Retour d'état ------------------------------------------------------------------------
    Rectangle {
        id: toast
        objectName: "fileToast"
        z: 50
        property bool shown: false
        visible: panel.ctl ? (shown || panel.ctl.busy) : false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 56
        width: Math.min(parent.width - 32, toastRow.implicitWidth + 28)
        height: 40
        radius: Theme.radius
        color: Theme.surface
        border.color: panel.ctl && panel.ctl.messageIsError ? Theme.error : Theme.border
        RowLayout {
            id: toastRow
            anchors.centerIn: parent
            spacing: 10
            Text {
                objectName: "fileToastText"
                text: panel.ctl ? (panel.ctl.busy ? panel.ctl.busyLabel + "…" : panel.ctl.message) : ""
                color: panel.ctl && panel.ctl.messageIsError ? Theme.error : Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
                elide: Text.ElideRight
                Layout.maximumWidth: panel.width - 160
            }
            StudioButton {
                objectName: "fileCancelTask"
                visible: panel.ctl && panel.ctl.busy
                text: qsTr("Annuler")
                onClicked: panel.ctl.cancelTask()
            }
        }
        Connections {
            target: panel.ctl
            ignoreUnknownSignals: true
            function onMessageChanged() { toastTimer.restart(); toast.shown = true }
        }
        Timer {
            id: toastTimer
            interval: 8000
            onTriggered: toast.shown = false
        }
    }
}
