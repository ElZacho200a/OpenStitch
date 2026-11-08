// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

// Compagnon IA : discussion avec Claude, pont MCP lié au document ouvert, journal des demandes et
// carte d'approbation (avant / après) de chaque modification proposée. Aucune logique ici : tout
// vient de `companion` (CompanionController). Absent du contexte = panneau inerte.
Controls.Popup {
    id: panel
    objectName: "companionPanel"
    readonly property bool hasCompanion: typeof companion !== "undefined" && companion !== null
    modal: false
    focus: false
    closePolicy: Controls.Popup.CloseOnEscape
    parent: Controls.Overlay.overlay
    width: Math.min(400, parent ? parent.width - 24 : 400)
    height: parent ? parent.height - 24 : 560
    x: parent ? parent.width - width - 12 : 0
    y: 12
    padding: 12

    function openPanel() {
        open()
        if (hasCompanion && !companion.proposalPending)
            input.forceActiveFocus()
    }

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: Theme.radius
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        visible: panel.hasCompanion

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Compagnon IA")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.headingSize
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            StudioButton {
                objectName: "companionClose"
                iconName: "close"
                tooltip: qsTr("Fermer le compagnon")
                quiet: true
                implicitWidth: 28
                implicitHeight: 28
                onClicked: panel.close()
            }
        }

        // ---- Pont MCP ----
        Text {
            objectName: "companionStatus"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            text: !panel.hasCompanion ? "" : companion.bridgeActive
                  ? qsTr("Pont actif : un agent (Claude Code) peut lire et proposer des changements sur le document ouvert. Chaque modification demande votre accord.")
                  : qsTr("Pont coupé : aucun agent externe n'est relié au document.")
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            StudioButton {
                objectName: "companionBridgeToggle"
                text: panel.hasCompanion && companion.bridgeActive ? qsTr("Couper le pont") : qsTr("Activer le pont")
                tooltip: qsTr("Relier ou détacher un agent MCP du document ouvert")
                onClicked: {
                    const refusal = companion.setBridgeEnabled(!companion.bridgeActive)
                    bridgeError.text = refusal
                }
            }
            StudioButton {
                objectName: "companionOpenClaudeCode"
                text: qsTr("Ouvrir Claude Code")
                tooltip: qsTr("Active le pont et copie la commande de branchement de Claude Code")
                onClicked: {
                    const refusal = companion.prepareClaudeCode()
                    bridgeError.text = refusal !== "" ? refusal : qsTr("Commande copiée : collez-la dans un terminal.")
                }
            }
        }
        Text {
            id: bridgeError
            objectName: "companionBridgeMessage"
            visible: text !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        // ---- Proposition en attente ----
        Rectangle {
            id: proposalCard
            objectName: "companionProposal"
            visible: panel.hasCompanion && companion.proposalPending
            Layout.fillWidth: true
            implicitHeight: proposalColumn.implicitHeight + 20
            color: Theme.surface
            border.color: Theme.accent
            radius: Theme.radius
            ColumnLayout {
                id: proposalColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: panel.hasCompanion ? qsTr("Proposition : ") + (companion.proposal.title || "") : ""
                    color: Theme.foreground
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodySize
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.hasCompanion && !!(companion.proposal.before || companion.proposal.after)
                    spacing: 8
                    Repeater {
                        model: [qsTr("Avant"), qsTr("Après")]
                        delegate: ColumnLayout {
                            id: previewColumn
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Text {
                                text: previewColumn.modelData
                                color: Theme.faint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.smallSize
                            }
                            Image {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 110
                                fillMode: Image.PreserveAspectFit
                                source: panel.hasCompanion
                                        ? ((previewColumn.index === 0 ? companion.proposal.before : companion.proposal.after) || "") : ""
                                Accessible.role: Accessible.Graphic
                                Accessible.name: qsTr("Aperçu ") + previewColumn.modelData
                            }
                        }
                    }
                }
                Repeater {
                    model: panel.hasCompanion ? companion.proposal.items : []
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: "• " + modelData.text
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
                Repeater {
                    model: panel.hasCompanion ? companion.proposal.metrics : []
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        color: Theme.success
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
                Repeater {
                    model: panel.hasCompanion ? companion.proposal.warnings : []
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true
                        text: "⚠ " + modelData
                        color: Theme.accent
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallSize
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    StudioButton {
                        objectName: "companionReject"
                        text: qsTr("Rejeter")
                        tooltip: qsTr("Refuser : le document ne change pas")
                        onClicked: companion.reject()
                    }
                    StudioButton {
                        objectName: "companionApprove"
                        text: qsTr("Appliquer")
                        emphasized: true
                        tooltip: qsTr("Appliquer en une étape d'annulation")
                        onClicked: companion.approve()
                    }
                }
            }
        }

        // ---- Discussion ----
        ListView {
            id: chat
            objectName: "companionMessages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            clip: true
            spacing: 6
            model: panel.hasCompanion ? companion.messages : []
            onCountChanged: positionViewAtEnd()
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                wrapMode: Text.WordWrap
                text: (modelData.role === "user" ? qsTr("Vous : ") : modelData.role === "tool" ? qsTr("Outil : ") : "") + modelData.text
                color: modelData.role === "error" ? Theme.accent : modelData.role === "user" ? Theme.foreground : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 20
                visible: chat.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.faint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                text: panel.hasCompanion && companion.providerReady
                      ? qsTr("Demandez par exemple « passe l'objet sélectionné en rouge ».")
                      : qsTr("Aucun fournisseur prêt : renseignez une clé API dans Préférences ▸ Compagnon IA, ou reliez Claude Code avec le pont.")
            }
        }
        Text {
            objectName: "companionUsage"
            visible: text !== ""
            Layout.fillWidth: true
            color: Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.captionSize
            text: panel.hasCompanion ? companion.usage : ""
        }

        RowLayout {
            Layout.fillWidth: true
            StudioTextField {
                id: input
                objectName: "companionInput"
                Layout.fillWidth: true
                enabled: panel.hasCompanion && !companion.busy && !companion.proposalPending
                placeholderText: qsTr("Demander à Claude…")
                Accessible.name: qsTr("Message pour Claude")
                onAccepted: sendButton.clicked()
            }
            StudioButton {
                id: sendButton
                objectName: "companionSend"
                text: panel.hasCompanion && companion.busy ? qsTr("Annuler") : qsTr("Envoyer")
                emphasized: !(panel.hasCompanion && companion.busy)
                tooltip: panel.hasCompanion && companion.busy ? qsTr("Interrompre la demande en cours") : qsTr("Envoyer le message")
                onClicked: {
                    if (companion.busy) {
                        companion.cancel()
                    } else if (companion.send(input.text)) {
                        input.text = ""
                    }
                }
            }
        }

        // ---- Journal ----
        Text {
            text: qsTr("Journal de l'agent")
            color: Theme.faint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
        }
        ListView {
            objectName: "companionLog"
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            clip: true
            model: panel.hasCompanion ? companion.events : []
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                elide: Text.ElideRight
                text: (modelData.outcome === "applied" ? qsTr("Appliqué") : modelData.outcome === "refused" ? qsTr("Refusé") : qsTr("En attente"))
                      + " · " + modelData.label
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                Accessible.name: text
            }
        }
        RowLayout {
            Layout.fillWidth: true
            StudioButton {
                objectName: "companionUndo"
                text: qsTr("Annuler la dernière modification")
                enabled: panel.hasCompanion && companion.canUndo
                tooltip: panel.hasCompanion && companion.canUndo ? qsTr("Annuler : %1").arg(companion.undoLabel) : qsTr("Rien à annuler")
                onClicked: companion.undoLast()
            }
            Item { Layout.fillWidth: true }
            StudioButton {
                objectName: "companionReset"
                text: qsTr("Nouvelle conversation")
                quiet: true
                onClicked: companion.resetConversation()
            }
        }
    }
}
