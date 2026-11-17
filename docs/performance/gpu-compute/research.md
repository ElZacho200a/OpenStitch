# Calcul GPU et alternatives CPU pour OpenStitch : étude documentaire

Statut : recherche documentaire (aucune mesure nouvelle, aucun code modifié).
Date : 2026-11-17. Contexte : branche `perf/gpu-compute-r02`.

## Convention de fiabilité

Chaque affirmation est marquée :

- **[V]** vérifiée pendant cette session sur une source citée en Références ;
- **[C]** connaissance générale de l'auteur (ordre de grandeur couramment admis), **non revérifiée en ligne** : à confirmer par mesure avant toute décision ;
- **[?]** non vérifiée, à traiter comme hypothèse.

Les recherches web de la session étaient limitées (5 requêtes). Ce document privilégie donc l'honnêteté sur l'exhaustivité : les chiffres de latence GPU en particulier sont [C] et doivent être remesurés sur la machine cible (la mesure R01 du dépôt fait foi).

## Calibrage : ce que R01 établit déjà

- Les charges sont de l'ordre de la milliseconde à quelques centaines de ms ; images de 1 à 4 Mpx ; centaines de régions.
- Le prototype D3D11 (ouverture elliptique, distance chamfer) est exact au bit près mais perd : 0,34 ms CPU contre 1,15 ms GPU pour une ouverture ; 26 ms CPU contre 136 ms GPU pour 106 masques de distance ; initialisation du contexte 226 ms. Transfert et attente dominent.
- Tatami FP64 sur GPU : battu par une variante CPU indexée.
- Coûts mono-thread dominants : simulation de tirage du tissu 151 ms, amincissement Zhang-Suen 124 ms, rasterisation CPU de l'aperçu 123 ms, préfiltre médian 100 ms, ponts tatami cachés (A* de visibilité) 154 ms.

Lecture : ces cinq postes sont des algorithmes séquentiels ou à structure de données irrégulière, et un meilleur algorithme CPU (ou le parallélisme CPU déjà disponible via `parallel_for`) a de bien meilleures chances que le GPU. Aucune conclusion de cette étude n'invalide le no-go R01 ; plusieurs le renforcent. Une nuance est signalée en section 2 (GPU résident sans aller-retour).

---

## 1. Comparaison des backends

### 1.1 Grille synthétique

