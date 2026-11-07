// SPDX-License-Identifier: Apache-2.0
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import OpenStitch.Studio 1.0
import ".."

// Espace « Segmentation » : COQUE de la charpente V0. Fichier POSSÉDÉ par l'agent de l'espace
// (docs/desktop-v2/23-espaces-studio.md) ; il ne lit/écrit que les contrôleurs de contexte
// (studio, editor, tools, ...) et publie l'état de SON étape par `workspace.setStageStatus`.
//
// POINT D'EXTENSION segmentation : paramètres de segmentation, régions, fusion/scission, masque de détail. Publier « stale » quand des régions sont corrigées sans répercussion sur les formes (workspace.segmentationDirty est alimenté par regionTools.stale).
// Contrat : racine `Item { required property var workspace; property bool active }`, propriété
// `canvas` (le GpuCanvas de l'espace, sauvegardé/restauré par SpaceHost), sous-composants dans
// `qml/spaces/segmentation/` (à déclarer dans studio/CMakeLists.txt, ligne d'ajout).
Item {
    id: root
    objectName: "segmentationSpace"
    required property var workspace
    property bool active: false
    readonly property string spaceId: "segmentation"
    readonly property var canvas: spaceCanvas

    // Couches dessinées par CE canevas (GpuCanvas.layers) ; « stitches » éteint tant que l'espace
    // n'a pas de contenu propre.
    readonly property var canvasLayers: ({ regions: true, stitches: false, selectionOverlays: false })

    // --- état publié et action « mettre à jour » du bandeau -----------------------------------
    // Exemple : workspace.setStageStatus(spaceId, "upToDate", qsTr("12 formes"));
    //           workspace.setStageAction(spaceId, qsTr("Resegmenter"), true);
    Connections {
        target: root.workspace
        // L'espace courant exécute la mise à jour de SON étape (une seule étape d'annulation).
        function onUpdateRequested(id) {
            if (id === root.spaceId) {
                // TODO(agent d'espace segmentation) : régénérer cette étape depuis l'amont.
            }
        }
    }

    Item {
        id: viewport
        anchors.fill: parent
        clip: true

        GpuCanvas {
            id: spaceCanvas
            objectName: "segmentationCanvas"
            anchors.fill: parent
            controller: studio
            editor: root.editorController
            tools: root.toolController
            layers: root.canvasLayers
            focus: true
        }
        Rectangle {
            objectName: "spaceUnderConstruction"
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 64)
            height: message.implicitHeight + 36
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            Text {
                id: message
                anchors.centerIn: parent
                width: parent.width - 36
                text: qsTr("Espace « Segmentation » en construction.\nLes régions de la segmentation s'afficheront ici.")
                color: Theme.muted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.35
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodySize
            }
        }

        ViewPanel {
            objectName: "viewHub"
            anchors.fill: parent
            canvasItem: spaceCanvas
        }
    }

    readonly property var editorController: editor
    readonly property var toolController: typeof tools !== "undefined" ? tools : null
}
