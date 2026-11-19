// SPDX-License-Identifier: Apache-2.0
// Fil du rendu réaliste (RENDER-PBR-01) : sommets du quad de fil inchangés (x, y, u, v).
#version 440
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 vTex;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 color;  // rgb du fil, 0..1
    vec4 params; // relief, brillance, torsion, inutilisé
    float qt_Opacity;
};
out gl_PerVertex { vec4 gl_Position; };
void main() {
    vTex = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * qt_Vertex;
}
