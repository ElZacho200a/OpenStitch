// SPDX-License-Identifier: Apache-2.0
// Galerie des composants : tous les composants Studio* dans tous leurs états, avec le thème
// courant, pour repérer une incohérence esthétique avant de distribuer un thème. Les thèmes se
// changent à chaud depuis l'en-tête ; les contrastes des paires texte / fond sont mesurés en
// direct (WCAG, 4,5:1 pour le texte, 3:1 pour les contours).
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "."

Controls.Popup {
    id: gallery
    objectName: "themeGallery"
    readonly property bool hasEngine: typeof themeEngine !== "undefined" && themeEngine !== null
    modal: true
    focus: true
    parent: Controls.Overlay.overlay
    anchors.centerIn: parent
    width: parent ? Math.min(1180, parent.width - 48) : 1000
    height: parent ? Math.min(860, parent.height - 48) : 760
    padding: Theme.panelPadding
    closePolicy: Controls.Popup.CloseOnEscape
    background: StudioPopupBackground { }
    enter: StudioPopupEnter { }
    exit: StudioPopupExit { }

    readonly property var states: ["normal", "hover", "pressed", "selected", "focus", "disabled"]
    readonly property var stateLabels: ({ normal: qsTr("Normal"), hover: qsTr("Survol"), pressed: qsTr("Appui"),
                                          selected: qsTr("Sélection"), focus: qsTr("Focus"), disabled: qsTr("Inactif") })
    readonly property var iconNames: ["select", "pan", "fit", "open", "check", "save", "undo", "redo", "plus",
        "minus", "close", "layers", "needle", "chevron", "satin", "fade", "guide", "break", "rungs", "stitches",
        "text", "eyedropper", "measure", "applique", "script", "pull", "view", "thread", "analysis", "play", "realistic",
        "link", "edit", "tree", "list", "crop", "transparent", "rectangle", "ellipse", "polygon", "regular",
        "bezier", "freeform", "node", "knife", "warp", "bucket", "brush", "eraser"]

    function openGallery() { open(); closeGallery.forceActiveFocus() }

    // Contraste WCAG entre deux couleurs QML (même formule que theme_schema.cpp).
    function luminance(c) {
        const ch = (v) => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b)
    }
    function contrast(a, b) {
        const x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }

    component Heading: Text {
        color: Theme.foreground
        font.family: Theme.headingFamily
        font.pixelSize: Theme.headingSize
        font.weight: Theme.headingWeight
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }
    component Caption: Text {
        color: Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.captionSize
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gap

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: qsTr("Galerie des composants")
                color: Theme.foreground
                font.family: Theme.headingFamily
                font.pixelSize: Theme.brandSize
                font.weight: Theme.headingWeight
            }
            Caption {
                text: gallery.hasEngine ? qsTr("Thème : %1 (%2)").arg(themeEngine.meta.name || "").arg(Theme.light ? qsTr("clair") : qsTr("sombre")) : ""
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: gallery.hasEngine ? themeEngine.themes : []
                StudioButton {
                    required property var modelData
                    objectName: "galleryTheme_" + modelData.id
                    text: modelData.name
                    enabled: modelData.loadable
                    active: gallery.hasEngine && themeEngine.activeId === modelData.id
                    onClicked: themeEngine.activate(modelData.id)
                }
            }
            StudioButton {
                id: closeGallery
                objectName: "galleryClose"
                iconName: "close"
                tooltip: qsTr("Fermer la galerie")
                quiet: true
                onClicked: gallery.close()
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar { }
            ColumnLayout {
                id: body
                width: parent.width - 14
                spacing: 14

                Heading { text: qsTr("Boutons") }
                Repeater {
                    model: [{ v: "default", label: qsTr("Tonal") }, { v: "emphasized", label: qsTr("Plein") }, { v: "quiet", label: qsTr("Discret") }]
                    RowLayout {
                        id: variantRow
                        required property var modelData
                        spacing: 10
                        Caption { text: modelData.label; Layout.preferredWidth: 70 }
                        Repeater {
                            model: gallery.states
                            StudioButton {
                                required property string modelData
                                text: gallery.stateLabels[modelData]
                                iconName: "check"
                                emphasized: variantRow.modelData.v === "emphasized"
                                quiet: variantRow.modelData.v === "quiet"
                                forcedState: modelData
                                enabled: modelData !== "disabled"
                                focusPolicy: Qt.NoFocus
                            }
                        }
                    }
                }
                RowLayout {
                    spacing: 10
                    Caption { text: qsTr("Interactif"); Layout.preferredWidth: 70 }
                    StudioButton { text: qsTr("Survolez-moi"); iconName: "realistic" }
                    StudioButton { text: qsTr("Action principale"); emphasized: true; iconName: "save" }
                    StudioButton { iconName: "undo"; tooltip: qsTr("Bouton icône") }
                    StudioButton { text: qsTr("Bascule"); checkable: true; iconName: "layers" }
                }

                Heading { text: qsTr("Champs et choix") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 12
                    StudioTextField { placeholderText: qsTr("Texte…"); width: 160 }
                    StudioTextField { text: qsTr("Valeur invalide"); invalid: true; width: 160 }
                    StudioTextField { text: qsTr("Inactif"); enabled: false; width: 160 }
                    StudioSpinBox { value: 42; from: 0; to: 100; editable: true }
                    StudioComboBox { model: [qsTr("Tatami"), qsTr("Satin"), qsTr("Contour")] }
                    StudioComboBox { model: [qsTr("Inactif")]; enabled: false }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 16
                    StudioCheckBox { text: qsTr("Décochée") }
                    StudioCheckBox { text: qsTr("Cochée"); checked: true }
                    StudioCheckBox { text: qsTr("Partielle"); tristate: true; checkState: Qt.PartiallyChecked }
                    StudioCheckBox { text: qsTr("Erreur"); invalid: true }
                    StudioCheckBox { text: qsTr("Inactive"); enabled: false; checked: true }
                    StudioSwitch { text: qsTr("Interrupteur") }
                    StudioSwitch { text: qsTr("Activé"); checked: true }
                    StudioSwitch { text: qsTr("Inactif"); enabled: false }
                }
                RowLayout {
                    spacing: 16
                    StudioSlider { from: 0; to: 100; value: 35; Layout.preferredWidth: 220 }
                    StudioSlider { from: 0; to: 100; value: 70; enabled: false; Layout.preferredWidth: 220 }
                }

                Heading { text: qsTr("Typographie") }
                RowLayout {
                    spacing: 18
                    StudioLabel { role: "heading"; text: qsTr("Titre") }
                    StudioLabel { role: "body"; text: qsTr("Corps de texte") }
                    StudioLabel { role: "small"; text: qsTr("Petit texte") }
                    StudioLabel { role: "caption"; text: qsTr("Légende") }
                    SectionLabel { text: qsTr("SECTION") }
                }
                RowLayout {
                    spacing: 18
                    Repeater {
                        model: ["foreground", "muted", "faint", "accent", "error", "warning", "success"]
                        StudioLabel { required property string modelData; role: "body"; tone: modelData; text: modelData }
                    }
                }

                Heading { text: qsTr("Surfaces") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 18
                    StudioCard {
                        width: 200; height: 96
                        StudioLabel { anchors.centerIn: parent; role: "body"; text: qsTr("Carte flottante") }
                    }
                    StudioPanel {
                        width: 200
                        title: qsTr("Panneau")
                        StudioLabel { text: qsTr("Contenu du panneau") }
                    }
                    StudioPopupBackground {
                        width: 200; height: 96
                        StudioLabel { anchors.centerIn: parent; role: "body"; text: qsTr("Popup / menu") }
                    }
                    ThemedSurface {
                        readonly property var tipStyle: Theme.style("tooltip", "default", "normal")
                        width: 150; height: 34
                        themeStyle: tipStyle
                        fill: Theme.raised
                        radius: 4
                        borderWidth: 1
                        border: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Infobulle")
                            color: parent.tipStyle.text !== undefined ? parent.tipStyle.text : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallSize
                        }
                    }
                }
                RowLayout {
                    spacing: 8
                    Caption { text: qsTr("Onglets"); Layout.preferredWidth: 70 }
                    Repeater {
                        model: ["normal", "hover", "selected"]
                        ThemedSurface {
                            required property string modelData
                            readonly property var tabStyle: Theme.style("tab", "default", modelData)
                            width: 120; height: 32
                            themeStyle: tabStyle
                            radius: 9
                            fill: modelData === "selected" ? Theme.accent : modelData === "hover" ? Theme.raised : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: gallery.stateLabels[parent.modelData]
                                color: parent.tabStyle.text !== undefined ? parent.tabStyle.text
                                       : parent.modelData === "selected" ? Theme.accentInk : Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.bodySize
                            }
                        }
                    }
                    Caption { text: qsTr("Lignes"); Layout.leftMargin: 24 }
                    Repeater {
                        model: ["normal", "hover", "selected"]
                        ThemedSurface {
                            required property string modelData
                            width: 120; height: Theme.rowHeight * 0.8
                            themeStyle: Theme.style("listItem", "default", modelData)
                            radius: Theme.radius
                            fill: modelData === "selected" ? Theme.accentSoft : modelData === "hover" ? Theme.hover : "transparent"
                            StudioLabel { anchors.centerIn: parent; role: "small"; tone: "foreground"; text: gallery.stateLabels[parent.modelData] }
                        }
                    }
                }

                Heading { text: qsTr("Icônes") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: gallery.iconNames
                        Column {
                            required property string modelData
                            spacing: 2
                            width: 56
                            StudioIcon { anchors.horizontalCenter: parent.horizontalCenter; name: parent.modelData; color: Theme.foreground; width: 24; height: 24 }
                            Caption { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: parent.modelData }
                        }
                    }
                }

                Heading { text: qsTr("Couleurs et contrastes") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: ["foreground", "muted", "faint", "accent", "secondaryAccent", "info", "success", "warning", "error"]
                        Repeater {
                            id: grounds
                            required property string modelData
                            readonly property string fg: modelData
                            model: ["background", "surface", "panel", "raised"]
                            Rectangle {
                                required property string modelData
                                readonly property real ratio: gallery.contrast(Theme[grounds.fg], Theme[modelData])
                                width: 118; height: 40
                                radius: Theme.radius / 2
                                color: Theme[modelData]
                                border.width: ratio < 4.5 ? 2 : 0
                                border.color: Theme.error
                                Text {
                                    anchors.centerIn: parent
                                    horizontalAlignment: Text.AlignHCenter
                                    text: grounds.fg + "\n" + parent.modelData + " · " + parent.ratio.toFixed(1) + (parent.ratio >= 4.5 ? " ✓" : " ✕")
                                    color: Theme[grounds.fg]
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.captionSize
                                }
                            }
                        }
                    }
                }

                Heading { text: qsTr("Diagnostics du thème") }
                Text {
                    objectName: "galleryDiagnostics"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: {
                        if (!gallery.hasEngine)
                            return qsTr("Moteur de thèmes absent.")
                        const d = themeEngine.diagnostics
                        if (d.length === 0)
                            return qsTr("Aucun avertissement : jetons valides, ressources présentes, contrastes conformes.")
                        return d.map(x => (x.severity === "error" ? "✕ " : "⚠ ") + x.where + " — " + x.message).join("\n")
                    }
                    color: gallery.hasEngine && themeEngine.diagnostics.length > 0 ? Theme.warning : Theme.success
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallSize
                }
            }
        }
    }
}
