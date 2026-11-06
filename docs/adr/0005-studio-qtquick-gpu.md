# ADR 0005 — OpenStitch Studio : shell Qt Quick et canevas de géométrie GPU

Statut : **accepté pour un prototype expérimental, généralisation conditionnelle** (2026-11-06).
Révise le choix du shell de l'[ADR 0003](0003-desktop-v2-technologie.md) ; conserve les contrats de
l'[ADR 0004](0004-desktop-v2-architecture.md).

## Contexte et preuves

La V2 dispose déjà d'une session documentaire, d'une sélection, d'un registre d'outils et d'un
gestionnaire de tâches sans Qt. Son aperçu Widgets est une image réaliste calculée par
`render_preview` → `effective_sequence` → `stitch_render::render_threads`, présentée par
`PreviewCanvas` (`QGraphicsView`). Le déplacement de cette image n'effectue pas un nouveau rendu
de tous les fils. Les anciennes mesures de services C++ ne constituent donc pas une mesure des
FPS du canevas et ne permettent pas de conclure à la supériorité d'un front.

L'ambition change : travailler directement sur des rails, poignées et couches de broderie, avec
des données géométriques qui restent accessibles au rendu. Un aperçu bitmap reste utile pour le
rendu réaliste et le repli ; il ne doit plus imposer l'architecture du futur canevas interactif.

Cette décision est accompagnée d'un exécutable `openstitch_v2_studio`, d'un parcours `.osp` réel,
de tests des contrats sans GPU et d'un banc comparatif. Le [protocole et ses limites](../desktop-v2/18-studio-benchmarks.md)
distinguent présentation Quick, peinture Widgets et calcul des ressources. **60 FPS et 16,7 ms
sont des objectifs, jamais des résultats déduits du choix de Qt Quick.**

## Décision

1. **Qt Quick / QML est la cible du prochain shell.** QML porte la composition, les états visuels
   et les gestes ; C++ porte les données, commandes, transformations et calculs. Aucun objet QML
   par point, aucun historique ni format de projet supplémentaire.
2. **Conserver les moteurs C++ et les contrôleurs V2.** `DocumentSession` possède toujours le
   projet, la seule pile d'annulation et `SelectionModel`. Le pont QObject expose leurs données
   au shell et livre les résultats de `TaskManager` sur le thread d'interface. CLI, MCP, OSP et
   codecs DST restent sur leurs chemins existants.
3. **Commencer avec le Qt Quick Scene Graph public.** Un `QQuickItem` C++ produit des
   `QSGGeometryNode` groupés et des matériaux de couleur. Les segments sont issus de la même
   séquence effective, conservent leur identité source et leurs couleurs. Le canevas n'appelle
   pas de génération de points pendant un déplacement de caméra.
4. **Séparer trois contrats.** L'instantané de scène est immuable et indépendant de Qt ; la
   caméra convertit coordonnées document/écran et décrit la vue ; l'implémentation Scene Graph
   détient seule les ressources graphiques. Les sélections et autres surimpressions sont des
   données de présentation, pas des modifications du projet.
5. **Ne pas utiliser QRhi privé dans cette première étape.** Le Scene Graph utilise déjà le RHI
   de Qt 6.8. Des matériaux/shaders publics pourront enrichir les fils. Un besoin démontré de
   passes hors écran, textures de simulation ou cartes analytiques justifiera un nouvel adaptateur
   spécialisé ; il ne doit pas faire entrer un handle graphique dans le cœur.

## Threading, cache et cycle de vie

Le travail coûteux capture `DocumentSession::snapshot()` et sa révision. Un résultat annulé ou
dont la révision n'est plus courante n'est pas installé. Le document vivant et les modèles QML
restent sur le thread d'interface. La synchronisation de `updatePaintNode` ne transfère que les
données de présentation déjà calculées ; les nœuds, géométries et matériaux appartiennent au
thread de rendu. Aucun pointeur de nœud n'est manipulé depuis un callback métier ou QML.

Le changement d'instantané invalide les ressources de points ; pan, zoom et redimensionnement
modifient la transformation. Un changement de sélection invalide ses surimpressions seulement.
L'invalidation du Scene Graph détruit ses nœuds et impose leur reconstruction au prochain
`updatePaintNode`, même si l'instantané C++ est inchangé. Les compteurs de diagnostic rendent
vérifiable l'absence de reconstruction de géométrie pendant les gestes de caméra.

## Plateformes, API graphiques et repli

Utiliser le backend par défaut de Qt : normalement D3D11 sous Windows, Metal sous macOS et
OpenGL sous Linux ; Vulkan/OpenGL peuvent être demandés lorsque Qt et le pilote les supportent.
Le diagnostic affiche l'API effectivement obtenue, sans déduire l'accélération de la seule
présence d'un `QQuickWindow`. La taille du canevas et les gestes utilisent des pixels logiques ;
Qt applique le ratio de pixels de l'écran. Les essais HiDPI, écrans multiples et fréquence élevée
restent des validations distinctes à effectuer.

Le canevas choisit un backend CPU explicite lorsque Qt Quick utilise `Software` (option
`--software`). Cet adaptateur peint les segments immuables avec `QPainter` dans une image à la
taille du viewport et au DPR courant, présentée par un `QSGSimpleTextureNode`. Les mouvements
de caméra repeignent cette image ; ils ne régénèrent jamais les points. Ce repli privilégie la
compatibilité, sans présumer une cadence de 60 FPS. Il conserve les mêmes coordonnées et la
même sélection ; il ne fournit pas encore le rendu réaliste complet de `render_preview`.