| Backend | Perf potentielle | Portabilité | Intégration Qt / cœur sans Qt | vcpkg | Licence | Déterminisme flottant |
|---|---|---|---|---|---|---|
| CUDA | la meilleure sur NVIDIA | NVIDIA seulement | indépendant de Qt, mais dépendance propriétaire (toolkit ~GB) ; casse `linux-core` sans GPU | non (toolkit hors vcpkg) | EULA propriétaire | contrôlable (`-fmad=false`, `--prec-*`), mais varie selon GPU/driver pour intrinsèques |
| Vulkan compute | haute | NVIDIA/AMD/Intel, Win/Linux | utilisable hors Qt ; lourd (boilerplate) | oui (`vulkan-headers`, `volk`, `vulkan-memory-allocator`) | Apache-2.0 / MIT | pas de garantie IEEE stricte sur les shaders ; `precise`/`NoContraction` possibles |
| OpenGL compute | moyenne | large mais OpenGL en fin de vie, pilotes inégaux | contexte GL requis, mauvais pour un cœur sans Qt | oui (glad…) | variée | pas de garantie |
| Qt QRhi compute | moyenne | D3D11/12, Vulkan, Metal, GL | **API semi-privée** [V], utilisable seulement dans `apps/desktop` ; impossible dans le cœur | inclus dans Qt | LGPL/GPL/commerciale (Qt déjà utilisé) | hérite des shaders (GLSL → SPIR-V → HLSL via qsb) |
| D3D11/D3D12 compute | haute sur Windows | Windows seulement | indépendant de Qt, mais hors cœur Linux | système (Windows SDK) | MS (SDK) | HLSL : FMA contraction possible, pas de stricte IEEE par défaut |
| WebGPU natif (Dawn / wgpu-native) | moyenne-haute | NVIDIA/AMD/Intel, Win/Linux/macOS | indépendant de Qt, API unique | wgpu-native non standard ; Dawn lourd à compiler [C] | Dawn : BSD-3 [C] ; wgpu-native : MIT/Apache-2.0 [C] | WGSL : non strict ; pas de f64 [C] |
| OpenCL | haute | NVIDIA/AMD/Intel via ICD ; support NVIDIA stagnant | indépendant de Qt | oui (`opencl`, headers) | Khronos permissive | meilleur contrôle IEEE parmi les APIs GPU (modes d'arrondi, `-cl-opt-disable`), mais dépend du pilote |
| SYCL (AdaptiveCpp, DPC++) | haute | multi-fournisseurs | écosystème de compilation lourd | non fiable [C] | AdaptiveCpp : BSD-2 [C] | dépend du backend |
| SIMD CPU (Highway, xsimd, std::simd) | très bonne sur kernels réguliers | x86/ARM, MSVC/Linux | **cœur sans Qt, aucun déploiement GPU** | oui (`highway`, `xsimd`) | Highway Apache-2.0 [C] ; xsimd BSD-3 [C] | maîtrisable : mêmes opérations IEEE, pas de fast-math ; entiers = déterminisme total |
| Pool de threads / TBB / work-stealing | bonne | partout | cœur sans Qt ; TBB oui vcpkg | oui | Apache-2.0 (oneTBB) | déterministe si assemblage séquentiel (déjà la règle du projet) |

### 1.2 Points structurants

**Déterminisme.** Le projet impose une sortie identique octet pour octet. Sur GPU, les opérations `+ - * /` et `sqrt` sont IEEE en simple précision dans la plupart des API, mais : (a) la contraction FMA est autorisée par défaut par les compilateurs de shaders, (b) les fonctions transcendantes ont des erreurs tolérées qui varient selon le fournisseur, (c) les réductions atomiques à virgule flottante dépendent de l'ordre d'exécution, (d) le compilateur du pilote peut réordonner. Ces points sont [C] mais largement documentés dans les spécifications Vulkan (précision relaxée, `RelaxedPrecision`) et les guides CUDA (`--fmad`). Conséquence : tout noyau GPU alimentant des coordonnées entières (µm) doit être **purement entier** (comme l'ouverture et le chamfer R01) ou être suivi d'une quantification robuste, et il faut un oracle CPU bit-à-bit dans les tests. La recommandation raisonnable : **GPU uniquement pour des résultats entiers/masques/binaires ou purement visuels** (aperçu), jamais pour des flottants qui entrent dans les coordonnées de points.

**Qt-free core.** Seul `apps/desktop` peut dépendre de Qt. QRhi est semi-privé en 6.8 (nécessite `Qt6::GuiPrivate`, garantie de compatibilité limitée) [V : doc Qt 6.8]. Le seul moyen d'accélérer le cœur par GPU est une interface interne (type `IComputeBackend`) avec une implémentation CPU toujours présente et le GPU optionnel branché dans l'application. Cela double le coût de maintenance (deux chemins à garder bit-exacts).

**Fiabilité / device-lost.** D3D11/12, Vulkan et QRhi signalent la perte de périphérique (`DXGI_ERROR_DEVICE_REMOVED`, `VK_ERROR_DEVICE_LOST`, `QRhi::isDeviceLost()`). Tout chemin GPU exige un repli CPU transparent et bit-exact ; pour des machines modestes (GPU intégré, mémoire partagée), les gains sont encore plus faibles (la mémoire est déjà partagée, donc le transfert est moins coûteux, mais le débit calcul l'est aussi). [C]

**QRhi compute en pratique.** Les fonctions `QRhi::Compute` et `ReadBackNonUniformBuffer` doivent être testées ; un témoignage de forum indique que la lecture retour de plusieurs buffers varie selon le backend (OpenGL : un seul) [V sur le forum, anecdotique]. Le compute QRhi est exposé mais peu utilisé hors graphisme ; peu de retours d'expérience en production.

### 1.3 Verdict backend

1. Le **CPU (SIMD + `parallel_for` + meilleurs algorithmes)** est le chemin à rendement le plus élevé pour le projet : déterministe, portable, sans déploiement, compatible `linux-core`.
2. Si un GPU doit être tenté, la seule option cohérente avec les contraintes est **D3D11 compute (déjà prototypé) ou QRhi côté `apps/desktop`**, pour des tâches à résultat entier ou visuel, avec repli CPU. CUDA est exclu (propriétaire, NVIDIA seul). Vulkan/WebGPU/OpenCL/SYCL : surcoût d'intégration disproportionné pour des charges de l'ordre de la ms.

---

## 2. Latences plancher CPU↔GPU

Les chiffres ci-dessous sont des ordres de grandeur **[C]**, non revérifiés en ligne cette session ; la mesure R01 (226 ms d'initialisation, ~1 ms de coût fixe par masque) est la référence locale et s'y conforme.

| Élément | Ordre de grandeur | Fiabilité |
|---|---|---|
| Soumission d'un dispatch compute (D3D11, driver NVIDIA) | ~5 à 50 µs côté CPU pour l'enregistrement ; 50 à 200 µs de latence de lancement | [C] |
| Synchronisation + `Map` d'un staging buffer (lecture retour) | 100 µs à plusieurs ms (flush du pipeline GPU, attente de fin) | [C] ; cohérent avec R01 (~1 ms par masque) |
| PCIe 4.0 x16, débit théorique | ~32 Go/s ; mesuré typiquement 20 à 25 Go/s ; un laptop (x8 ou PCIe 4 x8) moitié | [C] |
| Image 4 Mpx 8 bits, transfert aller seul à 20 Go/s | ~0,2 ms ; aller-retour ~0,4 ms hors synchronisation | calcul à partir du débit [C] |
| GPU intégré (mémoire partagée) | pas de copie PCIe, mais débit mémoire partagé avec le CPU (~50 à 100 Go/s DDR5) | [C] |
| Création d'un contexte / device | 100 à 300 ms (R01 : 226 ms) | [V : mesure locale R01] |

**Seuil de rentabilité.** Une règle pratique : le GPU gagne si le temps CPU de l'opération dépasse nettement le coût fixe de soumission + synchronisation + transfert, soit grosso modo au-delà de **5 à 10 ms de travail CPU équivalent par lot** et si les données restent résidentes entre plusieurs opérations. Pour des images de 1 à 4 Mpx, les filtres simples (flou gaussien, seuillage, convolution) coûtent déjà 1 à 5 ms en CPU SIMD multi-thread ; un GPU y gagne seulement s'il enchaîne plusieurs étapes sans relire les pixels. [C, cohérent avec R01]

**Nuance qui pourrait nuancer R01 (sans le contredire).** R01 a mesuré des transferts *synchrones par masque*. Le coût était dominé par la synchronisation, pas par la bande passante. Un pipeline **résident** (image uploadée une fois, N étapes chaînées, lecture finale unique) amortirait ces coûts. Mais OpenStitch alterne GPU et logique irrégulière (régions, polygones, A*), ce qui reconduit des synchronisations ; le gain réaliste ne dépasse pas une poignée de ms sur 100 à 500 ms de pipeline. Non prometteur sauf pour l'aperçu, déjà sur GPU.

---

## 3. Algorithmes

### 3.1 Étiquetage en composantes connexes (CCL)

- **YACCLAB** (Grana, Bolelli, Allegretti) est le banc de référence ; il compare des algorithmes CPU et GPU. Les meilleurs CPU sont à base de blocs / arbres de décision : **BBDT**, **Spaghetti** (Bolelli et al., TIP 2021, DOI 10.1109/TIP.2021.3069845 [?] : DOI à vérifier), **BMRS/Block-based union-find**. [C pour les noms ; non revérifié]
- GPU : **Block-based Union Find (BUF)** et **Block-based Komura Equivalence (BKE)** : Allegretti, Bolelli, Grana, IEEE TPDS 31(2):423-438, 2020, DOI 10.1109/TPDS.2019.2934683 [V]. Annoncés plus rapides que Playne et Hawick (2018, "A New Algorithm for Parallel Connected-Component Labelling on GPUs", IEEE TPDS 29(6), DOI 10.1109/TPDS.2018.2592899 [?]) mais surtout sur images grandes.
- OpenCV 4.12 propose `connectedComponents` avec les algorithmes **Spaghetti, BBDT, SAUF** [C] ; l'implémentation dans OpenCV vient directement de ces travaux. Sans backend parallèle OpenCV tourne mono-thread.

**Classification OpenStitch :** CCL GPU = *non pertinent* (étiquette de 1-4 Mpx : quelques ms en CPU ; la lecture retour annule le gain). Vérifier que le chemin actuel utilise bien Spaghetti (`CCL_SPAGHETTI`) et non un algorithme naïf = *très prometteur à coût quasi nul* si ce n'est pas le cas [?]. Parallélisation CPU par bandes puis fusion d'étiquettes : *potentiellement intéressant* mais complexe.

### 3.2 Transformée de distance euclidienne exacte

- **Felzenszwalb & Huttenlocher**, "Distance Transforms of Sampled Functions", Theory of Computing 8(19):415-428, 2012, DOI 10.4086/toc.2012.v008a019 [C pour le DOI] : exacte, linéaire, séparable (deux passes 1D) ; parallélisable trivialement par lignes puis colonnes, déterministe (arithmétique entière possible pour le carré des distances).
- **Meijster, Roerdink, Hesselink** (2000), "A general algorithm for computing distance transforms in linear time", Mathematical Morphology and its Applications to Image and Signal Processing, Springer, pp. 331-340 [C]. Équivalent, très employé.
- **PBA** (Cao, Tang, Mohamed, Tan), I3D 2010, pp. 83-90 [V : page projet NUS] : exact GPU, linéaire. Pour 2D/3D grands volumes ; DOI non confirmé [?].
- **Jump flooding** (Rong & Tan, I3D 2006) : approximatif (erreurs), donc **contre-indiqué** pour le déterminisme et la précision [C].
- OpenCV `distanceTransform` avec `DIST_L2` et masque 3x3/5x5 est **approximatif** (chamfer) [C, cohérent avec "DIST_L2/3 chamfer" de R01] ; `DIST_MASK_PRECISE` donne le résultat exact mais plus lent.

**Classification :** transformée exacte Felzenszwalb/Meijster en entiers (carré des distances), multi-thread par lignes/colonnes : *potentiellement intéressant* (remplace 106 masques de distance en CPU ; vérifier d'abord le coût réel dans le pipeline : R01 donne 26 ms pour 106 masques, donc au mieux un gain de quelques ms). PBA/JFA GPU : *contre-productif*.

### 3.3 Squelettisation / amincissement

Poste majeur de R01 : Zhang-Suen 124 ms.

- **Zhang & Suen** (1984), CACM 27(3):236-239, DOI 10.1145/357994.358023 [C]. Algorithme itératif par sous-itérations ; coût proportionnel (nombre d'itérations × surface). Les amincissements itératifs sont lents car à chaque passe on balaie toute l'image.
- **Guo & Hall** (1989), CACM 32(3):359-373 [C] : variante plus fine, deux sous-itérations, utilisée dans `cv::ximgproc::thinning` (opencv_contrib) [C].
- **Amincissement par liste de contour / file de pixels actifs** : ne traiter que les pixels frontière à chaque itération (liste de candidats), ce qui réduit le coût d'un facteur proportionnel à la largeur des traits ; gain CPU sûr et déterministe. [C : technique classique ; pas de source précise]
- **Squelette fondé sur la transformée de distance** (axe médian, crêtes locales du DT, ou "medial axis transform" par Voronoï) : une passe EDT + extraction de crêtes, indépendante du nombre d'itérations ; plus rapide et plus précise (squelette centré), mais la topologie doit être stabilisée par élagage. Le projet a déjà `medial_axis` pour le satin.
- **Amincissement parallèle GPU** : Zhang-Suen se parallélise naturellement par pixel (chaque sous-itération = un dispatch). Beaucoup de passes (dizaines à centaines) → autant de synchronisations ; en shader résident c'est possible (ping-pong de textures), mais le déterminisme est conservé car les règles sont purement booléennes. Gain réel sur images grandes seulement [C].

**Classification :** amincissement CPU par file active / parallélisation par bandes avec barrière entre sous-itérations (déterministe) : *très prometteur* (124 ms mono-thread → plausible 10 à 30 ms ; **non mesuré, [?]**). Squelette via EDT exacte + crêtes : *potentiellement intéressant* (changement de sortie : plus risqué, golden à régénérer). GPU : *non pertinent*.

### 3.4 Filtre médian

Poste R01 : préfiltre médian 100 ms.

- **Huang, Yang, Tang** (1979), IEEE TASSP 27(1):13-18 [C] : histogramme glissant, O(r) par pixel.
- **Perreault & Hébert**, "Median Filtering in Constant Time", IEEE TIP 16(9):2389-2394, 2007, DOI 10.1109/TIP.2007.902329 [V] : histogrammes par colonne, O(1) par pixel, s'appuie sur SIMD pour réduire la constante ; extension au noyau quasi circulaire. Seulement pour pixels 8 bits.
- **Weiss** (2006), "Fast median and bilateral filtering", ACM TOG 25(3):519-526, DOI 10.1145/1141911.1141918 [C].
- **OpenCV** `medianBlur` implémente déjà Perreault-Hébert (O(1)) pour les grands noyaux 8 bits, avec SIMD ; pour 3x3 et 5x5, réseau de tri SIMD [C]. Sans IPP, OpenCV utilise sa propre version universelle (SSE/AVX via intrinsèques universels).
- Pour un filtre médian *sur mesure* (noyau circulaire, ou en couleurs/espace Lab), O(1) ne s'applique pas directement ; une alternative est de médianiser sur la plage 8 bits par canal.

**Classification :** vérifier que le préfiltre appelle bien `cv::medianBlur` 8 bits avec le bon noyau ; sinon implémenter Perreault-Hébert / utiliser l'implémentation OpenCV : *très prometteur* (100 ms → probablement <20 ms ; **[?] non mesuré**). Parallélisation par bandes : déterministe, *potentiellement intéressant*. GPU : *non pertinent*.

### 3.5 Quantification de couleurs / k-means

- k-means (Lloyd) : coût N×K par itération ; 4 Mpx × 8-16 couleurs × 10 itérations = quelques centaines de millions d'opérations : 100 à 300 ms mono-thread [C]. Parallélisable par bandes avec accumulateurs entiers par thread puis fusion séquentielle (déterministe).
- Accélérations CPU : **sous-échantillonnage** (entraîner sur 100-200 k pixels puis affecter tout), histogramme de couleurs (ex. 5 bits par canal : 32 k cases) et k-means sur histogramme pondéré : on passe de 4 M à ≤ 32 k points ; **Wu** (1992), "Color quantization by dynamic programming and principal analysis", ACM TOG 11(4):348-372 [C] : quantification déterministe sans itération.
- **k-means GPU** : Zechner & Granitzer 2009 (« Accelerating k-means on the graphics processor via CUDA ») [?], **Kmeans par FAISS / cuML** : bibliothèques hors périmètre (CUDA, licences/lourdeur).

**Classification :** histogramme pondéré / sous-échantillonnage : *très prometteur* si la quantification figure dans le pipeline ; GPU : *non pertinent*.

### 3.6 Contours (marching squares) et vectorisation

- **Marching squares** est local par cellule ; la partie parallélisable (classification des cellules) est triviale, mais le chaînage des segments en polylignes est séquentiel (ou par union-find). Le suivi de contour de **Suzuki & Abe** (1985, CVGIP 30(1):32-46 [C]) est ce qu'utilise `cv::findContours`.
- Parallélisme CPU possible par bandes avec jonction des polylignes aux frontières de bande ; coût de complexité élevé pour un gain limité (quelques ms).
- Génération GPU de contours : sans intérêt (sortie de taille variable, lecture retour).

**Classification :** *non pertinent* (sauf profil montrant un coût >30 ms).

### 3.7 Découpe / décalage de polygones

- Clipper2 (Boost, Angus Johnson) est utilisé ; il est séquentiel. Pas de bibliothèque GPU éprouvée équivalente et déterministe pour la découpe de polygones [?]. La littérature GPU sur les opérations booléennes (ex. Greiner-Hormann parallèle, Wu et al.) est académique, avec précision flottante [?].
- Parallélisme **au niveau des tâches** : chaque région / objet fait son offset indépendamment ; `parallel_for` sur les régions avec assemblage séquentiel. C'est le seul parallélisme sûr et déjà dans le style du projet.
- Clipper2 offre une variante en coordonnées entières (`Paths64`) : déterministe par construction (ce que le projet utilise vu `Micrometers`) [C].

**Classification :** GPU : *non pertinent / contre-productif* (flottant, irrégulier). Parallélisme par région : *potentiellement intéressant* si pas déjà fait.

### 3.8 Graphe de visibilité et plus court chemin (ponts tatami, 154 ms)

- **Grille spatiale / BVH** pour accélérer les tests segment-obstacle : réduit O(N) à O(log N) ou O(1) par requête ; déterministe ; classique [C].
- **Visibility graph** : Lee (1978), Welzl (1985) en O(n²), Ghosh & Mount (1991) O(E + n log n) [C]. L'algorithme de balayage angulaire évite O(n³).
- **A\* avec heuristique cohérente** + tas binaire ; **Theta\*** (Daniel, Nash, Koenig, Felner, JAIR 39:533-579, 2010 [C]) donne des chemins any-angle sans graphe de visibilité complet. Mais **Theta\* n'est pas optimal** : change la sortie (golden à régénérer).
- **Jump Point Search** (Harabor & Grastien, AAAI 2011 [C]) sur grille uniforme : accélère A* sans changer l'optimalité (grilles à coût uniforme, 8 voisins). Pertinent si le routage est sur grille, sinon non.
- Réutilisation : **cache de visibilité** par paire de polygones, ou **graphe construit une fois par objet** puis requêtes multiples (Dijkstra multi-cibles) ; le gain vient de l'amortissement, pas du GPU.
- Parallélisme : N ponts indépendants → `parallel_for` + assemblage séquentiel si les ponts ne dépendent pas les uns des autres (sinon dépendance d'ordre).

**Classification :** grille spatiale/BVH + précalcul du graphe : *très prometteur* (154 ms est un poste dominant sur tentabrode ; **gain [?] non mesuré**). GPU : *contre-productif* (A\* irrégulier, déjà R01).

### 3.9 Point dans polygone par lot

- Tests de parité O(n) par requête ; accélérés par grille de cellules ou par rasterisation de masque : une région rasterisée une fois donne un test O(1) (avec précision d'un pixel, à employer avec une marge ou un test exact de repli pour les cas frontière).
- GPU : pas pertinent aux tailles du projet (R01 tatami : un CPU indexé bat le GPU).

**Classification :** *non pertinent* sur GPU ; indexation CPU : *potentiellement intéressant*.

### 3.10 SIMD CPU

- **Highway** (Google, Apache-2.0 [C]) : dispatch dynamique multi-cible (SSE4/AVX2/AVX-512/NEON), contribué par vcpkg [C]. **xsimd** (QuantStack, BSD-3 [C]). `std::simd` : adopté dans C++26 ; **non disponible en C++23 MSVC** [C].
- Déterminisme : le résultat SIMD sur entiers est identique à la version scalaire ; sur flottants, il faut éviter les réductions horizontales dont l'ordre diffère entre largeurs (AVX2 vs AVX-512) ; on impose un ordre fixe (somme par groupes de 8 puis séquentielle) ou on reste en entiers. FMA : à désactiver (`/fp:precise` ne suffit pas toujours ; MSVC contracte FMA avec `/arch:AVX2` sous `/fp:fast`, pas sous `/fp:precise`) [C].
- Risque de dispatch : un dispatch dynamique selon le CPU peut produire des résultats différents entre machines s'il y a du flottant → imposer les mêmes résultats par test de non-régression sur chaque cible SIMD.

**Classification :** *potentiellement intéressant* pour les noyaux entiers réguliers (médian, seuillage, accumulations d'histogramme, rasterisation) ; coût de maintenance modéré.

---

## 4. Spécifique à la broderie

Ce qui est établi (peu de littérature publique, tout non vérifié en ligne cette session) :

- **Génération de remplissage / points :** littérature principalement orientée outils (Ink/Stitch, exclu car GPL ; Embroidermodder ; Wilcom/Hatch propriétaires). Des travaux académiques : **Kaldor et al.**, "Simulating Knitted Cloth at the Yarn Level" (SIGGRAPH 2008, DOI 10.1145/1360612.1360664) pour tricot [C] ; **Zhou, Wang et al.**, « Image-based embroidery » / « Stitch-based image abstraction » (Pattern recognition, IEEE TVCG) sur la génération de broderie par fils [?] ; **Igarashi, Jacobs** (2008?) [?]. Aucune source précise validée : je ne peux affirmer aucun chiffre de performance.
- **Simulation physique de tirage du fil/tissu :** les méthodes naturelles sont les masses-ressorts et **Position-Based Dynamics** (Müller, Heidelberger, Hennix, Ratcliff, 2007, J. Vis. Commun. Image Represent. 18(2):109-118, DOI 10.1016/j.jvcir.2007.01.005 [C]) et la variante unifiée sur GPU de **Macklin, Müller, Chentanez, Kim**, "Unified Particle Physics for Real-Time Applications", ACM TOG 33(4):104, 2014, DOI 10.1145/2601097.2601152 [V : indexation hgpu / SIGGRAPH]. Ces travaux sont conçus pour 10⁴-10⁶ particules à 60 Hz ; **rien ne démontre une exigence de déterminisme bit-à-bit** (solveurs de Gauss-Seidel/Jacobi parallèles avec ordre non fixe).
- Le coût mesuré de la simulation de tirage (151 ms) vient probablement du nombre d'itérations et de la structure (voisinages, contraintes par point), pas d'un manque de FLOPS. Pour un solveur à itérations de Jacobi avec sommations en ordre fixe, une **version CPU SIMD/multi-thread** est déterministe et simple ; la version GPU poserait le problème de déterminisme flottant et de déploiement.
- **Idées d'accélération CPU spécifiques à la simulation de tirage** [hypothèses, **[?]**] : (a) simulation hiérarchique / multi-grille (résolution grossière puis raffinement) ; (b) ne simuler que les objets modifiés (cache à clé exacte, déjà préconisé dans `openstitch-performance`) ; (c) découpage en tuiles ou en régions indépendantes si le tissu n'est couplé que localement ; (d) tolérance d'arrêt déterministe (nombre d'itérations fixe plutôt que seuil flottant).

**Classification :** simulation de tirage sur GPU : *contre-productif* (déterminisme flottant + déploiement + peu de gain vu l'échelle). Optimisations algorithmiques CPU de la simulation : *très prometteur* (c'est le 1er poste de R01 : 151 ms).

---

## 5. Classification finale

Légende : TP = très prometteur ; PI = potentiellement intéressant ; NP = non pertinent ; CP = contre-productif.

| Piste | Classification | Justification courte | Source / fiabilité |
|---|---|---|---|
| Backend CUDA | CP | propriétaire, NVIDIA seul, casse `linux-core` | [C] |
| Vulkan / WebGPU / OpenCL / SYCL pour le cœur | CP | surcoût d'intégration énorme pour des tâches de quelques ms | [C] |
| QRhi compute (dans `apps/desktop`) | NP pour le calcul, PI pour l'aperçu | API semi-privée, hors cœur | [V] doc Qt |
| D3D11 compute (calculs du pipeline) | CP | R01 : GPU plus lent | R01 |
| GPU résident enchaîné (nuance) | NP | irrégularité + synchronisations | R01 + [C] |
| CCL GPU (BUF/BKE) | NP | image 1-4 Mpx : quelques ms en CPU | [V] TPDS DOI |
| CCL CPU : vérifier Spaghetti/BBDT | TP (si pas déjà utilisé) | gain quasi gratuit | [C] |
| EDT exacte entière (Felzenszwalb/Meijster) multi-thread | PI | remplace le chamfer approximatif ; gain borné par R01 (26 ms) | [C] |
| PBA / jump flooding | CP | GPU ; JFA approximatif | [V] PBA ; [C] JFA |
| Zhang-Suen par file active + bandes | TP | cible 124 ms ; déterministe | [C] ; gain [?] |
| Squelette par EDT + crêtes | PI | gain structurel, change la sortie | [C] |
| Amincissement GPU | NP | trop de synchronisations | [C] |
| Médian O(1) (Perreault-Hébert / `medianBlur`) | TP | cible 100 ms | [V] TIP DOI 10.1109/TIP.2007.902329 |
| Quantification via histogramme / sous-échantillon | TP (si présent) | N de 4 M à 32 k | [C] |
| k-means GPU | NP | | [?] |
| Marching squares / contours parallèles | NP | | [C] |
| Offset/clipping parallèle par région | PI | tâches indépendantes | [C] |
| Clipping GPU | CP | flottant, irrégulier | [?] |
| Grille spatiale/BVH + précalcul du graphe de visibilité | TP | cible 154 ms (ponts tatami) | [C] ; gain [?] |
| Theta\* / JPS | PI | change la sortie ; JPS seulement sur grille | [C] |
| Point-in-polygon indexé (CPU) | PI | | [C] |
| Simulation de tirage : optimisation algorithmique CPU | TP | cible 151 ms (poste n°1) | [?] |
| Simulation de tirage sur GPU (PBD/Flex) | CP | déterminisme + déploiement | [V] Macklin et al. |
| Rasterisation de l'aperçu CPU (123 ms) | PI | déplacer vers le chemin GPU existant (Qt Quick) ; sortie visuelle uniquement, déterminisme non requis | R01 |
| SIMD (Highway/xsimd) sur noyaux entiers | PI | ajoute une dépendance ; gain à mesurer | [C] |

---

## Références

Vérifiées en ligne pendant la session :

1. S. Allegretti, F. Bolelli, C. Grana, "Optimized Block-Based Algorithms to Label Connected Components on GPUs", IEEE TPDS 31(2):423-438, 2020, DOI 10.1109/TPDS.2019.2934683. Dépôt univ. Modène : https://iris.unimore.it/bitstream/11380/1179616/1/2018_TPDS_Optimized_Block_Based_Algorithms_to_Label_Connected_Components_on_GPUs.pdf
2. T.-T. Cao, K. Tang, A. Mohamed, T.-S. Tan, "Parallel Banding Algorithm to Compute Exact Distance Transform with the GPU", I3D 2010, pp. 83-90. https://www.comp.nus.edu.sg/~tants/pba.html (DOI non confirmé).
3. S. Perreault, P. Hébert, "Median Filtering in Constant Time", IEEE TIP 16(9):2389-2394, 2007, DOI 10.1109/TIP.2007.902329 (la graphie 902326 est fausse). https://uio.no/studier/emner/matnat/ifi/IN2070/v21/undervisningsmateriale/litteratur/perreault-hebert-median.pdf
4. Qt 6.8, `QRhiReadbackDescription` (en-tête `rhi/qrhi.h`, cible `Qt6::GuiPrivate`, compatibilité limitée) : https://doc.qt.io/qt-6.8/qrhireadbackdescription.html ; `QRhiComputePipeline` : https://doc.qt.io/qt-6.8/qrhicomputepipeline.html ; retour d'expérience compute/readback (anecdotique) : https://forum.qt.io/post/811877
5. M. Macklin, M. Müller, N. Chentanez, T.-Y. Kim, "Unified Particle Physics for Real-Time Applications", ACM TOG 33(4):104, 2014, DOI 10.1145/2601097.2601152 (références bibliographiques confirmées par indexation ; contenu cloth non relu). https://matthias-research.github.io/pages/publications/flex.pdf

Citées de mémoire, **non vérifiées** (à confirmer avant toute citation externe) :

6. Playne, Hawick, "A New Algorithm for Parallel Connected-Component Labelling on GPUs", IEEE TPDS 29(6), 2018.
7. Bolelli et al., "Spaghetti Labeling", IEEE TIP 2021 ; BBDT (Grana et al., TIP 2010).
8. Felzenszwalb, Huttenlocher, "Distance Transforms of Sampled Functions", Theory of Computing 8:415-428, 2012.
9. Meijster, Roerdink, Hesselink, 2000 (distance transform en temps linéaire).
10. Rong, Tan, "Jump Flooding in GPU with Applications to Voronoi Diagram and Distance Transform", I3D 2006.
11. Zhang, Suen, CACM 27(3):236-239, 1984 ; Guo, Hall, CACM 32(3):359-373, 1989.
12. Huang, Yang, Tang, 1979 ; Weiss, ACM TOG 25(3), 2006.
13. Wu, "Color quantization by dynamic programming and principal analysis", ACM TOG 11(4), 1992.
14. Suzuki, Abe, CVGIP 30(1):32-46, 1985.
15. Daniel, Nash, Koenig, Felner, "Theta\*: Any-Angle Path Planning on Grids", JAIR 39, 2010 ; Harabor, Grastien, "Online Graph Pruning for Pathfinding on Grid Maps" (JPS), AAAI 2011.
16. Müller, Heidelberger, Hennix, Ratcliff, "Position Based Dynamics", J. Vis. Commun. Image R. 18(2), 2007.
17. Kaldor, James, Marschner, "Simulating Knitted Cloth at the Yarn Level", SIGGRAPH 2008.
18. Licences et propriétés de Highway (Apache-2.0), xsimd (BSD-3), Dawn (BSD-3), wgpu-native (MIT/Apache-2.0), AdaptiveCpp (BSD-2), oneTBB (Apache-2.0), disponibilité vcpkg.
19. Tous les ordres de grandeur de latence et de débit de la section 2 (dispatch, Map, PCIe) et du déterminisme FMA/MSVC de la section 3.10.
20. Mesures internes du dépôt : R01 (docs/performance, banc de mesure du projet).

Non trouvé / sans source : littérature académique précise sur l'accélération GPU de la génération de points de broderie ou de la simulation de tirage du tissu en broderie ; aucun chiffre ne doit être déduit de ce document sur ces points.

## Addendum R02 : ce que les mesures ont tranché

Cette étude documentaire a été rédigée avant les expériences R02. Les mesures qui suivent
remplacent ses ordres de grandeur marqués [C] :

- **Transferts GPU.** Sur RTX 5060 Ti de bureau, l'envoi et la lecture retour des 106 masques
  de distance de tentabrode coûtent 73 ms au p50, contre 18,7 ms pour tout le calcul CPU.
  L'initialisation du contexte prend 176 à 221 ms
  ([résultats](benchmark-results.md#gpu-d3d11-sur-gpu-de-bureau)). Le seuil « 5 à 10 ms de
  travail par lot » de la section 2 est donc optimiste pour des transferts par masque.
- **Simulation du tirage.** Conclusion confirmée : réécriture CPU exacte et parallélisme
  déterministe, 95 → 14 ms (marine), sans GPU.
- **Ponts cachés.** Le coût n'était pas la recherche de chemin (A*, section 3.8), mais la
  rastérisation de la grille de recouvrement, quadratique par point. Une meilleure structure
  spatiale n'était pas nécessaire : il suffisait de ne tester que l'intervalle utile de chaque
  rangée.
- **Squelette (section 3.3).** La « file de pixels actifs » proposée existe déjà
  (`skeleton.cpp`, bord actif et table de 256 voisinages).
