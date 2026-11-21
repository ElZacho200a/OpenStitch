// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import "../.."

// Panneau de l'espace Vectorisation : état de l'étape, origine de la forme sélectionnée (lien amont
// vers la région), actions « Mettre à jour depuis la segmentation » et « Convertir en broderie ».
// Aucune logique métier : tout passe par VectorSpaceController (`stage`).
ColumnLayout {
    id: panel
    objectName: "vectorShapePanel"
    required property var stage
    required property var workspace
    signal goToRegionRequested
    // Plan de mise à jour en attente de confirmation (retouches à écraser) ; null = pas de demande.
    property var pendingPlan: null
    // Carte de réglages de la conversion en broderie, ouverte par « ② Convertir en broderie ».
    property bool convertOptionsOpen: false
    // Petite fenêtre : les textes d'aide (redondants avec les infobulles) sont masqués pour que
    // les étapes ① Recalculer, Nettoyer et ② Convertir restent visibles sans défiler.
    property bool compact: false
    spacing: 9

    function askUpdate() {
        const plan = panel.stage.updatePlan()
        if (plan.replace > 0) {
            panel.pendingPlan = plan
        } else {
            panel.stage.updateFromSegmentation()
        }
    }

    SectionLabel { text: qsTr("VECTORISATION") }

    Text {
        objectName: "vectorStageDetail"
        Layout.fillWidth: true
        text: panel.stage ? panel.stage.stageDetail : ""
        color: panel.stage && panel.stage.stageState === "stale" ? Theme.warning : Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.bodySize
        wrapMode: Text.WordWrap
    }

    // Précision de la vectorisation (V1) : appliquée à la prochaine mise à jour des formes.
    SectionLabel { text: qsTr("PRÉCISION") }
    RowLayout {
        Layout.fillWidth: true
        visible: panel.stage && !panel.stage.curves
        Text {
            text: qsTr("Tolérance (mm)")
            color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
        }
        StudioSlider {
            objectName: "vectorPrecisionSlider"
            Layout.fillWidth: true
            from: 0.01; to: 2.0; stepSize: 0.01
            value: panel.stage ? panel.stage.precisionMm : 0.2
            Accessible.name: qsTr("Tolérance de simplification en millimètres")
            onMoved: panel.stage.precisionMm = value
        }
        // Saisie fine (VEC-ADAPT-01) : mêmes bornes que le contrôleur, qui clampe.
        StudioTextField {
            objectName: "vectorPrecisionValue"
            implicitWidth: 56
            text: panel.stage ? panel.stage.precisionMm.toFixed(2) : ""
            wheelStep: 0.01; wheelMin: 0.01; wheelMax: 2.0; wheelDecimals: 2
            Accessible.name: qsTr("Tolérance de simplification, saisie en millimètres")
            onEditingFinished: { const v = Number(text.replace(",", ".")); if (isFinite(v)) panel.stage.precisionMm = v; else text = panel.stage.precisionMm.toFixed(2) }
        }
    }
    StudioCheckBox {
        objectName: "vectorCurvesBox"
        text: qsTr("Courbes de Bézier (moins de nœuds)")
        checked: panel.stage ? panel.stage.curves : false
        onToggled: panel.stage.curves = checked
        palette.windowText: Theme.foreground
    }
    RowLayout {
        Layout.fillWidth: true
        visible: panel.stage && panel.stage.curves
        Text {
            text: qsTr("Écart max. (px)")
            color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
        }
        StudioSlider {
            objectName: "vectorCurveToleranceSlider"
            Layout.fillWidth: true
            from: 0.1; to: 10.0; stepSize: 0.1
            value: panel.stage ? panel.stage.curveTolerancePx : 1.5
            Accessible.name: qsTr("Écart maximal des courbes en pixels")
            onMoved: panel.stage.curveTolerancePx = value
        }
        StudioTextField {
            objectName: "vectorCurveToleranceValue"
            implicitWidth: 56
            text: panel.stage ? panel.stage.curveTolerancePx.toFixed(2) : ""
            wheelStep: 0.05; wheelMin: 0.1; wheelMax: 10.0; wheelDecimals: 2
            Accessible.name: qsTr("Écart maximal des courbes, saisie en pixels")
            onEditingFinished: { const v = Number(text.replace(",", ".")); if (isFinite(v)) panel.stage.curveTolerancePx = v; else text = panel.stage.curveTolerancePx.toFixed(2) }
        }
    }
    // Chaque réglage et chaque action dit ce qu'il produit, en une ligne, sans survol.
    Text {
        objectName: "vectorPrecisionHelp"
        visible: !panel.compact
        Layout.fillWidth: true
        text: qsTr("Tolérance haute : contours plus simples, moins de nœuds. Basse : plus fidèle à l'image (sous 1 px, les courbes suivent les escaliers des pixels). Pris en compte à l'étape ①.")
        color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap
    }

    StudioButton {
        objectName: "vectorUpdateButton"
        Layout.fillWidth: true
        text: qsTr("① Recalculer les formes")
        tooltip: qsTr("Régénère les formes depuis les régions (une seule étape d'annulation)")
        enabled: panel.stage !== null && panel.stage.stageState !== "blocked"
        onClicked: panel.askUpdate()
    }
    Text {
        objectName: "vectorUpdateHelp"
        visible: !panel.compact
        Layout.fillWidth: true
        text: qsTr("Redessine les contours depuis les régions segmentées du calque courant. Les retouches faites sur ces formes sont écrasées (confirmation demandée).")
        color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap
    }
    StudioButton {
        objectName: "vectorUpdateAllButton"
        Layout.fillWidth: true
        // Recalculé quand les formes changent (layerChanged) ; visible dès deux calques segmentés.
        readonly property int layers: panel.stage ? (panel.stage.shapeCount, panel.stage.segmentedLayerCount()) : 0
        visible: layers > 1
        text: qsTr("Vectoriser les %1 calques segmentés").arg(layers)
        tooltip: qsTr("Met à jour les formes de tous les calques en une seule étape d'annulation")
        enabled: !studio.busy
        onClicked: panel.stage.updateAllLayersFromSegmentation()
    }

    Rectangle {
        objectName: "vectorUpdateConfirm"
        visible: panel.pendingPlan !== null
        Layout.fillWidth: true
        implicitHeight: confirmColumn.implicitHeight + 2 * Theme.panelPadding
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.warning
        ColumnLayout {
            id: confirmColumn
            anchors.fill: parent
            anchors.margins: Theme.panelPadding
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: panel.pendingPlan
                      ? qsTr("%1 forme(s) vont être remplacées : les retouches de ces formes seront écrasées.").arg(panel.pendingPlan.replace)
                      : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
            RowLayout {
                spacing: 6
                StudioButton {
                    objectName: "vectorUpdateConfirmButton"
                    text: qsTr("Écraser")
                    emphasized: true
                    onClicked: {
                        panel.pendingPlan = null
                        panel.stage.updateFromSegmentation()
                    }
                }
                StudioButton {
                    objectName: "vectorUpdateCancelButton"
                    text: qsTr("Annuler")
                    onClicked: panel.pendingPlan = null
                }
            }
        }
    }

    // Nettoyage facultatif entre la vectorisation et la conversion : les miettes laissées par la
    // vectorisation font des trous et des coupes ; on les fond dans leurs voisines plutôt que de
    // les supprimer, et on rebouche les vides entre formes. Seuils par défaut (réglables dans
    // Édition > Miettes et dans la palette).
    readonly property int crumbCount: { editor.tree; return editor.smallShapeCount(3.0) }
    Text {
        objectName: "vectorCleanTitle"
        Layout.fillWidth: true
        Layout.topMargin: 4
        text: panel.crumbCount > 0
              ? qsTr("Nettoyer (facultatif) : %1 miette(s) de moins de 3 mm²").arg(panel.crumbCount)
              : qsTr("Nettoyer (facultatif) : aucune miette de moins de 3 mm²")
        color: panel.crumbCount > 0 ? Theme.warning : Theme.muted
        font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        StudioButton {
            objectName: "vectorAbsorbCrumbs"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("Absorber les miettes")
            tooltip: qsTr("Fond chaque petite forme dans sa voisine (même fil d'abord) : plus de trou à sa place. Une étape d'annulation.")
            enabled: panel.crumbCount > 0 && !studio.busy
            disabledReason: qsTr("Aucune miette de moins de 3 mm²")
            onClicked: actions.run("shape.absorb_crumbs", { "max_area_mm2": "3", "priority": "same_thread", "reach_mm": "0.2" })
        }
        StudioButton {
            objectName: "vectorFillGaps"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("Reboucher les trous")
            tooltip: qsTr("Rebouche les petits vides fermés ENTRE formes (moins de 4 mm²). Les trous voulus à l'intérieur d'une forme restent. Une étape d'annulation.")
            enabled: panel.stage !== null && panel.stage.shapeCount > 1 && !studio.busy
            disabledReason: qsTr("Il faut au moins deux formes")
            onClicked: actions.run("shape.fill_gaps_between", { "max_area_mm2": "4", "reach_mm": "0.2" })
        }
    }

    StudioButton {
        objectName: "vectorConvertButton"
        Layout.fillWidth: true
        text: panel.stage && panel.stage.converting ? qsTr("Conversion en cours…") : qsTr("② Convertir en broderie")
        // Action principale de l'espace : la seule pleine du panneau.
        emphasized: enabled
        tooltip: qsTr("Crée les objets de broderie des formes qui n'en ont pas encore")
        enabled: panel.stage !== null && panel.stage.shapeCount > 0 && !panel.stage.converting
        onClicked: panel.convertOptionsOpen = !panel.convertOptionsOpen
    }
    // Choix avant conversion : par défaut, aucune déformation des contours vectoriels.
    Rectangle {
        objectName: "vectorConvertOptions"
        visible: panel.convertOptionsOpen
        Layout.fillWidth: true
        implicitHeight: convertColumn.implicitHeight + 2 * Theme.panelPadding
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.divider
        ColumnLayout {
            id: convertColumn
            anchors.fill: parent
            anchors.margins: Theme.panelPadding
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: qsTr("Réglages de la conversion")
                color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.bodySize
            }
            StudioCheckBox {
                id: pullBox
                objectName: "vectorConvertPullBox"
                Layout.fillWidth: true
                text: qsTr("Compensation du tirage (auto)")
                palette.windowText: Theme.foreground
            }
            StudioCheckBox {
                id: overlapBox
                objectName: "vectorConvertOverlapBox"
                Layout.fillWidth: true
                text: qsTr("Bloc uni (débord 0,5 mm)")
                palette.windowText: Theme.foreground
            }
            StudioCheckBox {
                id: satinBox
                objectName: "vectorConvertSatinBox"
                Layout.fillWidth: true
                text: qsTr("Bandes étroites en satin")
                palette.windowText: Theme.foreground
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Espacement (mm)")
                    color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
                }
                StudioSlider {
                    id: spacingSlider
                    objectName: "vectorConvertSpacingSlider"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 40
                    from: 0.2; to: 0.8; stepSize: 0.05
                    value: 0.4
                    Accessible.name: qsTr("Espacement des rangées de fils en millimètres")
                }
                Text {
                    text: spacingSlider.value.toFixed(2)
                    color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Tout est décoché par défaut : les points suivent exactement les contours vectoriels. Le tirage auto agrandit les formes de 0,1 à 0,4 mm ; le bloc uni les fait déborder sous leurs voisines ; sans satin, tout est en remplissage. Modifiable ensuite objet par objet.")
                color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap
            }
            RowLayout {
                spacing: 6
                StudioButton {
                    objectName: "vectorConvertConfirmButton"
                    text: qsTr("Convertir")
                    emphasized: true
                    onClicked: {
                        panel.convertOptionsOpen = false
                        panel.stage.convertToEmbroidery({
                            "pull": pullBox.checked ? "auto" : "none",
                            "overlapMm": overlapBox.checked ? 0.5 : 0.0,
                            "satin": satinBox.checked,
                            "spacingMm": spacingSlider.value
                        })
                    }
                }
                StudioButton {
                    objectName: "vectorConvertCancelButton"
                    text: qsTr("Annuler")
                    onClicked: panel.convertOptionsOpen = false
                }
            }
        }
    }
    Text {
        objectName: "vectorConvertHelp"
        visible: !panel.compact
        Layout.fillWidth: true
        text: qsTr("Crée un objet de points (satin, remplissage…) pour chaque forme qui n'en a pas encore. Les formes déjà converties ne changent pas. Résultat dans l'espace Points.")
        color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap
    }

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

    SectionLabel { text: qsTr("FORME SÉLECTIONNÉE") }

    Text {
        objectName: "vectorShapeInfo"
        Layout.fillWidth: true
        readonly property var shape: panel.stage ? panel.stage.selectedShape : ({})
        text: shape.id === undefined
              ? qsTr("Cliquez une forme pour voir son origine.")
              : qsTr("%1 · %2 nœud(s) · %3 morceau(x)%4").arg(shape.name.length > 0 ? shape.name : qsTr("Forme"))
                    .arg(shape.nodes).arg(shape.pieces)
                    .arg(shape.embroidered ? qsTr(" · déjà en broderie") : "")
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
    }

    Text {
        objectName: "vectorShapeOrigin"
        Layout.fillWidth: true
        visible: shape.id !== undefined
        readonly property var shape: panel.stage ? panel.stage.selectedShape : ({})
        text: shape.id === undefined ? ""
              : !shape.hasSource ? qsTr("Origine : dessinée à la main (aucune région).")
              : shape.regionExists ? qsTr("Origine : région %1 de la segmentation.").arg(shape.regionId)
              : qsTr("Origine : région %1, qui n'existe plus.").arg(shape.regionId)
        color: shape.hasSource && !shape.regionExists ? Theme.warning : Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
    }

    // Calque de la forme sélectionnée : cliquable pour le rendre actif (même action que
    // ObjectTree/LayerPanel, via EditController).
    Text {
        objectName: "vectorShapeLayer"
        Layout.fillWidth: true
        readonly property var shape: panel.stage ? panel.stage.selectedShape : ({})
        visible: shape.id !== undefined && shape.layerName !== undefined && shape.layerName.length > 0
        text: qsTr("Calque : %1").arg(shape.layerName)
        color: layerArea.containsMouse ? Theme.foreground : Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        font.underline: layerArea.containsMouse
        MouseArea {
            id: layerArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: editor.setActiveLayer(parent.shape.layerId)
        }
    }

    StudioButton {
        objectName: "vectorGoToRegionButton"
        Layout.fillWidth: true
        readonly property var shape: panel.stage ? panel.stage.selectedShape : ({})
        text: qsTr("Voir la région d'origine")
        tooltip: qsTr("Ouvre l'espace Segmentation sur la région d'origine de la forme")
        enabled: shape.id !== undefined && shape.hasSource === true && shape.regionExists === true
        onClicked: panel.goToRegionRequested()
    }

    Text {
        objectName: "vectorMessage"
        Layout.fillWidth: true
        visible: text.length > 0
        text: panel.stage ? panel.stage.message : ""
        color: Theme.faint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
    }
}
