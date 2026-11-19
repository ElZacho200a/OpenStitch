// SPDX-License-Identifier: Apache-2.0
// Fil du rendu réaliste (RENDER-PBR-01). Les coordonnées (u, v) du quad encodent, comme pour
// l'atlas de sprite : u = (cellule d'orientation + abscisse en travers) / 16,
// v = (rangée de torsion + abscisse le long) / 8 (rangée r = r + 1 périodes, pas constant).
// Le shader en tire un éclairage analytique : cylindre (même lumière que la référence CPU),
// spéculaire anisotrope Kajiya-Kay à deux lobes incliné par l'hélice des brins, variation de
// brin déterministe, contact assombri aux bouts. Sortie prémultipliée.
#version 440
layout(location = 0) in vec2 vTex;
layout(location = 1) in float vAcrossScale;
layout(location = 2) in float vHalfPx;
layout(location = 3) in float vShadow;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 color;
    vec4 params;
    vec4 extra;
    float qt_Opacity;
};

const float kBins = 16.0;
const float kClasses = 8.0;
const float kPi = 3.14159265;

float strandHash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float relief = params.x;
    float sheen = params.y;
    float twist = params.z;

    float bin = floor(vTex.x * kBins);
    // [-1, 1] en travers sur le fil ; au-delà sur la marge d'anticrénelage.
    float acrossRaw = (fract(vTex.x * kBins) * 2.0 - 1.0) * vAcrossScale;
    float across = clamp(acrossRaw, -1.0, 1.0);
    float row = floor(vTex.y * kClasses);
    float along = fract(vTex.y * kClasses); // [0, 1] le long
    float periods = row + 1.0;

    float angle = (bin + 0.5) / kBins * 2.0 * kPi;
    vec2 dir = vec2(cos(angle), sin(angle)); // repère écran, Y vers le bas
    vec2 perp = vec2(-dir.y, dir.x);         // convention du moteur CPU

    // Lumière haut-gauche (libs/stitch_render/src/raster.cpp), vue de face.
    vec3 light = normalize(vec3(-0.64 * 0.72, -0.77 * 0.72, 0.70));
    vec3 view = vec3(0.0, 0.0, 1.0);
    float nz = sqrt(max(0.0, 1.0 - across * across));
    vec3 normal = normalize(vec3(perp * across, nz));

    // Diffus cylindrique, même forme que le sprite CPU.
    float diffuse = max(0.0, dot(normal, light));
    float raw = 0.25 + 0.75 * diffuse / light.z;
    float rim = 0.55 + 0.45 * sqrt(nz);
    float lit = (1.0 - relief) + relief * raw * rim;

    // Torsion à pas constant : bandes diagonales des brins.
    float phase = periods * along + 0.5 * across;
    float ply = 1.0 + 0.30 * twist * sin(2.0 * kPi * phase);

    // Variation de brin : quelques fibres plus ou moins claires, fixes sur le fil.
    float fibre = strandHash(vec2(floor((across + 1.0) * 4.0) + bin * 7.0,
                                        floor(along * periods * 3.0) + row * 13.0));
    float grain = 1.0 + 0.08 * relief * (fibre - 0.5);

    // Spéculaire anisotrope : tangente des brins inclinée par l'hélice (alterne avec la torsion).
    float helix = radians(28.0) * twist * sign(sin(2.0 * kPi * phase) + 1e-3);
    vec3 fibreDir = vec3(dir, 0.0);
    vec3 tangent = normalize(fibreDir * cos(helix) + cross(normal, fibreDir) * sin(helix));
    vec3 halfway = normalize(light + view);
    vec3 t1 = normalize(tangent + normal * 0.08);
    vec3 t2 = normalize(tangent - normal * 0.12);
    float th1 = dot(t1, halfway);
    float th2 = dot(t2, halfway);
    float lobe1 = pow(sqrt(max(0.0, 1.0 - th1 * th1)), 90.0);
    float lobe2 = pow(sqrt(max(0.0, 1.0 - th2 * th2)), 18.0);
    float lift = smoothstep(0.0, 0.25, nz);
    vec3 spec = sheen * lift * (vec3(0.65) * lobe1 + color.rgb * 0.35 * lobe2);

    // Silhouette (comme le sprite CPU) et contact fil/tissu assombri aux bouts.
    // Bord : rampe douce (18 % de la demi-largeur) mais jamais plus raide qu'un pixel ; fil plus
    // fin qu'un pixel : couverture proportionnelle à sa largeur.
    float edgePx = max(1.0, 0.18 * vHalfPx);
    float acrossAlpha = clamp((1.0 - abs(acrossRaw)) * vHalfPx / edgePx + 0.5 / edgePx, 0.0, 1.0);
    acrossAlpha *= min(1.0, 2.0 * vHalfPx);

    // Ombre portée (lot C) : silhouette très estompée, noir translucide. Dessinée juste avant son
    // fil, elle assombrit les fils cousus avant lui, pas lui-même.
    if (vShadow > 0.5) {
        float soft = clamp(1.0 - across * across, 0.0, 1.0);
        float endFade = clamp(min(along, 1.0 - along) / 0.14, 0.0, 1.0);
        float shadowAlpha = params.w * soft * (0.35 + 0.65 * endFade) * min(1.0, 2.0 * vHalfPx);
        fragColor = vec4(0.0, 0.0, 0.0, shadowAlpha) * qt_Opacity;
        return;
    }
    float edge = min(along, 1.0 - along);
    float endAlpha = clamp(edge / 0.14, 0.0, 1.0);
    float alpha = acrossAlpha * (0.35 + 0.65 * endAlpha);
    lit *= mix(0.72, 1.0, endAlpha);

    vec3 rgb = clamp(color.rgb * lit * ply * grain + spec, 0.0, 1.0);
    fragColor = vec4(rgb * alpha, alpha) * qt_Opacity;
}
