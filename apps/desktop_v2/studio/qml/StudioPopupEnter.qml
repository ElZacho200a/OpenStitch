// SPDX-License-Identifier: Apache-2.0
// Ouverture d'un popup : fondu et léger agrandissement (motion.popupScale, motion.easingEmphasis).
// Instantanée en mouvement réduit (durées à 0, échelle de départ 1).
import QtQuick
import "."

Transition {
    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationMs; easing.type: Theme.easingType }
    NumberAnimation { property: "scale"; from: Theme.popupScale; to: 1; duration: Theme.feedbackSlowMs; easing.type: Theme.easingEmphasis }
}
