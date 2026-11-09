# Audit des cascades de recalcul — OpenStitch Studio

Date : 9 novembre 2026. Périmètre : `apps/desktop_v2/studio`, ses quatre espaces (Image, Segmentation, Vectorisation, Points), le scene graph Qt Quick et les contrôleurs métiers. Audit **statique** des chemins d'invalidation et de leur coût algorithmique ; pas de profilage instrumenté ni de mesure FPS/mémoire dans cet environnement.

## Principe de correction

Une action utilisateur doit invalider **la plus petite donnée nécessaire**. Distinction entre :
- événement de présentation (statut, chargement, outil, sélection, zoom, grille, curseur de simulation) ;
- modification du document et de sa révision ;
- mutation des pixels de l'image, de la carte des régions, des vecteurs ou des points ;
- nouveau snapshot immuable du calque à envoyer au moteur graphique.

L'objectif est d'empêcher qu'un simple changement d'état déclenche indirectement une copie du projet, une régénération de points, une rasterisation de formes ou un nouvel upload de textures.

## Trajets audités et décisions

| Action / source | Cascade avant correction | Décision et résultat attendu | Statut |
|---|---|---|---|
| Simulation : curseur, grille, guides, marqueurs | `ViewController::publish()` → nouvel `CanvasOverlays` → `overlayRevision` → `GeometryBackend::synchronize` → **destruction + upload GPU** du calque vectoriel et de toutes les images d'analyse, même inchangées | Nœuds séparés (vecteurs / images d'analyse / primitives écran) ; vecteurs invalidés par identité de snapshot, autres images par `QImage::cacheKey` et emprise ; la caméra ne change que les matrices | **Corrigé** |
| Afficher / masquer les vecteurs | Reconstruction du groupe de textures commun, alors que les pixels sont identiques ; le fallback logiciel pouvait ignorer le drapeau `vectors` | Opacité du nœud vectoriel + respect du drapeau par le fallback CPU | **Corrigé** |
| Publication de la même image vectorielle, même région ou même snapshot d'overlays | `GpuCanvas` demandait parfois un rendu et avançait une révision sans changement réel | Gardes d'identité sur les snapshots immuables | **Corrigé** |
| Sélectionner une forme | `StudioController::selectionChanged` + `changed` appelaient deux fois `VectorSpaceController::refresh` ; le second passage recalculait l'état | Un seul abonnement, une seule reconstruction du calque de sélection | **Corrigé pour la double notification** |
| Ouvrir / revenir à l'espace vectoriel | `attach()` pouvait publier le calque une fois dans `refresh`, puis une deuxième fois dans `attach` | Actualiser hors canevas puis publier une fois ; même instance de canevas = aucune réinitialisation | **Corrigé** |
| Changer de sélection, message de progression, statut sans révision | `PointEditController::refresh` pouvait réexécuter `stitchPoints` ; `FadeController` relisait son axe et `SatinController` recalculait le diagnostic | Gardes `document.revision + selection` (et `active` pour Points) ; les véritables changements continuent à actualiser l'outil | **Corrigé** |
| Clic / mutation dans l'outil de numérisation | `DigitizeGestures::rebuildStatic` écoutait le signal Studio général **et** les 3 adaptateurs de points/fondu/satin ; jusqu'à quatre reconstructions d'overlay par événement, même outil inactif | Écouter l'adaptateur correspondant au mode actif, reconstruire au changement de mode | **Corrigé** |
| Sélection / statut → bandeau des étapes | `WorkspaceController::invalidate` invalidait `computeStageStatus` ; lecture suivante pouvait recopier le projet via `session.snapshot()` | Base calculée par révision du document ; forçage explicite sur remplacement du document et changements d'état de segmentation | **Corrigé** |
| Sélection / statut → espace Image | `ImageSpaceController::refresh` émettait `changed` et rebouclait sur la pile sans modification des opérations | Notification seulement quand révision change | **Corrigé** |
| Images `before`/`after` déjà identiques | `publishLayer` redimensionnait à nouveau les grandes images et incrementait `dataRevision`, pouvant reconstruire des textures | Comparaison des clés QImage + échelle ; re-publier le snapshot cached quand on attache un autre canevas | **Corrigé** |
| Changement de segmentation | Les contours et textures de sélection sont gérés par `SegmentationSpaceController` avec coalescence `QTimer`, `mapRevision`, fingerprint et ROI de surbrillance | Conserver cette invalidation ciblée ; benchmark sur grands masques à suivre | Préexistant, vérifié statiquement |
| Modification effective de broderie | `StudioController::rebuildScene()` annule et reconstruit le snapshot de scène entier en tâche de fond | Fonctionnel et asynchrone, mais coûteux sur petits changements ; ne pas modifier le moteur sans benchmark/diff de sorties | **Chantier ultérieur** |
| Changement de sélection vectorielle | `VectorLayer::build` recrée un raster de toutes les formes pour surligner la sélection | Séparer fond raster immuable et couche de sélection légère (chemins/nœuds locaux) pour éviter O(total formes) par clic | **Chantier ultérieur** |
| Invalidation d'image source côté contrôleur | `ImageController::refreshNow` reconstruit encore la liste des opérations quand aucun pixel ne change | Ne rafraîchir que les noms effectivement modifiés ; tenir compte de la sémantique des signaux QML | **Chantier ultérieur** |

