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
