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

    StudioButton {
        objectName: "vectorConvertButton"
        Layout.fillWidth: true
        text: panel.stage && panel.stage.converting ? qsTr("Conversion en cours…") : qsTr("② Convertir en broderie")
        // Action principale de l'espace : la seule pleine du panneau.
        emphasized: enabled
        tooltip: qsTr("Crée les objets de broderie des formes qui n'en ont pas encore")
        enabled: panel.stage !== null && panel.stage.shapeCount > 0 && !panel.stage.converting
        onClicked: panel.stage.convertToEmbroidery()
    }
    Text {
        objectName: "vectorConvertHelp"
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
