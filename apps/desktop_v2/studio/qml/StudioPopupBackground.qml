// SPDX-License-Identifier: Apache-2.0
// Fond commun des popups, dialogues et menus : style « popup » du thème (ThemedSurface). Les
// valeurs posées ici sont celles de Neo Studio (panneau, bordure, rayon) ; un appelant peut les
// changer (liste d'une liste déroulante…), le thème les surcharge toujours.
import QtQuick
import "."

ThemedSurface {
    themeStyle: Theme.style("popup", "default", "normal")
    fill: Theme.panel
    border: Theme.border
    borderWidth: Theme.borderWidth
    radius: Theme.radius
    shadowLayers: 0
    animated: false
}
