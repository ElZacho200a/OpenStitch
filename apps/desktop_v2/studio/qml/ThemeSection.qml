// SPDX-License-Identifier: Apache-2.0
// Préférences ▸ Thèmes : thèmes installés (aperçu dessiné avec leurs propres couleurs), activation
// immédiate, import depuis un dossier ou une archive .zip, export, thème dérivé, retour au thème
// par défaut, erreurs de compatibilité ou de chargement. Toutes les actions passent par
// `themeEngine` (ThemeController) ; un thème défectueux est refusé et le thème courant reste.
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "."

ColumnLayout {
    id: section
    objectName: "themeSection"
    signal galleryRequested()
    readonly property bool hasEngine: typeof themeEngine !== "undefined" && themeEngine !== null
    readonly property var themes: hasEngine ? themeEngine.themes : []
    readonly property string activeId: hasEngine ? themeEngine.activeId : "neo-studio"
    property string focusId: activeId
    readonly property var focused: {
        for (let i = 0; i < themes.length; ++i)
            if (themes[i].id === focusId)
                return themes[i]
        return null
    }
    // Résultat de la dernière action (import, export…) : texte et ton.
    property string status: ""
    property bool statusError: false
    // Import en attente de confirmation de remplacement.
    property url pendingUrl
    property bool pendingArchive: false
    property bool pendingReplace: false
    spacing: 8

    function report(result, verb) {
        pendingReplace = !result.ok && result.exists === true
        statusError = !result.ok
        const details = result.messages && result.messages.length > 0 ? "\n" + result.messages.join("\n") : ""
        status = (result.ok ? qsTr("%1 : « %2 ».").arg(verb).arg(result.id)
                            : pendingReplace ? qsTr("Un thème « %1 » est déjà installé.").arg(result.id)
                                             : qsTr("%1 refusé.").arg(verb)) + details
        if (result.ok)
            focusId = result.id
    }
    function importFrom(url, archive, replace) {
        pendingUrl = url
        pendingArchive = archive
        const result = archive ? themeEngine.importArchive(url, replace) : themeEngine.importFolder(url, replace)
        report(result, archive ? qsTr("Archive importée") : qsTr("Dossier importé"))
    }

    FolderDialog {
        id: folderDialog
        objectName: "themeFolderDialog"
        title: qsTr("Importer un thème (dossier contenant manifest.json)")
        onAccepted: section.importFrom(selectedFolder, false, false)
    }
    FileDialog {
        id: archiveDialog
        objectName: "themeArchiveDialog"
        title: qsTr("Importer un thème (archive .zip)")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Archive de thème (*.zip)")]
        onAccepted: section.importFrom(selectedFile, true, false)
    }
    FileDialog {
        id: exportDialog
        objectName: "themeExportDialog"
        title: qsTr("Exporter le thème")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Archive de thème (*.zip)")]
        onAccepted: {
            const ok = themeEngine.exportTheme(section.focusId, selectedFile)
            section.statusError = !ok
            section.status = ok ? qsTr("Thème « %1 » exporté.").arg(section.focusId) : themeEngine.lastError
        }
    }

    // Cartes des thèmes : un clic active le thème (aperçu immédiat de toute l'interface).
    Flow {
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: section.themes
            delegate: Controls.AbstractButton {
                id: card
                required property var modelData
                objectName: "themeCard_" + modelData.id
                readonly property bool current: modelData.id === section.activeId
                readonly property var sw: modelData.swatches || ({})
                width: 150
                height: 132
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                enabled: modelData.loadable
                Accessible.role: Accessible.RadioButton
                Accessible.checked: current
                Accessible.name: qsTr("Thème %1").arg(modelData.name)
                Accessible.description: modelData.description || ""
                onClicked: {
                    section.focusId = modelData.id
                    if (!themeEngine.activate(modelData.id)) {
                        section.statusError = true
                        section.status = themeEngine.lastError
                    } else {
                        section.status = ""
                    }
                }
                onActiveFocusChanged: if (activeFocus) section.focusId = modelData.id
                scale: Theme.reducedMotion ? 1 : down ? Theme.pressScale : 1
                Behavior on scale { NumberAnimation { duration: Theme.feedbackFastMs; easing.type: Theme.easingEmphasis } }

                background: ThemedSurface {
                    themeStyle: Theme.style("card", "default", "normal")
                    fill: Theme.raised
                    radius: Theme.radius
                    borderWidth: card.current ? Theme.focusWidth : card.hovered ? Theme.borderWidth : 0
                    border: card.current ? Theme.accent : Theme.outline
                    focused: card.activeFocus
                    shadowEnabled: false
                    opacity: card.enabled ? 1 : 0.6
                }
                contentItem: ColumnLayout {
                    spacing: 6
                    // Mini-maquette aux couleurs du thème lui-même (pas du thème courant).
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        radius: Math.min(10, (card.sw.radius || 8))
                        color: card.sw.background || Theme.background
                        clip: true
                        Image {
                            anchors.fill: parent
                            visible: (card.modelData.preview || "").length > 0
                            source: card.modelData.preview || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        Rectangle {
                            x: 8; y: 8; width: parent.width * 0.42; height: parent.height - 16
                            radius: Math.min(8, (card.sw.radius || 8) * 0.7)
                            color: card.sw.panel || Theme.panel
                            Column {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 4
                                Rectangle { width: parent.width * 0.8; height: 4; radius: 2; color: card.sw.foreground || Theme.foreground }
                                Rectangle { width: parent.width * 0.6; height: 3; radius: 1.5; color: card.sw.muted || Theme.muted }
                                Rectangle { width: parent.width * 0.7; height: 3; radius: 1.5; color: card.sw.muted || Theme.muted }
                            }
                        }
                        Rectangle {
                            x: parent.width * 0.5 + 4; y: 10
                            width: parent.width * 0.42; height: 16
                            radius: Math.min(8, card.sw.radius || 8)
                            color: card.sw.accent || Theme.accent
                            Rectangle { anchors.centerIn: parent; width: parent.width * 0.5; height: 3; radius: 1.5; color: card.sw.accentInk || Theme.accentInk }
                        }
                        Rectangle {
                            x: parent.width * 0.5 + 4; y: 32
                            width: parent.width * 0.42; height: 16
                            radius: Math.min(8, card.sw.radius || 8)
                            color: card.sw.raised || Theme.raised
                        }
                        Row {
                            x: parent.width * 0.5 + 4; y: 54
                            spacing: 4
                            Repeater {
                                model: [card.sw.secondaryAccent, card.sw.info, card.sw.accent]
                                Rectangle { required property var modelData; width: 8; height: 8; radius: 4; color: modelData || Theme.accent }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: card.modelData.name
                            elide: Text.ElideRight
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                            font.weight: card.current ? Theme.strongWeight : Theme.bodyWeight
                        }
                        StudioIcon {
                            visible: card.current
                            name: "check"
                            color: Theme.accent
                            width: 14
                            height: 14
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: card.modelData.errorCount > 0 ? qsTr("Erreur : non activable")
                              : card.modelData.warningCount > 0 ? qsTr("%1 avertissement(s)").arg(card.modelData.warningCount)
                              : card.modelData.builtin ? qsTr("Livré avec le Studio")
                              : qsTr("Importé")
                        color: card.modelData.errorCount > 0 ? Theme.error
                               : card.modelData.warningCount > 0 ? Theme.warning : Theme.faint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.captionSize
                    }
                }
            }
        }
    }

    // Détail du thème sélectionné : description, origine, diagnostics.
    Text {
        objectName: "themeDetails"
        Layout.fillWidth: true
        visible: section.focused !== null
        wrapMode: Text.WordWrap
        text: section.focused === null ? ""
              : (section.focused.description || "") + "\n"
                + qsTr("Auteur : %1 · version %2").arg(section.focused.author || "?").arg(section.focused.version || "?")
                + ((section.focused.parent || "").length > 0 ? qsTr(" · dérivé de « %1 »").arg(section.focused.parent) : "")
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
    }
    Text {
        objectName: "themeDiagnostics"
        Layout.fillWidth: true
        visible: section.focused !== null && section.focused.messages.length > 0
        wrapMode: Text.WordWrap
        text: section.focused === null ? "" : section.focused.messages.slice(0, 6).join("\n")
              + (section.focused.messages.length > 6 ? "\n" + qsTr("… et %1 autre(s)").arg(section.focused.messages.length - 6) : "")
        color: section.focused !== null && section.focused.errorCount > 0 ? Theme.error : Theme.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6
        StudioButton {
            objectName: "themeImportFolder"
            text: qsTr("Importer un dossier…")
            iconName: "open"
            enabled: section.hasEngine
            onClicked: folderDialog.open()
        }
        StudioButton {
            objectName: "themeImportArchive"
            text: qsTr("Importer une archive…")
            enabled: section.hasEngine
            onClicked: archiveDialog.open()
        }
        StudioButton {
            objectName: "themeExport"
            text: qsTr("Exporter…")
            iconName: "save"
            enabled: section.hasEngine && section.focused !== null && section.focused.loadable
            onClicked: {
                exportDialog.selectedFile = "file:///" + section.focusId + ".zip"
                exportDialog.open()
            }
        }
        StudioButton {
            objectName: "themeGalleryButton"
            text: qsTr("Galerie des composants")
            iconName: "view"
            onClicked: section.galleryRequested()
        }
        StudioButton {
            objectName: "themeReset"
            text: qsTr("Rétablir Neo Studio")
            quiet: true
            enabled: section.hasEngine && section.activeId !== "neo-studio"
            onClicked: { themeEngine.resetToDefault(); section.status = "" }
        }
        StudioButton {
            objectName: "themeReload"
            text: qsTr("Recharger")
            tooltip: qsTr("Relire les dossiers de thèmes (thème modifié à la main)")
            quiet: true
            enabled: section.hasEngine
            onClicked: themeEngine.reload()
        }
        StudioButton {
            objectName: "themeOpenFolder"
            text: qsTr("Dossier des thèmes")
            tooltip: section.hasEngine ? themeEngine.userThemesDir : ""
            quiet: true
            enabled: section.hasEngine && themeEngine.userThemesDir.length > 0
            onClicked: Qt.openUrlExternally("file:///" + themeEngine.userThemesDir.replace(/\\/g, "/"))
        }
    }

    // Nouveau thème : enfant vide du thème sélectionné, à éditer dans le dossier des thèmes
    // (rechargé à chaud dès qu'un fichier change).
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        StudioTextField {
            id: derivedName
            objectName: "themeDerivedName"
            Layout.fillWidth: true
            placeholderText: qsTr("Nom d'un nouveau thème dérivé de la sélection")
            Accessible.name: qsTr("Nom du thème dérivé")
        }
        StudioButton {
            objectName: "themeCreateDerived"
            text: qsTr("Créer")
            iconName: "plus"
            enabled: section.hasEngine && derivedName.text.trim().length > 1 && section.focused !== null
            onClicked: {
                const id = derivedName.text.trim().toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "")
                           .replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 48)
                const result = themeEngine.createDerived(section.focusId, id, derivedName.text.trim())
                section.report(result, qsTr("Thème créé"))
                if (result.ok) {
                    derivedName.text = ""
                    themeEngine.activate(result.id)
                }
            }
        }
    }

    Text {
        objectName: "themeStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: section.status.length > 0 ? section.status : section.hasEngine ? themeEngine.lastError : ""
        wrapMode: Text.WordWrap
        color: section.statusError || (section.status.length === 0 && section.hasEngine && themeEngine.lastError.length > 0)
               ? Theme.error : Theme.success
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
    StudioButton {
        objectName: "themeReplace"
        visible: section.pendingReplace
        text: qsTr("Remplacer le thème installé")
        onClicked: section.importFrom(section.pendingUrl, section.pendingArchive, true)
    }
}
