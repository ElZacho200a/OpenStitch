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
Ombre entre fils d'un meme run absente ; torsion a pas constant mais quantifiee (1 a 8 periodes) ; bouts
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

## RENDER-PBR-01 - note d'architecture (19/11/2026)

### Fait (lot B, premier pas)
Torsion a pas physique constant : l'atlas de sprite empile `kTwistClasses` = 8 rangees, la rangee
c porte c + 1 periodes ; `twistClass(longueur du quad, largeur)` choisit la rangee la plus proche
de longueur / pas (pas = 2 largeurs de fil). Un point long n'etire plus sa torsion, un point court
ne la comprime plus. Cout : sprite 8x plus haut (384 x 256 px RGBA par couleur, ~390 Ko), meme
nombre de sommets, aucun shader. Demi-texel d'ecart en v pour ne pas lire la rangee voisine.
Test : `twistKeepsAConstantPhysicalPitch`. Comparatif visuel photo : non fait (lot A).

### Bloquant pour le PBR complet
Module `qtshadertools` installe (aqtinstall : `python -m aqt install-qt windows desktop 6.8.3
win64_msvc2022_64 -m qtshadertools`) et declare dans les workflows CI et release
(`modules: qtshadertools`). Etape suivante : brancher sur `ThreadMaterialProvider` un materiau de fil :
- sommets enrichis (x, y, axe du fil, abscisse en mm, largeur) au lieu de (x, y, u, v) ;
- fragment : normale de cylindre analytique, speculaire anisotrope (Kajiya-Kay, deux lobes
  decales pour la torsion des brins), torsion a pas exact en mm, bruit de brin deterministe
  (hachage de l'indice de segment), assombrissement de contact aux bouts ;
- l'ordre de couture reste l'ordre des noeuds ; repli : materiaux actuels (aucun chemin perdu).

### Lot C (ombres, tissu) - pistes
Occlusion entre fils d'un meme run : passe de hauteur (rendu des fils dans une texture R16 de
hauteur, puis ombrage par difference de hauteur) ; tissu : normal map de tissage au lieu d'une
tuile de luminance. A mesurer sur 10k / 100k segments en Release (`openstitch-studio-bench`).

### Lot D - ray tracing : decision provisoire NO-GO temps reel, GO conditionnel hors ecran
- Temps reel RTX : Qt RHI n'expose ni BLAS/TLAS ni ray queries ; une integration native
  (D3D12 DXR / Vulkan RT) contournerait le RHI, exclurait le repli logiciel et une partie du parc.
  Le gain visible vient d'abord du materiau (B) et de l'occlusion (C), pas du lancer de rayons.
- Hors ecran (export HD / vignette « photo ») : export de la sequence effective en courbes
  (fils = tubes de rayon largeur/2 suivant les points) vers un moteur de rendu progressif externe
  (ex. Blender Cycles, Apache-2.0 compatible via processus separe, aucun lien de code). POC a faire
  sur un echantillon reduit apres le lot A, decision finale apres comparaison au PBR.
