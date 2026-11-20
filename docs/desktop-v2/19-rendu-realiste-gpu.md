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

## Shader de fil (opt-in)
Avec le module qtshadertools, `shaders/thread.{vert,frag}` (BATCHABLE) remplacent le materiau de
texture du fil quand `OPENSTITCH_THREAD_SHADER=1`. Sans le module, ou sans la variable : materiaux
Qt standard + textures generees (chemin par defaut inchange).

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

### Lot D - ray tracing : decision NO-GO temps reel dans le Studio, GO hors ecran
- Temps reel RTX dans le canevas : Qt RHI n'expose ni BLAS/TLAS ni ray queries ; une integration
  native (D3D12 DXR / Vulkan RT) contournerait le RHI et perdrait le repli logiciel. Le shader de
  fil et les ombres entrelacees couvrent l'usage interactif.
- Hors ecran : prototype livre (voir ci-dessous), concluant.
### Shader de fil - MVP (19/11/2026)
`ThreadShaderMaterial` (canvas_renderer.cpp), active par `OPENSTITCH_THREAD_SHADER=1`, chemin RHI
seulement. Meme geometrie et memes (u, v) que l'atlas : le fragment retrouve la cellule
d'orientation, l'abscisse en travers, la rangee de torsion et l'abscisse le long, puis calcule
cylindre + speculaire anisotrope Kajiya-Kay a deux lobes (tangente inclinee par l'helice des
brins), torsion a pas constant, variation de brin deterministe, contact assombri aux bouts.
Ombre et tissu : materiaux standard. Uniformes std140 : matrice, couleur, (relief, brillance,
torsion), opacite.
Verification : variante CTest `test_desktop_v2_studio_realistic_gpu_shader` (WARP, poste local) ;
le test echoue desormais sur tout avertissement « shader » ou « pipeline » (sans `BATCHABLE`, le
pipeline echouait silencieusement et le test passait quand meme).
Mesure (scene de test minuscule, Debug, WARP) : aire d'encre 12,3 mm2 contre 20,0 pour les textures
et 34,7 pour la reference CPU ; couleurs plus vives. Anticrenelage (ajoute ensuite) : le vertex shader elargit le quad d'un pixel de chaque cote selon
le zoom courant (vue en pixels + largeur du fil en uniformes ; u1 rentre de 0,1 % pour reconnaitre
le cote), le fragment fond le bord sur au moins un pixel et module la couverture des fils plus fins
qu'un pixel : encre 12,3 -> 15,8 mm2. Limites : orientation quantifiee a 16
directions (heritee de l'encodage u), pas de comparaison photo, pas de mesure Release.

### Lot C - ombres entre fils (shader, 19/11/2026)
Avec le shader de fil, `writeInterleavedShadowThreadQuads` ecrit pour chaque segment son ombre
portee puis son fil dans un meme flux (12 sommets/segment, un seul noeud par tranche) : l'ombre du
segment i assombrit les segments 0..i-1 du meme run, jamais le segment i. Les quads d'ombre gardent
l'encodage (u, v) du fil decale de 2 en u ; le shader y reconnait le mode ombre (silhouette
estompee, intensite 0,65 x reglage, comme le sprite CPU). Chemin par textures inchange (ombre du
run entier avant ses fils). Test : `interleavedShadowsPrecedeEachThread`. Non fait : occlusion par
passe de hauteur, tissu en normal map, mesures Release.

### Lot D - prototype de ray tracing hors ecran (19/11/2026)
Chaine : `openstitch-cli render projet.osp ref.png --scene-json scene.json` ecrit, en plus du PNG
de reference CPU, la scene de brins (sequence effective, ordre de couture, mm, couleur, largeur ;
format neutre `openstitch-thread-scene` v1). `tools/raytrace/render_mitsuba.py scene.json out.png
[--spp N] [--tilt deg]` la rend avec Mitsuba 3 (BSD-3, `pip install --user mitsuba`, outil externe,
aucun lien de code) : chaque brin = courbe lineaire de rayon largeur/2, enfoncee aux points
d'aiguille, empilee selon l'ordre de couture ; tissu diffus ; soleil haut-gauche + ciel. Ombres,
occlusion et reflets entre fils sont calcules. Variante choisie automatiquement : cuda (OptiX, RTX
materiel) > llvm > scalar.
Mesure (motif bateau numerise depuis tests/fixtures/ai-benchmark/marine-synth-01/source.png,
6 921 brins, 8 couleurs, poste local, variante cuda_ad_rgb) : 908 x 688 px a 64 spp en 0,4 s ;
1816 x 1377 px vue inclinee 35 degres en 0,8 s. Captures : `captures/raytrace-bateau-dessus.jpg`,
`raytrace-bateau-incline.jpg`, reference CPU `raytrace-bateau-reference-cpu.jpg`.
Limites : pas de tissu tisse ni de relief de sous-couche (fils sur un plan), materiau « principled »
generique (pas de BSDF de fibre), pas d'integration Studio (export a lancer en CLI), pas de
comparaison photo reelle, variante CPU (llvm) non disponible ici (LLVM absent) donc non mesuree.
Studio (ajoute ensuite) : Fichier > Exporter > « Rendu photo, lancer de rayons (PNG)… »
(`FileController::exportPhotoRender`) : scene ecrite par `export_thread_scene` (desktop_v2/core,
format commun `stitch_render::thread_scene_json`, partage avec la CLI), puis le script lance par
QProcess dans la tache de fond (annulable), vue inclinee 35 degres, 64 spp ; l'image s'ouvre a la
fin. Interpreteur : `OPENSTITCH_PYTHON` (defaut `python`) ; script : `OPENSTITCH_RAYTRACE_SCRIPT`,
sinon `tools/raytrace/` a cote de l'executable, sinon les sources. Messages clairs si Python ou
Mitsuba manquent. Test QTest `photoRenderRunsTheExternalEngineOnTheThreadScene` (moteur factice).
Non fait : copie du script au deploiement (windeployqt), reglages (inclinaison, qualite) dans l'IHM.

### Rendu photo - reglages (19/11/2026)
Fenetre Fichier > Exporter > Rendu photo : inclinaison (0-70 degres), qualite (1-4096
echantillons/pixel), resolution (2-40 px/mm), largeur du fil (0,15-1 mm), type de tissu (uni,
toile, serge, jersey, feutre, eponge : tuiles procedurales deterministes, couleur + relief par
bump map), couleur du tissu (preglages ou selecteur libre), couleur du fond (tissu presente sur
un fond, bordure 12 %). Options bornees par le controleur, liste fermee pour le type. Script :
`--fabric-type`, `--background`, `--border`, `--width-px` (3840 = 4K).
Mesure : Impossibke.osp, 131 829 brins, 3840 x 2676 px, 256 spp, vue de dessus, coton noir en
6,2 s (cuda_ad_rgb) ; en 4K UHD 3840 x 2160 (`--height-px 2160`) en 5,0 s : capture
`captures/raytrace-impossibke-4k-coton-noir.png`.

8K (7680 x 4320, 256 spp) : le script decoupe en passes de graines fixes au-dela de ~2^30
echantillons par appel (limite Mitsuba) ; Impossibke sur toile kaki en 19,2 s :
`captures/raytrace-impossibke-8k-coton-kaki.png` (52 Mo).
