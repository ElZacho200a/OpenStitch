# 17 — OpenStitch Studio : fondations Qt Quick / GPU

État au 2026-11-07 : prototype expérimental distinct, destiné à remplacer progressivement le
shell Widgets. Décision : [ADR 0005](../adr/0005-studio-qtquick-gpu.md). Mesures et reproduction :
[banc Studio](18-studio-benchmarks.md).

## Audit préalable et périmètre conservé

| Zone existante | Réutilisation / migration |
|---|---|
| `DocumentSession` | Conserver le propriétaire du `Project`, l'historique, les révisions, `snapshot`, le pont live et les deux entrées `execute`/`applyBatch`. Un QObject est un adaptateur, pas une seconde session. |
| `SelectionModel` | Source de vérité commune à l'arbre, l'inspecteur et au canevas. Identifiants C++ 64 bits ; les identifiants exposés au JavaScript sont des chaînes pour éviter une perte au-delà de 2^53. |
| `TaskManager` | Calculs sur instantanés, annulation, progression et livraison via `pump` sur le thread d'interface. Les résultats périmés sont rejetés. |
| `ToolRegistry`, modèles d'inspecteur, catalogues et lots `core/` | Conserver les déclarations et opérations. Porter les gestes et les contrôles, sans recopier leur logique dans QML. |
| `PreviewCanvas` | `QGraphicsView`, pixmap d'aperçu, gestes, poignées et surimpressions Qt Widgets : adaptateur existant conservé pour le repli ; sa présentation est à remplacer. |
| `ShellWindow` et panneaux `ui/` | Widgets, `QAction`, dialogues, menus, événements et abonnements sont encore couplés à Widgets. Migrer famille par famille vers un shell QML ; extraire un service C++ partagé dès qu'un calcul est encore dans un handler. |
| `render_preview` | Service pur : séquence effective, rendu réaliste CPU, PNG, repère et index de sélection. Conservé pour Widgets et les aperçus réalistes. Le repli Studio déjà présent peint des segments plats ; un adaptateur réaliste ultérieur pourra réutiliser ce service. |
| `stitch_render::render_threads` | Raster déterministe, indépendant de Qt ; aucune réécriture pour la bascule. `ThreadSegment` est aussi un contrat de présentation réutilisable. |
| `stitch_generation::effective_sequence` | Seule entrée vers les points, avec retouches et finitions. Aucun appel de génération pendant pan/zoom. |
| Build / tests / CI | `desktop_v2_core` et les tests métier fonctionnent sans Qt (`linux-core`). Widgets reste construit avec sa cible existante ; Studio ajoute des modules Quick optionnels. |

Les fichiers des PR ouvertes ont été examinés, puis revérifiés pendant la mission ; leurs
branches ne sont pas modifiées par ce prototype. Les descriptions initiales ne suffisent pas
à décrire leurs ajouts actuels :

