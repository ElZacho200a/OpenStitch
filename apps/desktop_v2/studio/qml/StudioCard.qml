// SPDX-License-Identifier: Apache-2.0
// Carte flottante : surface arrondie détachée des bords, posée au-dessus du plan de travail.
// L'ombre est faite de rectangles translucides empilés (ThemedSurface) : elle fonctionne aussi en
// rendu logiciel (pas de ShaderEffect). Le thème peut tout surcharger via le style « card » ;
// `themed: false` garde la couleur posée par l'appelant (carte sémantique : alerte, aperçu…).
import QtQuick
import "."

Item {
    id: card
    default property alias content: body.data
    property int radius: Theme.cardRadius
    property color color: Theme.panel
    property color borderColor: Theme.divider
    property bool elevated: true
    property bool themed: true
    // Style du thème à appliquer (« card » par défaut, « header » pour l'en-tête) ; un style
    // propre absent du thème retombe sur celui des cartes.
    property string themeComponent: "card"
    readonly property var ownStyle: Theme.style(themeComponent, "default", "normal")

    ThemedSurface {
        anchors.fill: parent
        themeStyle: !card.themed ? ({}) : Object.keys(card.ownStyle).length > 0 ? card.ownStyle
                    : Theme.style("card", "default", "normal")
        radius: card.radius
        fill: card.color
        border: card.borderColor
        borderWidth: Theme.borderWidth
        shadowLayers: Theme.shadowLayers
        shadowEnabled: card.elevated
        animated: false
    }

    Item {
        id: body
        anchors.fill: parent
    }
}
