// SPDX-License-Identifier: Apache-2.0
// Fil du rendu réaliste (RENDER-PBR-01) : sommets du quad de fil inchangés (x, y, u, v).
// Anticrénelage : le quad est élargi d'un pixel de chaque côté (selon le zoom courant) ; le
// fragment reçoit l'échelle de la coordonnée en travers et la demi-largeur du fil en pixels.
#version 440
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 vTex;
layout(location = 1) out float vAcrossScale; // across' = across * échelle (|across'| > 1 : dehors)
layout(location = 2) out float vHalfPx;      // demi-largeur du fil en pixels
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 color;  // rgb du fil, 0..1
    vec4 params; // relief, brillance, torsion, inutilisé
    vec4 extra;  // largeur et hauteur de la vue (px), largeur du fil (mm), inutilisé
    float qt_Opacity;
};
out gl_PerVertex { vec4 gl_Position; };

const float kBins = 16.0;
const float kPi = 3.14159265;

void main() {
    vTex = qt_MultiTexCoord0;
    float cell = qt_MultiTexCoord0.x * kBins;
    float bin = floor(cell);
    float side = fract(cell) > 0.5 ? 1.0 : -1.0; // u0 : côté -perp ; u1 (légèrement rentré) : +perp
    float angle = (bin + 0.5) / kBins * 2.0 * kPi;
    vec2 perp = vec2(-sin(angle), cos(angle));
    vec2 perpNdc = (qt_Matrix * vec4(perp, 0.0, 0.0)).xy;
    float pxPerMm = max(1e-4, length(perpNdc * extra.xy * 0.5));
    float halfMm = max(1e-4, extra.z * 0.5);
    float padMm = 1.0 / pxPerMm;
    vAcrossScale = (halfMm + padMm) / halfMm;
    vHalfPx = halfMm * pxPerMm;
    gl_Position = qt_Matrix * vec4(qt_Vertex.xy + perp * side * padMm, qt_Vertex.zw);
}
