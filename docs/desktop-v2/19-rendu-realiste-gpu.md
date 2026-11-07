# 19 - Rendu realiste accelere par le GPU (famille K)

Vue seule : le calque realiste ne modifie jamais le document ni les points. Le moteur CPU
(`render_preview`) reste la reference (export, repli `--software`, comparaison).

## Conception
- `realistic_scene.*` (sans Qt) : segments de la scene immuable -> runs de meme couleur dans l'ordre
  de couture -> tranches de 5 000 segments -> quadrilateres texcoordes (6 sommets/segment, bouts
  etendus d'une demi-largeur). Ombre = memes quads decales vers le bas-droite, inseres AVANT le fil.
- `thread_textures.*` (sans Qt) : sprite de fil par couleur (atlas de 16 orientations, eclairage
  cylindrique, reflet pow 28, torsion, bords et bouts adoucis en alpha, memes constantes que
  `libs/stitch_render/src/raster.cpp`), sprite d'ombre, tuile de tissage ; cache par couleur.
- `canvas_renderer.cpp` : `RealisticLayer` (QSGOpacityNode) + `ThreadMaterialProvider` (couture pour
  de futurs shaders ; aujourd'hui QSGTextureMaterial / QSGOpaqueTextureMaterial, mipmaps, filtrage
  lineaire). Reconstruit seulement si scene ou style change ; la bascule masque le calque.
- Selection du chemin : backend RHI -> GPU ; backend logiciel -> raster CPU de reference.
- UI : bouton Realiste (barre), section dans le panneau Vue, Ctrl+Shift+T, indicateur d'etat.

## Pas de shaders personnalises
Ni Qt local ni la CI n'ont qsb/Qt6ShaderTools : materiaux Qt standard + textures generees.

## Limites
Ombre entre fils d'un meme run absente ; torsion etiree avec la longueur du segment ; bouts
carres adoucis (pas de demi-disque exact) ; un run par changement de couleur (alternance
pathologique = beaucoup de noeuds) ; simulation : tranche du curseur redessinee en prefixe.

## Mesures
Test WARP (D3D11 logiciel, Debug) : 1 construction par revision, 0 au pan/zoom et a la bascule,
+1 au changement de reglage de fil, +1 au changement de document. Aire d'encre GPU/CPU = 0,36
(fil fin, seuil strict), couleur moyenne a < 70 niveaux. Pas de mesure FPS Release (banc
`openstitch-studio-bench --realistic` ajoute, non execute faute de temps de build Release).
Captures : `captures/rendu-realiste-gpu.png` (fenetre WARP 720x480, tissu uni + deux fils croises
fins, couleurs framboise/menthe, scene de test minuscule) et `rendu-realiste-cpu.png` (reference
CPU 300x300 de la meme scene) ; ce sont des sorties de test, pas des captures de l'application.
