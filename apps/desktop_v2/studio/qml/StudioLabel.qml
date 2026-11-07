// SPDX-License-Identifier: Apache-2.0
// Label with the Studio type scale. `role` picks the size/weight from Theme; the color is a
// token (`tone`), never a literal.
import QtQuick
import "."

Text {
    id: label
    // "caption" | "small" | "body" | "heading"
    property string role: "small"
    // "foreground" | "muted" | "faint" | "accent" | "error" | "warning" | "success"
    property string tone: role === "heading" || role === "body" ? "foreground" : "muted"

    color: !enabled ? Theme.faint
         : tone === "foreground" ? Theme.foreground
         : tone === "faint" ? Theme.faint
         : tone === "accent" ? Theme.accent
         : tone === "error" ? Theme.error
         : tone === "warning" ? Theme.warning
         : tone === "success" ? Theme.success
         : Theme.muted
    font.family: Theme.fontFamily
    font.pixelSize: role === "caption" ? Theme.captionSize
                  : role === "body" ? Theme.bodySize
                  : role === "heading" ? Theme.headingSize
                  : Theme.smallSize
    font.weight: role === "heading" ? Font.DemiBold : Font.Normal
    wrapMode: Text.WordWrap
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
