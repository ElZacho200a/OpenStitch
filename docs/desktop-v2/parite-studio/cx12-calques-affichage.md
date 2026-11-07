# CX12 — Calques et comparaison

Le bouton « Calques et comparaison » en bas à droite ouvre les calques de l’image source
préparée, de la segmentation (ou son aperçu non appliqué), des contours vectoriels et des
liaisons entre objets dans l’ordre effectif de couture. L’opacité s’applique aux quatre calques.
Le mode rideau affiche une partie de l’image source au-dessus de la broderie. Son curseur
est exprimé en fraction de la largeur physique de l’image, centré sur l’origine du document.

Les chemins sont aplatis par la géométrie existante ; les points proviennent du même
instantané de séquence effective que le canevas. CompareController prépare les données
sur TaskManager et ne modifie jamais le projet. CanvasOverlays porte un instantané immuable.
Les nœuds QSG (textures, contours, ordre) sont conservés tant que cet instantané ne change
pas. La caméra, l’opacité et le rideau changent seulement matrices, opacités et rectangle de
clip. Le repli logiciel utilise le même rectangle en millimètres et les mêmes données.

Les essais `StudioCompareTest::realQmlLayersCurtainAndPreview` utilisent une vraie image
numérisée, le shell Main.qml et QTest souris/clavier. Ils couvrent les quatre calques,
le rideau, l’alignement en mm et le compteur de construction avant/après pan/zoom,
l’aperçu de segmentation et les octets DST identiques. Les variantes CTest logiciel et
D3D11 WARP sont inscrites ; leur présence n’est pas une preuve d’exécution réussie.

Validation Debug dédiée : **2/2 CTest**, **6,01 s** (logiciel 2,45 s, D3D11/WARP 3,10 s).
Le test GPU utilise la plateforme Windows native, une fenêtre exposée et l’assertion
explicite `graphicsApi() == Direct3D11`. Le premier test nommé WARP utilisait le repli
logiciel avec la plateforme offscreen : cette fausse preuve a été retirée et corrigée.
Les interactions et invariants sont maintenant exécutés sur les deux chemins.

Suite Studio complète et Release restent à exécuter avant push/PR. La base récente a
modifié le rendu GPU (famille K) ; sa compatibilité doit être vérifiée après rebasage.
