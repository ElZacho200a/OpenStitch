# Traitement d'image

Public : utilisateur avancé, développeur. État : **Implémenté** (non destructif).

## Principe

L'image importée est convertie en une **image de travail RGBA 8 bits**
(`image::Image`). L'original n'est **jamais** modifié : le document conserve
l'original et une **pile d'opérations** (`std::vector<ImageOp>`) rejouée à la
demande par `apply_pipeline`. C'est ce qui rend toutes les retouches annulables.

## Chargement

`load_image` lit le fichier via OpenCV (`cv::imdecode` sur les octets, ce qui
gère les chemins Windows non-ASCII), gère 8 et 16 bits, niveaux de gris + alpha,
et normalise tout en RGBA. `read_image_info` renvoie largeur, hauteur, canaux,
présence d'alpha et format.

## Opérations disponibles

Chaque opération est une alternative du variant `image::ImageOp` :

| Opération | Type | Paramètres | Effet |
|---|---|---|---|
| Recadrage | `CropOp` | x, y, largeur, hauteur (px) | Découpe (borné à l'image) |
| Symétrie | `FlipOp` | horizontal/vertical | Miroir |
| Rotation 90° | `Rotate90Op` | quarts de tour 1–3 | Rotation |
| Niveaux de gris | `GrayscaleOp` | — | Gris (alpha conservé) |
| Luminosité/contraste | `BrightnessContrastOp` | −100..100 | Ajuste (alpha conservé) |
| Débruitage | `MedianDenoiseOp` | force 1–2 (noyau 3/5) | Médian |
| Quantification | `QuantizeOp` | 2–64 couleurs | k-means déterministe (RGB) |
| Lissage bilatéral | `BilateralDenoiseOp` | force 1–3 (noyau 5/7/9 px, tolérance de couleur 20/40/60) | Adoucit aplats et dégradés en gardant les contours ; alpha conservé |
| Aplat des zones | `FlattenOp` | force 1–3 (rayon spatial 4/6/8 px, colorimétrique 8/14/20) | Mean-shift : fond les teintes proches, contours conservés ; réduit îlots et grain avant segmentation ; alpha conservé |
| Contraste local | `ClaheOp` | limite 1–8 | CLAHE sur la luminance CIELAB (tuiles 8×8) : sépare des zones voisines de luminance proche ; alpha conservé |
| Netteté | `SharpenOp` | 0–3 | Masque flou (σ 1,5 px) sur le RGB ; alpha conservé |
| Lignes cohérentes | `CoherentLinesOp` | `sigma_c` 0,3–5 · `sigma_m` 0–10 · `rho` 0,9–1 · `tau` 0,5–1 · ETF : 0–10 passes, rayon 1–15 | Photo/dessin → traits noirs sur fond blanc (FDoG) ; alpha conservé |

Le lissage bilatéral est la 8ᵉ alternative du variant (ajoutée **en fin** :
les index existants ne bougent pas). Dans le `.osp`, `"bilateralDenoise"` avec
`strength` ; une version antérieure refuse un projet qui le contient
(« Opération d'image inconnue »). Il sert surtout avant l'analyse de couleurs
d'une région pour un fondu à deux fils (*Remplissage directionnel*).

**Lignes cohérentes** (`CoherentLinesOp`, 9ᵉ alternative, `"coherentLines"` dans
le `.osp`). Implémentation à partir de la publication *Coherent Line Drawing*
(Kang, Lee & Chui, NPAR 2007), sans code tiers : un champ de flux de tangentes
(ETF) est calculé depuis le gradient puis lissé itérativement ; une différence
de gaussiennes 1D est évaluée dans la direction du gradient puis lissée **le
long du flux**, enfin seuillée (`F < 0` et `1 + tanh(F) < tau`). Le résultat est
binaire (noir/blanc), l'alpha d'origine est conservé et un pixel transparent
n'est jamais tracé. Comparé à un seuillage de gradient, les traits sont continus
et le bruit isolé disparaît. Calcul séquentiel en `float` : déterministe. À
utiliser avant le mode *Contours / Line Art* (menu Image, ou
`openstitch-cli digitize --mode contours --coherent-lines`). Un trait épais
donne ses deux bords ; le squelette du mode Contours les fusionne ensuite.

Note : Le **rééchantillonnage** (redimensionnement de la résolution de travail)
est volontairement absent de cette pile : il changerait le rapport mm/pixel. La
taille physique se fixe à l'import (voir *Modèle physique*).

## Déterminisme

La quantification fixe la graine du générateur aléatoire d'OpenCV
(`cv::setRNGSeed`), donc deux exécutions donnent le même résultat — prérequis des
tests et de la reproductibilité.

## Aperçu et annulation

La luminosité/contraste offre un **aperçu en direct** (le dialogue émet un signal,
la fenêtre recalcule l'affichage sans modifier le document tant que l'utilisateur
n'a pas validé). Toute opération validée passe par une commande d'annulation
(`AppendImageOpCommand`).

## Erreurs possibles

- Recadrage hors de l'image → message, aucune modification.
- Nombre de couleurs hors bornes → refus.
- Fichier illisible/corrompu → erreur `InvalidFile`, pas de crash.

## Implémentation associée

- `libs/image/include/openstitch/image/ops.hpp` — le variant `ImageOp`.
- `libs/image/src/ops.cpp` — `apply_op`, `apply_pipeline` (OpenCV encapsulé).
- `libs/image/src/load.cpp` — `load_image`, `read_image_info`, `encode_png`,
  `decode_image`.
- `libs/commands/include/openstitch/commands/project_commands.hpp` —
  `AppendImageOpCommand`.
- Tests : `tests/unit/image/test_ops.cpp`, `test_image_load.cpp`.