| Source examinée | Services et adaptations déjà présents dans la PR | Réemploi prévu dans Studio |
|---|---|---|
| [#279 — document/production](https://github.com/ElZacho200a/OpenStitch/pull/279/files) | `core/display_filters`, `core/simulation_nav`, leurs tests, compléments de palettes de fils et listes CMake | Brancher les filtres de présentation et la navigation de simulation sur le canevas ; reprendre les services de document, d'export et de fils sans créer une seconde logique. |
| [#280 — édition et ergonomie directe](https://github.com/ElZacho200a/OpenStitch/pull/280/files) | `core/context_commands`, `core/radial_menu`, `core/scrub`, `core/spool_rail`, tests et adaptations Widgets `floating_action_bar`, `radial_menu_widget`, `scrub_filter`, `spool_rail_widget` ; catalogues/nœuds/tissu et docs 15/16 | Réutiliser ces services Qt-free pour les futures barres contextuelles, menus radiaux, réglages par glissement et rail de bobines. Porter leur présentation et leurs gestes dans Quick ; conserver les opérations et tests existants. |
| [#236 — desktop historique](https://github.com/ElZacho200a/OpenStitch/pull/236/files) | Découpage du desktop historique | Conserver ses corrections ; les nouveaux fichiers Studio sont distincts. |

Ces fonctions d'ergonomie de #280 existent donc déjà dans ses fichiers ; **leur adaptation au
shell Quick de cette PR reste à faire**. Ce prototype n'annonce ni leur absence du projet ni
leur parité dans son propre shell. Les correctifs métier utiles doivent rester communs aux fronts.

Points de conflit à traiter lors de l'intégration : cette PR, #279 et #280 ajoutent des sources
dans `apps/desktop_v2/CMakeLists.txt` et des tests dans `tests/unit/desktop_v2/CMakeLists.txt`.
Conserver l'union des sources et des tests, ainsi que les ajouts de #279/#280 aux catalogues et
aux services, plutôt que choisir un côté. #280 et cette PR ajoutent aussi des lignes au journal
et à la roadmap : conserver les deux historiques. Cet audit ne fusionne ni ne réécrit ces PR.

Les nouveaux documents utilisent les numéros 17 et 18 pour préserver les documents 15 et 16
réservés dans #280. La migration ne modifie pas ces PR ni leurs branches.

## Frontières et flux

```text
QML : shell / composants / tokens / espaces / liste et inspection
                  │ intentions et propriétés, jamais de Project mutable
                  ▼
StudioController (QObject) ─── DocumentSession / SelectionModel / TaskManager
                  │ snapshot + révision, calcul hors thread d'interface
                  ▼
build_studio_scene ─── effective_sequence ─── moteur C++ existant
                  │ shared_ptr<const StudioSceneSnapshot> (sans Qt)
                  ▼
GpuCanvas (QQuickItem) ── StudioCamera ── sélection / gestes
                  │ updatePaintNode, thread de rendu
                  ▼
QSGTransformNode ── QSGGeometryNode groupés ── matériau couleur ── RHI Qt
```

`StudioSceneSnapshot` garde le projet source immuable, sa révision, les segments et leurs
identifiants complets, les sommets, le cadre et les emprises d'objets. La conversion du document
(µm entiers, Y vers le haut) vers l'affichage (mm, Y vers le bas) intervient à cette frontière.
Elle n'altère aucune coordonnée persistée. L'ordre de couture et les coupures de continuité
restent ceux des segments effectifs ; aucune ligne ne doit traverser un saut ou une coupe.

`StudioCamera` travaille en pixels logiques et fournit l'inverse des transformations. Le zoom
sous le curseur conserve le point document sous ce curseur ; le pan ne change que l'offset. Le
clic est converti en coordonnées de scène avant le test de proximité ; le résultat sélectionne
une cible dans le `SelectionModel` existant. Changer d'espace ne remplace pas le document.

Le backend possède les nœuds et les buffers sur le thread Scene Graph. Il reconstruit les
sommets de points lors d'un nouvel instantané ou d'une recréation du contexte ; le déplacement de
caméra met à jour la matrice et les cadres sélectionnés gardent leur épaisseur en pixels. Ces
petites surimpressions peuvent être reconstruites sans reconstruire les points. La sélection
possède un domaine d'invalidation distinct. Les données de
diagnostic reviennent sur le thread d'interface ; QML ne lit pas un pointeur de nœud de rendu.

## Parcours disponible

Ouvrir un `.osp`, afficher ses fils colorés, naviguer avec le pan et le zoom ancré, ajuster,
sélectionner un objet depuis la vue ou la liste et consulter son identité/diagnostic. Les espaces
**Préparer → Numériser → Contrôler → Produire** sont des contextes du même document. Le shell
n'expose que les contrôles réellement reliés ; ce parcours de visualisation ne remplace pas les
outils d'édition et de production Widgets.

Dans cette première version, changer d'espace adapte le texte d'accompagnement et l'indication
du contexte ; les outils disponibles restent la sélection, le pan et l'ajustement. Le filtrage
des familles d'outils du `ToolRegistry` et les éditeurs propres à chaque espace restent à porter.

La palette graphite et l'accent framboise sont définis dans les tokens QML ; les boutons et
sections se composent à partir des mêmes composants. Les coordonnées, les identifiants et les
opérations métier restent dans C++. Un point n'est jamais un QObject ou un composant QML.

## Construire, lancer, diagnostiquer

```powershell
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
cmake --preset msvc -DOPENSTITCH_BUILD_STUDIO=ON
cmake --build --preset msvc-release --target openstitch_v2_studio openstitch_v2
.\build\msvc\apps\desktop_v2\studio\Release\openstitch_v2_studio.exe motif.osp
```

Qt 6.8 avec Qml, Quick et QuickControls2 est requis pour Studio. L'absence de ces modules
laisse les cibles existantes disponibles ; la configuration signale que Studio n'est pas construit.
Le build headless continue avec `OPENSTITCH_BUILD_DESKTOP=OFF`.
`OPENSTITCH_BUILD_STUDIO=OFF` désactive uniquement le prototype. Les tests ciblés sont :

```powershell
cmake --build --preset msvc-release --target test_desktop_v2_core test_desktop_v2_studio_shell
ctest --preset msvc-release -R 'studio' --output-on-failure
```

Les cas `studio ...` de `test_desktop_v2_core` utilisent Catch2 sans Qt ;
`test_desktop_v2_studio_shell` utilise QTest et le repli logiciel/offscreen pour les contrats
d'intégration. Ce test n'est pas une preuve de performances GPU. Le protocole natif est dans
[le banc Studio](18-studio-benchmarks.md).

Le diagnostic donne l'API obtenue et le nombre de constructions de géométrie. Pour comparer
un autre backend supporté par Qt/pilote, définir par exemple `QSG_RHI_BACKEND=opengl` avant le
lancement, puis vérifier le diagnostic. Ce réglage n'est pas une garantie de compatibilité.
Si le rendu accéléré échoue, relancer avec `--software` : le canevas choisit l'adaptateur
`QPainter` → image à la taille du viewport/DPR → `QSGSimpleTextureNode`. Ce backend repeint les
segments lors du pan/zoom, sans régénérer la séquence, et garde la caméra/sélection commune. Sa
cadence est à mesurer séparément. Pour l'édition et la production complètes, ouvrir le même
`.osp` dans `openstitch_v2`, conservé pendant la transition. Le choix logiciel reste explicite ;
le programme ne redémarre pas automatiquement après un échec du pilote.

Le mode `--screenshot chemin.png` capture la fenêtre Qt puis quitte après le chargement du
projet. `--select-object 52` établit la sélection initiale via le contrôleur existant, pour une
recette reproductible. Captures natives réellement obtenues sur Tentabrode :

- [Rendu D3D11, objet 52 sélectionné](captures/studio-tentabrode-300mm-d3d11.png) : fenêtre
  1440 × 900 pixels logiques, image 2160 × 1350 pixels, DPR 1,5. Le journal Qt confirme la
  RTX 5060 Ti et **4 échantillons MSAA** ; ce n'est pas une maquette générée.
- [Repli CPU Qt Quick, même sélection](captures/studio-tentabrode-300mm-software.png) : rendu
  `QPainter` des mêmes segments et mêmes coordonnées ; aucune mesure FPS n'est tirée de l'image.

Leur viewport diffère de celui du banc (1280 × 800 pixels logiques) ; les captures ne servent
pas de source à ses durées de trame. [Métadonnées et journal Qt](captures/studio-captures.json)
et [extrait natif D3D11](captures/studio-native-d3d11.txt) permettent de vérifier leur provenance.
Après génération du projet décrite dans le
[protocole](18-studio-benchmarks.md), reproduire depuis la racine du dépôt :

```powershell
$env:QSG_INFO = '1'
.\build\msvc\apps\desktop_v2\studio\Release\openstitch_v2_studio.exe build\tentabrode-300mm.osp --select-object 52 --screenshot build\studio-native.png
.\build\msvc\apps\desktop_v2\studio\Release\openstitch_v2_studio.exe build\tentabrode-300mm.osp --select-object 52 --software --screenshot build\studio-software.png
```

Ces applications GUI peuvent se lancer de manière asynchrone depuis PowerShell ; attendre leur
sortie avant de vérifier le fichier. Le pipeline CI produit aussi une capture Qt réelle en mode
logiciel/offscreen ; son artifact s'appelle `openstitch-studio-screenshots-windows`. Capturer une
image peut synchroniser le GPU et doit rester séparé du benchmark.

## Points d'extension nécessaires

- Rails satin, Bézier, guides et poignées : modèle géométrique existant dans `core/`, gestes C++
  convertis par la caméra, données de surimpression immuables. Seul le relâchement valide une commande.
- Simulation : réutiliser `core/simulation_nav` et `core/display_filters` de #279 pour l'état et
  les commandes de navigation ; ajouter leur présentation Quick et le découpage des segments
  rendus. Cartes de densité et tirage : couches de rendu avec clé de révision et préparation en
  tâche ; matériaux/textures backend sans ressource Qt dans les moteurs.
- Comparaison et aperçu MCP : deux instantanés (`previewBatch` sur copie), une seule session
  réelle ; l'approbation applique la commande existante. Aucune seconde pile d'annulation.
- Barres contextuelles et menus radiaux : consommer `core/context_commands` et `core/radial_menu`
  déjà ajoutés par #280. Les adaptations Widgets `floating_action_bar` et `radial_menu_widget`
  donnent les parcours à porter, sans recopier leur logique en JavaScript.
- Réglages par glissement et rail de bobines : reprendre `core/scrub` et `core/spool_rail` de
  #280 ; remplacer `scrub_filter` et `spool_rail_widget` par des gestes/composants Quick qui
  utilisent les mêmes opérations, palettes et tests. Aucune nouvelle couche métier parallèle.
- Fil réaliste : matériau/shader adapté aux sommets de scène, sans nouveau générateur de points.
  Mesurer le coût du nouveau rendu et conserver l'aperçu CPU comme référence visuelle.

Ces adaptations Quick ne sont pas implémentées par ce prototype. Les services déjà présents
dans #279/#280 constituent leur point de départ ; ils ne sont pas à reconcevoir.

## Remplacement progressif du shell Widgets

| Étape | Livrable | Condition de sortie vérifiable |
|---|---|---|
| 1 — Fondations (cette PR) | Cible expérimentale, `.osp` réel, scène GPU, caméra/sélection, tests, banc et diagnostic | Compilation et parcours réel ; invariants document/DST ; résultats du banc avec limites explicites |
| 2 — Document et production | Ouverture/enregistrement/annulation, exports et paramètres migrés ; réutilisation de #279 | Aller-retour OSP et DST identiques ; mêmes commandes et erreurs dans les deux fronts |
| 3 — Manipulation directe | Formes/nœuds/rails, surimpressions GPU ; services existants `context_commands`/`radial_menu`/`scrub`/`spool_rail` de #280 présentés dans Quick | Parcours et sélection parité ; commandes/tests communs ; aucune génération pendant un geste ; p95 mesuré sur scène représentative |
| 4 — Contrôle et compagnon | Analyse, simulation réutilisant `simulation_nav`/`display_filters` de #279, previewBatch et approbation branchés sur la session commune | Les scénarios du compagnon utilisent le même historique et rejettent les résultats périmés ; filtres/navigation conformes aux services existants |
| 5 — Bascule et retrait | Studio devient principal, repli CPU du canevas stabilisé, packaging/accessibilité validés | Matrice fonctionnelle et tests verts, recette native/HiDPI, backends annoncés validés ; retrait du front Widgets après cette preuve |

Deux exécutables constituent une transition bornée par ces critères. Ajouter durablement une
nouvelle fonctionnalité dans deux logiques applicatives distinctes serait un échec de migration ;
la logique doit être partagée et seule sa présentation doit varier pendant la transition.
