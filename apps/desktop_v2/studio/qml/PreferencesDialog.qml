// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Préférences : apparence (thème, densité, animations), langue, compagnon IA. Les valeurs vivent
// dans `prefs` (PreferencesController) et `companion` (réglages IA) ; aucune clé n'est relue.
Controls.Popup {
    id: dialog
    objectName: "preferencesDialog"
    readonly property bool hasPrefs: typeof prefs !== "undefined" && prefs !== null
    readonly property bool hasUsage: typeof usage !== "undefined" && usage !== null
    readonly property bool hasCompanion: typeof companion !== "undefined" && companion !== null
    modal: true
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
    parent: Controls.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent ? parent.width - 32 : 480)
    height: Math.min(560, parent ? parent.height - 32 : 560)
    padding: 16

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: Theme.radius
    }

    function openDialog() {
        keyField.text = ""
        keyMessage.text = ""
        open()
        closeButton.forceActiveFocus()
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight
        clip: true
        Controls.ScrollBar.vertical: Controls.ScrollBar { }
        ColumnLayout {
            id: column
            width: parent.width - 12
            spacing: 10

            Text {
                text: qsTr("Préférences")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.headingSize
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            SectionLabel { text: qsTr("APPARENCE") }
            RowLayout {
                spacing: 6
                Text { text: qsTr("Thème"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize; Layout.preferredWidth: 90 }
                Repeater {
                    model: [{ id: "system", label: qsTr("Système") }, { id: "light", label: qsTr("Clair") }, { id: "dark", label: qsTr("Sombre") }]
                    delegate: StudioButton {
                        required property var modelData
                        objectName: "themeChoice_" + modelData.id
                        text: modelData.label
                        active: dialog.hasPrefs && prefs.theme === modelData.id
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: active
                        onClicked: prefs.theme = modelData.id
                    }
                }
            }
            RowLayout {
                spacing: 6
                Text { text: qsTr("Densité"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize; Layout.preferredWidth: 90 }
                Repeater {
                    model: [{ id: "compact", label: qsTr("Compacte") }, { id: "normal", label: qsTr("Confortable") }, { id: "spacious", label: qsTr("Aérée") }]
                    delegate: StudioButton {
                        required property var modelData
                        objectName: "densityChoice_" + modelData.id
                        text: modelData.label
                        active: dialog.hasPrefs && prefs.density === modelData.id
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: active
                        onClicked: prefs.density = modelData.id
                    }
                }
            }
            StudioCheckBox {
                objectName: "reducedMotionCheck"
                text: qsTr("Réduire les animations")
                checked: dialog.hasPrefs && prefs.reducedMotion
                Accessible.name: text
                onToggled: prefs.reducedMotion = checked
                contentItem: Text {
                    leftPadding: 28
                    text: parent.text
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Langue : français (seule langue de l'interface pour l'instant). Les raccourcis sont fixes ; Aide ▸ Raccourcis les liste.")
                color: Theme.faint
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }

            SectionLabel { text: qsTr("USAGE") }
            StudioCheckBox {
                objectName: "usageLogCheck"
                visible: dialog.hasUsage
                text: qsTr("Enregistrer l'usage de l'application sur cet ordinateur (jamais envoyé)")
                checked: dialog.hasUsage && usage.enabled
                enabled: dialog.hasUsage && !usage.lockedByEnvironment
                Accessible.name: text
                onToggled: usage.enabled = checked
                contentItem: Text {
                    leftPadding: 28
                    text: parent.text
                    color: parent.enabled ? Theme.foreground : Theme.faint
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.hasUsage
                text: dialog.hasUsage && usage.lockedByEnvironment
                      ? qsTr("Désactivé par la variable d'environnement OPENSTITCH_USAGE_LOG.")
                      : qsTr("Outils, commandes et espaces utilisés, sans nom de fichier ni texte saisi. Dossier : ")
                        + (dialog.hasUsage ? usage.folder : "")
                color: Theme.faint
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }

            SectionLabel { text: qsTr("COMPAGNON IA") }
            StudioCheckBox {
                objectName: "companionDisabledCheck"
                text: qsTr("Désactiver entièrement le compagnon (pont et fournisseur)")
                checked: dialog.hasCompanion && companion.fullyDisabled
                Accessible.name: text
                onToggled: companion.fullyDisabled = checked
                contentItem: Text {
                    leftPadding: 28
                    text: parent.text
                    color: Theme.foreground
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    verticalAlignment: Text.AlignVCenter
                }
            }
            StudioCheckBox {
                objectName: "providerEnabledCheck"
                text: qsTr("Utiliser le fournisseur Claude (messages envoyés à api.anthropic.com)")
                checked: dialog.hasCompanion && companion.providerEnabled
                Accessible.name: text
                onToggled: companion.providerEnabled = checked
                contentItem: Text {
                    leftPadding: 28
                    text: parent.text
                    color: Theme.foreground
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    verticalAlignment: Text.AlignVCenter
                }
            }
            RowLayout {
                Text { text: qsTr("Modèle"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize; Layout.preferredWidth: 90 }
                StudioTextField {
                    objectName: "modelField"
                    Layout.fillWidth: true
                    text: dialog.hasCompanion ? companion.model : ""
                    Accessible.name: qsTr("Modèle Claude")
                    onEditingFinished: companion.model = text
                }
            }
            RowLayout {
                Text { text: qsTr("Clé API"); color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize; Layout.preferredWidth: 90 }
                StudioTextField {
                    id: keyField
                    objectName: "apiKeyField"
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: dialog.hasCompanion && companion.hasApiKey ? qsTr("Clé enregistrée (non affichée)") : qsTr("sk-ant-…")
                    enabled: dialog.hasCompanion && companion.secretStoreAvailable
                    Accessible.name: qsTr("Clé API Anthropic")
                }
            }
            RowLayout {
                StudioButton {
                    objectName: "apiKeySave"
                    text: qsTr("Enregistrer la clé")
                    enabled: keyField.text.length > 0 && dialog.hasCompanion && companion.secretStoreAvailable
                    onClicked: {
                        keyMessage.text = companion.setApiKey(keyField.text) ? qsTr("Clé enregistrée dans le stockage sécurisé du système.") : qsTr("Échec : la clé n'a pas été enregistrée.")
                        keyField.text = ""
                    }
                }
                StudioButton {
                    objectName: "apiKeyClear"
                    text: qsTr("Supprimer la clé")
                    enabled: dialog.hasCompanion && companion.hasApiKey
                    onClicked: { companion.clearApiKey(); keyMessage.text = qsTr("Clé supprimée.") }
                }
            }
            Text {
                id: keyMessage
                objectName: "apiKeyMessage"
                Layout.fillWidth: true
                visible: text !== "" || (dialog.hasCompanion && !companion.secretStoreAvailable)
                text: ""
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.hasCompanion && !companion.secretStoreAvailable
                text: qsTr("Aucun stockage sécurisé disponible sur ce système : la clé ne sera jamais écrite sur le disque.")
                color: Theme.accent
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
            }

            RowLayout {
                Layout.fillWidth: true
                StudioButton {
                    objectName: "prefsReset"
                    text: qsTr("Valeurs par défaut")
                    quiet: true
                    onClicked: prefs.resetDefaults()
                }
                Item { Layout.fillWidth: true }
                StudioButton {
                    id: closeButton
                    objectName: "prefsClose"
                    text: qsTr("Fermer")
                    emphasized: true
                    onClicked: dialog.close()
                }
            }
        }
    }
}
