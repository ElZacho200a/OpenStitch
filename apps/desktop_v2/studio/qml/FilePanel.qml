// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

// Famille A : menu Fichier, contrôle des modifications non enregistrées, récents, glisser-déposer,
// import d'image sans options de segmentation, export, cadre/machine, récupération. Tout passe par le
// contrôleur `files` (FileController) ; sans lui (tests de la coque seule) chaque entrée retombe
// sur `studio`, et le panneau ne fait alors que les gestes de base.
Item {
    id: panel
    signal quitApproved()
    signal imageImported()
    objectName: "filePanel"
    readonly property bool notificationVisible: toast.visible
    anchors.fill: parent

    readonly property var ctl: (typeof files !== "undefined") ? files : null
    property string exportKind: ""
    property var exportValues: ({})

    // --- API pour Main.qml -----------------------------------------------------------------
    function requestNew() { if (ctl) ctl.requestNew(); else studio.newProject() }
    function requestOpen() { openDialog.open() }
    function openUrl(url) { if (ctl) ctl.openUrl(url); else studio.openProject(url) }
    function requestQuit() { return ctl ? ctl.requestQuit() : true }
    function openMenu() { menu.open() }
    function openExportDialog() { if (ctl) { exportValues = ctl.exportOptions(); exportDialog.open(); ctl.requestExportCheck(exportDialog.format) } }
    function openHoopDialog() { if (ctl) hoopDialog.open() }
    function importImageDialog() { imageDialog.open() }
    function importVectorDialog() { vectorDialog.open() }
    function importMachineDialog() { machineDialog.open() }
    function insertDesignDialog() { if (ctl) insertFileDialog.open() }
    // CX08 : aperçu de ce qui sera inséré, puis insertion en une étape annulable.
    function previewInsert(url) {
        insertDialog.url = url
        insertDialog.preview = ctl.insertDesignPreview(url)
        insertDialog.open()
    }
    function insertSummary(p) {
        if (!p || !p.ok)
            return ""
        var size = qsTr("%1 × %2 mm").arg(Number(p.widthMm).toFixed(1)).arg(Number(p.heightMm).toFixed(1))
        if (p.machine)
            return qsTr("Broderie machine : %1 points, %2 couleurs, %3. Elle arrive centrée dans le cadre.")
                .arg(p.stitches).arg(p.colors).arg(size)
        return qsTr("Projet : %1 formes, %2 objets de broderie, %3 textes, %4. Les objets gardent leur position et reçoivent de nouveaux identifiants.")
            .arg(p.shapes).arg(p.embroideries).arg(p.texts).arg(size)
    }

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

    // CX07 : réglages puis aperçu de ce que l'export produira, avant le choix du fichier.
    function openExportOptions(kind) {
        if (!ctl)
            return
        exportOptionsDialog.kind = kind
        exportOptionsDialog.plan = ({})
        exportOptionsDialog.open()
        exportOptionsDialog.refresh()
    }
    function exportPlanText(kind, p) {
        if (kind === "photo")
            return qsTr("Lancer de rayons (Mitsuba, hors écran). Plus d'échantillons : moins de grain, rendu plus long. 0° = vue de dessus.")
        if (!p || p.ok === undefined)
            return qsTr("Calcul de l'aperçu…")
        if (!p.ok)
            return p.message || qsTr("Aperçu impossible.")
        if (kind === "image") {
            var text = qsTr("%1 × %2 pixels pour %3 × %4 mm (cadre et points), %5 px/mm.")
                .arg(p.width).arg(p.height)
                .arg(Number(p.widthMm).toFixed(1)).arg(Number(p.heightMm).toFixed(1))
                .arg(Number(p.pxPerMm).toFixed(2))
            if (p.reduced)
                text += " " + qsTr("Résolution abaissée : l'image dépasserait la taille maximale.")
            return text
        }
        return qsTr("%1 page(s) (%2 × %3) de %4 × %5 mm pour un motif de %6 × %7 mm, à l'échelle 1:1.")
            .arg(p.pages).arg(p.columns).arg(p.rows)
            .arg(Number(p.pageWidthMm).toFixed(0)).arg(Number(p.pageHeightMm).toFixed(0))
            .arg(Number(p.designWidthMm).toFixed(1)).arg(Number(p.designHeightMm).toFixed(1))
    }

    function finishExport(url) {
        switch (exportKind) {
        case "machine": ctl.exportMachine(url, exportValues); break
        case "threads": ctl.exportThreadList(url); break
        case "dxf": ctl.exportDxf(url); break
        case "svg": ctl.exportSvg(url); break
        case "image": ctl.exportImage(url, exportOptionsDialog.pxPerMm, exportOptionsDialog.quality); break
        case "photo": ctl.exportPhotoRender(url, exportOptionsDialog.photoOptions()); break
        case "sheet": ctl.exportProductionSheet(url); break
        case "template": ctl.exportPlacementTemplate(url, { paper: exportOptionsDialog.paper, landscape: exportOptionsDialog.landscape }); break
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
        background: StudioPopupBackground { }
        enter: StudioPopupEnter { }
        exit: StudioPopupExit { }
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
        id: entry
        Layout.fillWidth: true
        quiet: true
        // Keep closing independent of each entry's own onClicked action. An inherited
        // handler alone is fragile when the caller supplies another handler.
        Connections {
            target: entry
            function onClicked() { menu.close() }
        }
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
            var filters = [qsTr("Tous les fichiers pris en charge (*.osp *.svg *.dxf *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.ora *.dst *.pes *.jef *.exp *.vp3)"),
                           qsTr("Projet OpenStitch (*.osp)"),
                           qsTr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.ora)"),
                           qsTr("OpenRaster (*.ora)"),
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
        title: qsTr("Importer des images en calques")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Image ou OpenRaster (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.ora)")]
        onAccepted: if (panel.ctl) panel.ctl.importImageLayers(selectedFiles)
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
        id: insertFileDialog
        objectName: "insertDesignFileDialog"
        title: qsTr("Insérer un design dans ce projet")
        nameFilters: {
            var out = [qsTr("Projet OpenStitch (*.osp)")]
            if (panel.ctl) {
                var f = panel.ctl.readableFilters
                for (var i = 0; i < f.length; ++i)
                    out.push(f[i].filter)
            }
            return out
        }
        onAccepted: panel.previewInsert(selectedFile)
    }
    Frame {
        id: insertDialog
        objectName: "insertDesignDialog"
        heading: insertDialog.preview && insertDialog.preview.name
                 ? qsTr("Insérer « %1 » ?").arg(insertDialog.preview.name)
                 : qsTr("Insérer un design ?")
        property url url
        property var preview: ({})
        Label {
            objectName: "insertDesignSummary"
            text: insertDialog.preview && insertDialog.preview.ok
                  ? panel.insertSummary(insertDialog.preview)
                  : ((insertDialog.preview && insertDialog.preview.message) || "")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton {
                objectName: "cancelInsertDesign"
                text: qsTr("Annuler")
                onClicked: insertDialog.close()
            }
            StudioButton {
                objectName: "acceptInsertDesign"
                text: qsTr("Insérer")
                emphasized: true
                enabled: insertDialog.preview && insertDialog.preview.ok === true
                onClicked: {
                    panel.ctl.insertDesign(insertDialog.url)
                    insertDialog.close()
                }
            }
        }
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

    // QA-STUDIO-001 EXP-003 / HOOP-019 / JRN-024: warning BEFORE writing
    // a machine file, never after an unacknowledged out-of-hoop export.
    Connections {
        target: panel.ctl
        function onExportConfirmationRequested(warning) {
            exportOverflowDialog.warning = warning
            exportOverflowDialog.open()
        }
    }
    Frame {
        id: exportOverflowDialog
        objectName: "exportOverflowDialog"
        heading: qsTr("Confirmer l'export hors limites ?")
        property string warning: ""
        onClosed: if (panel.ctl) panel.ctl.confirmMachineExport(false)
        Label {
            text: exportOverflowDialog.warning
        }
        Label {
            text: qsTr("Le motif risque de dépasser le cadre ou les limites de la machine. Vérifiez le placement avant de continuer.")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton {
                objectName: "cancelOverflowExport"
                text: qsTr("Annuler")
                onClicked: {
                    panel.ctl.confirmMachineExport(false)
                    exportOverflowDialog.close()
                }
            }
            StudioButton {
                objectName: "acceptOverflowExport"
                text: qsTr("Exporter quand même")
                emphasized: true
                onClicked: {
                    panel.ctl.confirmMachineExport(true)
                    exportOverflowDialog.close()
                }
            }
        }
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
        background: StudioPopupBackground { }
        enter: StudioPopupEnter { }
        exit: StudioPopupExit { }
        // A short window (1000x640) cuts the menu: bound its height and scroll the entries
        // so EXPORTER / PROJET stay reachable (QA-STUDIO-001 CTL-013/015, POL-001/023).
        height: Math.min(implicitHeight, (parent ? parent.height : 640) - y - 12)
        contentItem: Controls.ScrollView {
            id: menuScroll
            objectName: "fileMenuScroll"
            clip: true
            contentWidth: availableWidth
            implicitHeight: menuColumn.implicitHeight
          ColumnLayout {
            id: menuColumn
            width: menuScroll.availableWidth
            spacing: 2
            Entry { objectName: "newFileMenuEntry"; text: qsTr("Nouveau"); shortcutLabel: "Ctrl+N"; onClicked: panel.requestNew() }
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
            Entry { text: qsTr("Images en calques…"); onClicked: panel.importImageDialog() }
            Entry { text: qsTr("Un SVG ou un DXF…"); onClicked: panel.importVectorDialog() }
            Entry { text: qsTr("Une broderie machine…"); onClicked: panel.importMachineDialog() }
            Entry {
                objectName: "insertDesignMenuEntry"
                text: qsTr("Un design dans ce projet…")
                tooltip: qsTr("Ajoute le contenu d'un projet .osp ou d'une broderie machine au projet ouvert, en une étape annulable")
                enabled: panel.ctl !== null
                onClicked: panel.insertDesignDialog()
            }
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
            Entry { objectName: "exportImageEntry"; text: qsTr("Image (PNG, JPEG)…"); onClicked: panel.openExportOptions("image") }
            Entry { objectName: "exportPhotoEntry"; text: qsTr("Rendu photo, lancer de rayons (PNG)…"); onClicked: panel.openExportOptions("photo") }
            Entry { text: qsTr("Fiche de production (PDF)…"); onClicked: panel.startExport("sheet", qsTr("Fiche de production"), [qsTr("PDF (*.pdf)")], "pdf") }
            Entry { objectName: "exportTemplateEntry"; text: qsTr("Gabarit de placement 1:1 (PDF)…"); onClicked: panel.openExportOptions("template") }
            Entry { text: qsTr("Liste des fils (CSV)…"); onClicked: panel.startExport("threads", qsTr("Liste des fils"), [qsTr("CSV (*.csv)")], "csv") }
            Entry { text: qsTr("Contours en DXF…"); onClicked: panel.startExport("dxf", qsTr("Contours DXF"), [qsTr("DXF (*.dxf)")], "dxf") }
            Entry { text: qsTr("Points en SVG…"); onClicked: panel.startExport("svg", qsTr("Points SVG"), [qsTr("SVG (*.svg)")], "svg") }
            SectionLabel { text: qsTr("PROJET") }
            Entry { objectName: "hoopMenuEntry"; text: qsTr("Cadre et machine…"); onClicked: panel.openHoopDialog() }
            Entry {
                text: qsTr("Ouvrir le dossier du projet")
                enabled: studio.hasPath()
                onClicked: panel.ctl.revealFolder()
            }
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
        function onQuitApproved() { panel.quitApproved() }
        function onImageImported() { panel.imageImported() }
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

    // Importer une image ouvre seulement le sélecteur de fichier : la préparation
    // a lieu dans l'espace Image, la segmentation dans l'espace suivant.
    
    // --- Options des exports image et gabarit (CX07) -------------------------------------------
    Connections {
        target: panel.ctl
        function onExportPlanReady(kind, plan) {
            if (kind === exportOptionsDialog.kind)
                exportOptionsDialog.plan = plan
        }
    }
    Frame {
        id: exportOptionsDialog
        objectName: "exportOptionsDialog"
        property string kind: "image"
        property var plan: ({})
        property real pxPerMm: 6
        property int quality: 92
        property string paper: "A4"
        property bool landscape: false
        readonly property var papers: ["A4", "A3", "Letter"]
        // Rendu photo (RENDER-PBR-01 lot D) : réglages transmis au moteur de lancer de rayons.
        property int photoTilt: 35
        property int photoSamples: 64
        property int photoPxPerMm: 10
        property int photoThreadHundredths: 35
        // Couleurs en « r,g,b » (0..255) : préréglages ou couleur libre (sélecteur).
        property string photoFabricRgb: "245,243,238"
        property string photoBackgroundRgb: "200,200,204"
        property int photoFabricType: 1
        property int photoThreadType: 0
        // Réglages fins du fil (centièmes), préremplis par la matière choisie.
        property int photoGloss: 12
        property int photoSpread: 30
        property int photoMatte: 20
        property int photoSaturation: 35
        property int photoPlies: 2
        property int photoTwistPitch: 0
        property int photoRelief: 40
        property int photoLight: 24
        function applyThreadPreset(i) {
            var t = photoThreadTypes[i]
            photoThreadType = i
            photoGloss = t.gloss; photoSpread = t.spread; photoMatte = t.matte; photoPlies = t.plies
        }
        readonly property var photoFabrics: [
            { name: qsTr("Blanc cassé"), rgb: "245,243,238" },
            { name: qsTr("Blanc"), rgb: "252,252,252" },
            { name: qsTr("Gris chiné"), rgb: "150,150,155" },
            { name: qsTr("Noir"), rgb: "22,22,24" },
            { name: qsTr("Bleu marine"), rgb: "30,42,78" },
            { name: qsTr("Rouge"), rgb: "170,30,35" },
            { name: qsTr("Kaki"), rgb: "110,105,70" }
        ]
        readonly property var photoFabricTypes: [
            { name: qsTr("Uni (sans relief)"), id: "uni" },
            { name: qsTr("Toile (coton, lin)"), id: "toile" },
            { name: qsTr("Sergé (jean, gabardine)"), id: "serge" },
            { name: qsTr("Jersey (t-shirt, maille)"), id: "jersey" },
            { name: qsTr("Feutre"), id: "feutre" },
            { name: qsTr("Éponge"), id: "eponge" }
        ]
        readonly property var photoThreadTypes: [
            // Mêmes valeurs que THREAD_TYPES dans tools/raytrace/render_mitsuba.py.
            { name: qsTr("Rayonne (brillante)"), id: "rayonne", gloss: 12, spread: 30, matte: 20, plies: 2 },
            { name: qsTr("Polyester (satiné)"), id: "polyester", gloss: 22, spread: 40, matte: 35, plies: 2 },
            { name: qsTr("Coton (mat)"), id: "coton", gloss: 45, spread: 70, matte: 50, plies: 3 }
        ]
        function rgbToColor(rgb) {
            var p = rgb.split(",")
            return Qt.rgba(p[0] / 255, p[1] / 255, p[2] / 255, 1)
        }
        function colorToRgb(c) {
            return Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255)
        }
        function photoOptions() {
            return { tiltDeg: photoTilt, samples: photoSamples, pxPerMm: photoPxPerMm,
                     threadWidthMm: photoThreadHundredths / 100.0,
                     fabric: photoFabricRgb,
                     fabricType: photoFabricTypes[photoFabricType].id,
                     threadType: photoThreadTypes[photoThreadType].id,
                     threadGloss: photoGloss / 100.0, threadSpread: photoSpread / 100.0,
                     threadMatte: photoMatte / 100.0, threadSaturation: photoSaturation / 100.0,
                     threadPlies: photoPlies, twistPitchMm: photoTwistPitch / 100.0,
                     relief: photoRelief / 100.0, light: photoLight / 10.0,
                     background: photoBackgroundRgb }
        }
        heading: kind === "image" ? qsTr("Exporter l'image")
                 : kind === "photo" ? qsTr("Rendu photo") : qsTr("Gabarit de placement 1:1")
        function refresh() {
            plan = ({})
            if (kind === "photo")
                return // rien à prévoir : le moteur externe calcule l'image
            panel.ctl.requestExportPlan(kind, { pxPerMm: pxPerMm, paper: paper, landscape: landscape })
        }
        RowLayout {
            visible: exportOptionsDialog.kind === "image"
            spacing: 8
            Label { text: qsTr("Résolution (px/mm)") }
            StudioSpinBox {
                objectName: "exportImageResolution"
                from: 1; to: 12; editable: true
                value: exportOptionsDialog.pxPerMm
                onValueModified: { exportOptionsDialog.pxPerMm = value; exportOptionsDialog.refresh() }
            }
        }
        RowLayout {
            visible: exportOptionsDialog.kind === "image"
            spacing: 8
            Label { text: qsTr("Qualité JPEG") }
            StudioSpinBox {
                objectName: "exportImageQuality"
                from: 1; to: 100; editable: true
                value: exportOptionsDialog.quality
                onValueModified: exportOptionsDialog.quality = value
            }
        }
        Label {
            objectName: "photoPrerequisite"
            visible: exportOptionsDialog.kind === "photo"
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            text: qsTr("Rendu photo : nécessite l'extension Mitsuba (voir l'aide). Peut durer plusieurs minutes.")
        }
        GridLayout {
            visible: exportOptionsDialog.kind === "photo"
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            Label { text: qsTr("Inclinaison (°)") }
            StudioSpinBox {
                objectName: "photoTilt"
                from: 0; to: 70; editable: true
                value: exportOptionsDialog.photoTilt
                onValueModified: exportOptionsDialog.photoTilt = value
            }
            Label { text: qsTr("Qualité (échantillons/pixel)") }
            StudioSpinBox {
                objectName: "photoSamples"
                from: 1; to: 4096; stepSize: 16; editable: true
                value: exportOptionsDialog.photoSamples
                onValueModified: exportOptionsDialog.photoSamples = value
            }
            Label { text: qsTr("Résolution (px/mm)") }
            StudioSpinBox {
                objectName: "photoPxPerMm"
                from: 2; to: 40; editable: true
                value: exportOptionsDialog.photoPxPerMm
                onValueModified: exportOptionsDialog.photoPxPerMm = value
            }
            Label { text: qsTr("Largeur du fil (1/100 mm)") }
            StudioSpinBox {
                objectName: "photoThreadWidth"
                from: 15; to: 100; editable: true
                value: exportOptionsDialog.photoThreadHundredths
                onValueModified: exportOptionsDialog.photoThreadHundredths = value
            }
            Label { text: qsTr("Matière du fil") }
            StudioComboBox {
                objectName: "photoThreadType"
                Layout.fillWidth: true
                model: exportOptionsDialog.photoThreadTypes.map(function (t) { return t.name })
                currentIndex: exportOptionsDialog.photoThreadType
                onActivated: exportOptionsDialog.applyThreadPreset(currentIndex)
            }
            Label { text: qsTr("Brillance du fil (rugosité, 1/100)") }
            StudioSpinBox {
                objectName: "photoGloss"
                from: 2; to: 100; editable: true
                value: exportOptionsDialog.photoGloss
                onValueModified: exportOptionsDialog.photoGloss = value
            }
            Label { text: qsTr("Largeur du reflet (1/100)") }
            StudioSpinBox {
                objectName: "photoSpread"
                from: 5; to: 100; editable: true
                value: exportOptionsDialog.photoSpread
                onValueModified: exportOptionsDialog.photoSpread = value
            }
            Label { text: qsTr("Part mate (%)") }
            StudioSpinBox {
                objectName: "photoMatte"
                from: 0; to: 100; editable: true
                value: exportOptionsDialog.photoMatte
                onValueModified: exportOptionsDialog.photoMatte = value
            }
            Label { text: qsTr("Saturation des couleurs (1/100)") }
            StudioSpinBox {
                objectName: "photoSaturation"
                from: 0; to: 200; editable: true
                value: exportOptionsDialog.photoSaturation
                onValueModified: exportOptionsDialog.photoSaturation = value
            }
            Label { text: qsTr("Brins torsadés") }
            StudioSpinBox {
                objectName: "photoPlies"
                from: 1; to: 4; editable: true
                value: exportOptionsDialog.photoPlies
                onValueModified: exportOptionsDialog.photoPlies = value
            }
            Label { text: qsTr("Pas de torsion (1/100 mm, 0 = auto)") }
            StudioSpinBox {
                objectName: "photoTwistPitch"
                from: 0; to: 500; editable: true
                value: exportOptionsDialog.photoTwistPitch
                onValueModified: exportOptionsDialog.photoTwistPitch = value
            }
            Label { text: qsTr("Relief des superpositions (%)") }
            StudioSpinBox {
                objectName: "photoRelief"
                from: 0; to: 200; editable: true
                value: exportOptionsDialog.photoRelief
                onValueModified: exportOptionsDialog.photoRelief = value
            }
            Label { text: qsTr("Lumière (1/10)") }
            StudioSpinBox {
                objectName: "photoLight"
                from: 1; to: 100; editable: true
                value: exportOptionsDialog.photoLight
                onValueModified: exportOptionsDialog.photoLight = value
            }
            Label { text: qsTr("Type de tissu") }
            StudioComboBox {
                objectName: "photoFabricType"
                Layout.fillWidth: true
                model: exportOptionsDialog.photoFabricTypes.map(function (f) { return f.name })
                currentIndex: exportOptionsDialog.photoFabricType
                onActivated: exportOptionsDialog.photoFabricType = currentIndex
            }
            Label { text: qsTr("Couleur du tissu") }
            RowLayout {
                spacing: 6
                Rectangle {
                    implicitWidth: 22; implicitHeight: 22; radius: 4
                    color: exportOptionsDialog.rgbToColor(exportOptionsDialog.photoFabricRgb)
                    border.width: 1; border.color: Theme.outline
                }
                StudioComboBox {
                    objectName: "photoFabric"
                    Layout.fillWidth: true
                    // Préréglages puis « Personnalisée… » (sélecteur de couleur).
                    model: exportOptionsDialog.photoFabrics.map(function (f) { return f.name })
                           .concat([qsTr("Personnalisée…")])
                    currentIndex: {
                        var i = exportOptionsDialog.photoFabrics.findIndex(function (f) {
                            return f.rgb === exportOptionsDialog.photoFabricRgb })
                        return i >= 0 ? i : exportOptionsDialog.photoFabrics.length
                    }
                    onActivated: {
                        if (currentIndex < exportOptionsDialog.photoFabrics.length)
                            exportOptionsDialog.photoFabricRgb = exportOptionsDialog.photoFabrics[currentIndex].rgb
                        else
                            photoFabricColorDialog.open()
                    }
                }
            }
            Label { text: qsTr("Couleur du fond") }
            RowLayout {
                spacing: 6
                Rectangle {
                    implicitWidth: 22; implicitHeight: 22; radius: 4
                    color: exportOptionsDialog.rgbToColor(exportOptionsDialog.photoBackgroundRgb)
                    border.width: 1; border.color: Theme.outline
                }
                StudioButton {
                    objectName: "photoBackground"
                    text: qsTr("Choisir…")
                    onClicked: photoBackgroundColorDialog.open()
                }
            }
        }
        ColorDialog {
            id: photoFabricColorDialog
            objectName: "photoFabricColorDialog"
            title: qsTr("Couleur du tissu")
            selectedColor: exportOptionsDialog.rgbToColor(exportOptionsDialog.photoFabricRgb)
            onAccepted: exportOptionsDialog.photoFabricRgb = exportOptionsDialog.colorToRgb(selectedColor)
        }
        ColorDialog {
            id: photoBackgroundColorDialog
            objectName: "photoBackgroundColorDialog"
            title: qsTr("Couleur du fond")
            selectedColor: exportOptionsDialog.rgbToColor(exportOptionsDialog.photoBackgroundRgb)
            onAccepted: exportOptionsDialog.photoBackgroundRgb = exportOptionsDialog.colorToRgb(selectedColor)
        }
        StudioComboBox {
            objectName: "exportTemplatePaper"
            visible: exportOptionsDialog.kind === "template"
            Layout.fillWidth: true
            model: exportOptionsDialog.papers
            currentIndex: exportOptionsDialog.papers.indexOf(exportOptionsDialog.paper)
            onActivated: { exportOptionsDialog.paper = exportOptionsDialog.papers[currentIndex]; exportOptionsDialog.refresh() }
        }
        StudioCheckBox {
            objectName: "exportTemplateLandscape"
            visible: exportOptionsDialog.kind === "template"
            text: qsTr("Paysage")
            checked: exportOptionsDialog.landscape
            onToggled: { exportOptionsDialog.landscape = checked; exportOptionsDialog.refresh() }
        }
        Label {
            objectName: "exportPlanSummary"
            text: panel.exportPlanText(exportOptionsDialog.kind, exportOptionsDialog.plan)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton { objectName: "cancelExportOptions"; text: qsTr("Annuler"); onClicked: exportOptionsDialog.close() }
            StudioButton {
                objectName: "confirmExportOptions"
                text: qsTr("Enregistrer…")
                emphasized: true
                enabled: exportOptionsDialog.kind === "photo"
                         || (exportOptionsDialog.plan && exportOptionsDialog.plan.ok === true)
                onClicked: {
                    exportOptionsDialog.close()
                    if (exportOptionsDialog.kind === "image")
                        panel.startExport("image", qsTr("Exporter l'image"), [qsTr("Image (*.png *.jpg)")], "png")
                    else if (exportOptionsDialog.kind === "photo")
                        panel.startExport("photo", qsTr("Rendu photo"), [qsTr("PNG (*.png)")], "png")
                    else
                        panel.startExport("template", qsTr("Gabarit de placement"), [qsTr("PDF (*.pdf)")], "pdf")
                }
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
        StudioComboBox {
            objectName: "exportFormat"
            Layout.fillWidth: true
            model: exportDialog.formats
            textRole: "label"
            onActivated: {
                exportDialog.format = exportDialog.formats[currentIndex].extension
                if (panel.ctl) panel.ctl.requestExportCheck(exportDialog.format)
            }
        }
        StudioCheckBox {
            text: qsTr("Écrire les coupes de fil")
            checked: panel.exportValues.finishingEnabled === true
            onToggled: panel.exportValues.finishingEnabled = checked
        }
        StudioCheckBox {
            text: qsTr("Couper avant chaque changement de couleur")
            checked: panel.exportValues.trimBeforeColorChange === true
            onToggled: panel.exportValues.trimBeforeColorChange = checked
        }
        StudioCheckBox {
            text: qsTr("Supprimer les points trop courts")
            checked: panel.exportValues.filterShortStitches === true
            onToggled: panel.exportValues.filterShortStitches = checked
        }
        StudioCheckBox {
            text: qsTr("Découper les points trop longs")
            checked: panel.exportValues.splitLongStitches === true
            onToggled: panel.exportValues.splitLongStitches = checked
        }
        StudioCheckBox {
            objectName: "exportHiddenTravel"
            text: qsTr("Réduire les coupes : passer sous les points cousus ensuite")
            readonly property string help: qsTr("Cache les sauts de fil sous des points cousus plus tard, quand c'est possible.")
            Accessible.description: help
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 500
            Controls.ToolTip.text: help
            checked: panel.exportValues.hiddenTravel === true
            onToggled: panel.exportValues.hiddenTravel = checked
        }
        StudioCheckBox {
            objectName: "exportAutoJoin"
            text: qsTr("Entrée et sortie de chaque objet au plus court")
            checked: panel.exportValues.autoJoin === true
            onToggled: panel.exportValues.autoJoin = checked
        }
        Label { objectName: "exportCheckText"; text: panel.ctl ? panel.ctl.exportCheckText : "" }
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
        StudioComboBox {
            Layout.fillWidth: true
            model: panel.ctl ? panel.ctl.hoops : []
            textRole: "name"
            onActivated: hoopDialog.hoopId = model[currentIndex].id
        }
        RowLayout {
            Label { text: qsTr("Marge de sécurité (mm)"); Layout.fillWidth: true }
            StudioSpinBox { from: 0; to: 50; value: 0; editable: true; onValueModified: hoopDialog.margin = value }
            StudioButton {
                text: qsTr("Appliquer")
                enabled: hoopDialog.hoopId.length > 0
                onClicked: panel.ctl.applyHoop(hoopDialog.hoopId, hoopDialog.margin)
            }
        }
        StudioComboBox {
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
            StudioTextField { id: hoopName; Layout.fillWidth: true; placeholderText: qsTr("Nom du cadre actuel") }
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
        readonly property bool activeToast: panel.ctl ? (shown || panel.ctl.busy) : false
        visible: activeToast || opacity > 0
        opacity: activeToast ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.feedbackFastMs } }
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
            function onMessageChanged() {
                toast.shown = panel.ctl.message.length > 0
                if (toast.shown) toastTimer.restart()
                else toastTimer.stop()
            }
        }
        Timer {
            id: toastTimer
            interval: 8000
            onTriggered: toast.shown = false
        }
    }
}
