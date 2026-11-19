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

    background: StudioPopupBackground { }
    enter: StudioPopupEnter { }
    exit: StudioPopupExit { }

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
                  ? (companion.claudeCodeActive
                     ? qsTr("Claude Code est branché : écrivez votre demande ci-dessous. Il travaille sur le document ouvert, et chaque étape reste annulable (Ctrl+Z).")
                     : qsTr("Pont actif : un agent externe peut lire et modifier le document ouvert. Chaque étape reste annulable (Ctrl+Z)."))
                  : qsTr("Pont coupé : aucun agent externe n'est relié au document.")
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            StudioButton {
                objectName: "companionBridgeToggle"
                text: panel.hasCompanion && companion.bridgeActive ? qsTr("Couper le pont") : qsTr("Activer le pont")
                tooltip: qsTr("Active le pont et branche Claude Code directement dans ce panneau (sans terminal)")
                onClicked: {
                    // Activer = relier ET lancer Claude Code (terminal, MCP, skills) ; couper = détacher.
                    const refusal = companion.bridgeActive ? companion.setBridgeEnabled(false)
                                                           : companion.prepareClaudeCode()
                    bridgeError.text = refusal
                }
            }
            StudioButton {
                objectName: "companionOpenClaudeCode"
                text: qsTr("Dans un terminal")
                tooltip: qsTr("Option secondaire : ouvre Claude Code dans un terminal externe, relié à ce document")
                onClicked: {
                    const refusal = companion.openClaudeCodeTerminal()
                    bridgeError.text = refusal !== "" ? refusal : qsTr("Claude Code est lancé dans un terminal.")
                }
            }
        }
        StudioCheckBox {
            objectName: "companionAutoApply"
            Layout.fillWidth: true
            text: qsTr("Mode automatique : l'IA travaille en direct, sans validation (Ctrl+Z annule)")
            checked: panel.hasCompanion && companion.autoApply
            onToggled: companion.autoApply = checked
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
                Text {
                    Layout.fillWidth: true
                    visible: itemList.count > 6
                    text: qsTr("%1 opérations").arg(itemList.count)
                    color: Theme.faint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
                // Liste bornée et défilante : un lot de centaines d'opérations ne doit jamais
                // pousser les boutons Rejeter / Appliquer hors du panneau.
                ListView {
                    id: itemList
                    objectName: "companionProposalItems"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 150)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.hasCompanion ? companion.proposal.items : []
                    Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
                    delegate: Text {
                        required property var modelData
                        width: ListView.view.width - 10
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
            // Suit le bas tant que l'utilisateur n'est pas remonté lire : les messages
            // arrivent en flux (le texte grandit sans changer le nombre de lignes).
            property bool followEnd: true
            onMovementEnded: followEnd = atYEnd
            onCountChanged: if (followEnd) Qt.callLater(positionViewAtEnd)
            onContentHeightChanged: if (followEnd) Qt.callLater(positionViewAtEnd)
            delegate: Item {
                id: row
                required property var modelData
                readonly property string role: modelData.role
                readonly property bool isUser: role === "user"
                width: ListView.view.width
                implicitHeight: bubble.implicitHeight
                // Vous : bulle rose à droite ; Claude : carte neutre à gauche ; outils : puce
                // monospace ; réflexion : italique atténué (clic = tout lire).
                Rectangle {
                    id: bubble
                    anchors.right: row.isUser ? parent.right : undefined
                    anchors.left: row.isUser ? undefined : parent.left
                    width: Math.min(parent.width - (row.isUser ? 24 : 0), row.role === "tool" ? parent.width : parent.width - 8)
                    implicitHeight: content.implicitHeight + 14
                    radius: Theme.radius
                    color: row.isUser ? Theme.accentSoft
                           : row.role === "assistant" ? Theme.surface
                           : row.role === "tool" ? Theme.raised
                           : "transparent"
                    border.width: row.role === "assistant" || row.isUser ? 1 : 0
                    border.color: row.isUser ? Theme.accent : Theme.divider
                    ColumnLayout {
                        id: content
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 2
                        Text {
                            visible: row.role === "user" || row.role === "assistant" || row.role === "thinking" || row.role === "tool"
                            text: row.isUser ? qsTr("Vous")
                                  : row.role === "assistant" ? qsTr("Claude")
                                  : row.role === "thinking" ? qsTr("Réflexion")
                                  : "⚙ " + (row.modelData.title || qsTr("outil"))
                            color: row.isUser ? Theme.accent
                                   : row.role === "assistant" ? Theme.secondaryAccent
                                   : row.role === "tool" ? Theme.info
                                   : Theme.faint
                            font.family: row.role === "tool" ? "Consolas" : Theme.fontFamily
                            font.pixelSize: Theme.captionSize
                            font.weight: Font.DemiBold
                            font.italic: row.role === "thinking"
                        }
                        Text {
                            id: body
                            Layout.fillWidth: true
                            property bool expanded: false
                            wrapMode: Text.WordWrap
                            maximumLineCount: row.role === "thinking" && !expanded ? 3 : 100000
                            elide: Text.ElideRight
                            text: row.modelData.text
                            textFormat: row.role === "assistant" ? Text.MarkdownText : Text.PlainText
                            color: row.role === "error" ? Theme.error
                                   : row.isUser ? Theme.foreground
                                   : row.role === "assistant" ? Theme.foreground
                                   : row.role === "tool" ? Theme.muted
                                   : Theme.faint
                            font.family: row.role === "tool" ? "Consolas" : Theme.fontFamily
                            font.pixelSize: row.role === "assistant" || row.isUser ? Theme.bodySize : Theme.smallSize
                            font.italic: row.role === "thinking"
                            font.weight: row.isUser ? Font.Medium : Font.Normal
                            Accessible.role: Accessible.StaticText
                            Accessible.name: (row.isUser ? qsTr("Vous : ") : row.role === "tool" ? qsTr("Outil ") + (row.modelData.title || "") + " : " : "") + text
                            MouseArea {
                                anchors.fill: parent
                                enabled: row.role === "thinking"
                                onClicked: body.expanded = !body.expanded
                            }
                        }
                    }
                }
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
            id: activityLine
            objectName: "companionActivity"
            visible: panel.hasCompanion && companion.busy
            Layout.fillWidth: true
            color: Theme.info
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallSize
            font.italic: true
            property int dots: 0
            text: "● " + (panel.hasCompanion && companion.activity !== "" ? companion.activity : qsTr("Claude travaille…"))
                  + ".".repeat(dots)
            Timer {
                interval: 450
                repeat: true
                running: activityLine.visible && !Theme.reducedMotion
                onTriggered: activityLine.dots = (activityLine.dots + 1) % 4
            }
            Accessible.role: Accessible.StaticText
            Accessible.name: text
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