Le logiciel conserve aussi `openstitch_v2` pendant la transition : son aperçu CPU `render_preview`
et `PreviewCanvas`, avec ses outils complets, restent le chemin de compatibilité fonctionnelle.
Un échec d'initialisation du pilote ne déclenche pas encore automatiquement un redémarrage en
mode logiciel ; relancer explicitement avec `--software` ou dans Widgets. Le retrait de Widgets
exige d'abord le portage de sa parité fonctionnelle et la validation du repli Studio.

Qt Quick reste optionnel pour les builds existants. `OPENSTITCH_BUILD_DESKTOP=OFF` construit
toujours les modèles, la géométrie de scène et les tests sans Qt. Le prototype ne dépend ni de
CUDA ni d'une marque de GPU. Son intégration ne suppose pas de support multiplateforme prouvé
par un seul lancement Windows.

## Compromis et limites assumées

- Le prototype affiche des segments colorés ; ce rendu n'a pas la qualité photographique de
  l'aperçu CPU. Comparer leur initialisation donne le coût de deux produits visuels différents.
- Le premier shell ne possède pas toute l'édition, l'enregistrement, les exports et le compagnon
  du shell Widgets. Aucun retrait de fonctionnalité de l'exécutable existant n'est autorisé par
  cette ADR. La migration est décrite dans [l'architecture Studio](../desktop-v2/17-studio-gpu.md).
- Qt Quick ajoute les dépendances Qml, Quick, QuickControls2 et leurs plugins de déploiement.
  Les nœuds groupés évitent un coût par point côté QML mais n'annulent pas le coût des sommets,
  de la sélection, de la copie des instantanés ou de la génération initiale.
- La soumission d'une frame n'est pas une mesure matérielle de photon à l'écran. Le banc n'a
  pas de compteur portable de mémoire VRAM ou de temps GPU ; il indique ces métriques indisponibles.
- Les outils historiques `QGraphicsItem` nécessiteront un adaptateur de gestes/surimpressions.
  Leur logique géométrique dans `core/` reste réutilisable, leur couche visuelle ne l'est pas.

## Validation et remise en cause

Avant de rendre Studio principal, exiger :

1. Un `.osp` réel chargé sans mutation ; mêmes identifiants, couleurs et coordonnées ; exports
   DST identiques à projet/paramètres égaux ; CLI/MCP et tests existants conservés.
2. Une comparaison Release reproductible sur une même machine, avec 1k/10k/50k/100k points et
   Tentabrode 300 mm ; données brutes, API graphique, DPR, écran, médiane et p95 publiés.
3. Une cible de 60 FPS et p95 local < 16,7 ms sur les scènes représentatives ; aucune
   régénération de points/reconstruction de buffers pendant le déplacement de caméra. Les scènes
   et configurations qui ratent la cible sont documentées, pas exclues du rapport.
4. Le passage de la matrice fonctionnelle (ouvrir, modifier, annuler, enregistrer, exporter,
   compagnon) et d'une recette Windows native/HiDPI ; validation des autres backends annoncés
   avant toute promesse de support.
5. Des tests de transformations, sélection, invalidation et destruction/récréation du Scene Graph.

Réexaminer la décision si le coût du pont ou du Scene Graph empêche ces objectifs après profilage,
si une plateforme cible exige des API privées fragiles, ou si l'accessibilité/déploiement empêche
le remplacement du front Widgets. Comparer alors une autre implémentation du contrat de canevas,
sans réécrire le moteur ni la session.

### Première mesure, portée de l'acceptation expérimentale

Le [rapport du 2026-11-06](../desktop-v2/18-studio-benchmarks.md#résultats-et-critères) et ses
[données brutes](../desktop-v2/benchmarks/2026-11-06/README.md) proviennent d'une exécution native
Release Qt 6.8.3 / Windows 11 25H2 : Ryzen 7 7700X, RTX 5060 Ti réellement sélectionnée par
D3D11, écran 59,997 Hz et DPR 1,5. Les cinq scènes atteignent environ 60 swaps/s ; les buffers
de points sont construits une fois, avec un compteur inchangé après les gestes de caméra.

Le p95 local varie de 16,657 à 16,734 ms : **la cible stricte < 16,7 ms n'est pas uniformément
validée**, notamment sur Tentabrode. Les p95 d'intervalle de trame vont de 16,791 à 16,839 ms.
La première frame Quick arrive en 287 à 466 ms après installation ; la mémoire résidente du
processus Quick est supérieure à celle du cas Widgets dans cette passe. Les mesures Widgets
sont des fins de peinture CPU (p95 1,549 à 2,070 ms), sans mesure de leur présentation à l'écran.
Elles ne démontrent donc aucune supériorité globale d'un front. Temps GPU, mémoire GPU,
fréquences élevées, autres plateformes et répétabilité entre passes restent non mesurés.

Le prototype justifie l'exploration et le maintien de la cible expérimentale ; cette première
mesure ne valide pas le retrait de Widgets, la généralisation multiplateforme ou la parité du
shell. Les critères ci-dessus continuent de s'appliquer.

## Références techniques

- [Exemple Qt 6.8 de géométrie personnalisée](https://doc.qt.io/qt-6.8/qtquick-scenegraph-customgeometry-example.html)
- [Architecture et threading du Scene Graph](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph.html)
- [QSGRendererInterface : API graphique réellement utilisée](https://doc.qt.io/qt-6.8/qsgrendererinterface.html)
