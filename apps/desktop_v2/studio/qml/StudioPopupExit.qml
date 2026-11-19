// SPDX-License-Identifier: Apache-2.0
// Fermeture d'un popup : fondu court (instantané en mouvement réduit).
import QtQuick
import "."

Transition {
    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.feedbackFastMs; easing.type: Theme.easingType }
}
