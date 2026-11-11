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
    signal updateRequested
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
            from: 0.05; to: 1.0; stepSize: 0.05
            value: panel.stage ? panel.stage.precisionMm : 0.2
            Accessible.name: qsTr("Tolérance de simplification en millimètres")
            onMoved: panel.stage.precisionMm = value
        }
        Text {
            objectName: "vectorPrecisionValue"
            text: panel.stage ? panel.stage.precisionMm.toFixed(2) : ""
            color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
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
            from: 0.5; to: 5.0; stepSize: 0.5
            value: panel.stage ? panel.stage.curveTolerancePx : 1.5
            Accessible.name: qsTr("Écart maximal des courbes en pixels")
            onMoved: panel.stage.curveTolerancePx = value
        }
        Text {
            text: panel.stage ? panel.stage.curveTolerancePx.toFixed(1) : ""
            color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("S'applique à « Mettre à jour depuis la segmentation ».")
        color: Theme.faint; font.family: Theme.fontFamily; font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
    }

    StudioButton {
        objectName: "vectorUpdateButton"
        Layout.fillWidth: true
        text: qsTr("Mettre à jour depuis la segmentation")
        tooltip: qsTr("Régénère les formes depuis les régions (une seule étape d'annulation)")
        enabled: panel.stage !== null && panel.stage.stageState !== "blocked"
        onClicked: panel.askUpdate()
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
        text: qsTr("Convertir en broderie")
        tooltip: qsTr("Crée les objets de broderie des formes qui n'en ont pas encore")
        enabled: panel.stage !== null && panel.stage.shapeCount > 0
        onClicked: panel.stage.convertToEmbroidery()
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
        text: qsTr("Aller à la région")
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