## Contrats et non-régressions

- Aucune modification de `effective_sequence`, de la génération de points, des géométries brodées, du format OSP ou du DST.
- Une vraie mutation de document, une nouvelle sélection, un changement de mode d'outil ou une nouvelle image doit encore être visible.
- Zoom et déplacement ne reconstruisent pas la géométrie. La simulation ne doit pas réuploader les textures inchangées.
- Tests ajoutés dans `test_studio_space_vector` (révision d'overlay / publication / sélection), `test_studio_space_image` (images identiques et statut), `test_studio_workspaces` (invalidation par révision), `test_studio_digitize` (points/fondu/satin et poignées).

La suite correspondante est incluse dans les tests Qt du desktop Studio. Validation manuelle à compléter sur Windows/D3D11 ou D3D12 **et** avec le backend logiciel, notamment les permutations de couches, la simulation, les analyses de densité, le fondu et les outils de points. Ce rapport ne revendique ni CI verte ni gain mesuré avant résultats de tests / profilage.

## Plan de mesures complémentaire

Tracer les délais p50/p95/p99 sur événements : clic sélection, glisser nœud (60 Hz), changement de paramètre, sélection multiple, pan/zoom, changement de couche, changement d'espace, curseur simulation, appliquer/annuler, importer/segmenter/vectoriser/numériser, sauvegarder/exporter. Pour chaque événement, compter : appels de contrôleurs, opérations métier, copies de snapshot, uploads GPU, temps du fil GUI, allocations mémoire et durée du rendu.

Jeux de référence : documents à 1/100/500/1 000 formes, images 1K/4K/8K, petites/grandes segmentations, motifs à très nombreux segments et plusieurs milliers de nœuds. Comparer Release avant/après sur **la même machine et le même fichier**, GPU et fallback logiciel ; afficher les compteurs seulement en mode diagnostics.

### UX / retour utilisateur

Pas de « spinner » ajouté pour cacher un traitement évitable. Réserver les messages de progression aux traitements réellement coûteux (import, segmentation, revectorisation, reconstruction importante, export) : nom de la tâche, étape, annulation possible, et résultat périmé ignoré, sans modale bloquante. Décision UI à prendre séparément après mesures de latence.

### Référence

`docs/desktop-v2/10-performances.md` contient un banc historique Release/Debug de novembre 2026 (gestes, séquence, aperçu) mais **pas** les mesures de cette PR, et signale explicitement les angles morts : FPS, sélection massive et mémoire.
