# Studio — sélection vectorielle et géométrie incrémentale

Exploration du 9 novembre 2026. **Ce document distingue ce qui a été modifié de ce qui demande encore une expérimentation**. Branche dépendante de la PR #305 (invalidations ciblées).

## 1. Couche de sélection vectorielle — optimisation implémentée

### Chemin précédent

`VectorSpaceController::refresh()` → `VectorLayer::build(project, selected)` :
- parcourt toutes les formes, transforme leurs chemins en `QPainterPath` ;
- calcule toute leur emprise, alloue une image de la scène (jusqu'à la limite de 4096 px) ;
- rasterise toutes les formes, puis repeint les contours et poignées des formes sélectionnées ;
- republie une unique image vers Qt Quick, qui doit recréer sa texture.

Ce travail pouvait être fait sur le fil GUI pour une simple sélection, sans changement du document.

### Nouvelle séparation

`VectorLayer::build(project)` produit le **fond immuable + les chemins utilisables pour le picking**, une fois par changement géométrique.

`VectorLayer::selectionOverlay(selected)` :
- cherche les formes sélectionnées parmi les chemins déjà calculés ;
- alloue un **ROI** : union de leurs bounds avec marge de 2 mm, limité à l'image globale ;
- n'affiche que leurs contours et poignées à la même échelle px/mm que le fond.

`GpuCanvas` publie séparément `vectorImage` (stable) et `vectorSelectionImage` (ROI).
`GeometryBackend` a deux sous-nœuds : le premier ne recrée pas de texture quand le second change. Le fallback logiciel compose les deux images. Les drapeaux de visibilité pilotent le groupe complet.

**Complexité attendue :** sélection ordinaire = parcours léger de N métadonnées/chemins + rasterisation des S formes sélectionnées sur un ROI limité (pas de conversion/rasterisation des N formes). En cas de sélection totale, le ROI couvre logiquement l'ensemble : pas de réduction magique du travail.

**Garde de correction :** l'image de fond ne change pas quand la sélection change ; les poignées reviennent si le mode est actif et disparaissent lors de la désélection. Zoom/pan restent hors invalidation. Tests dans `test_studio_space_vector.cpp`.

### Prochaine optimisation facultative

Une table `id → index de VectorShape` et des rectangles spatiaux accéléreraient encore la préparation du ROI pour de très grands N. Il faudra comparer l'overhead mémoire et le coût des clics avant d'introduire un index spatial.

## 2. Scène de broderie — audit des dépendances et mesure

### Chaîne réelle

`StudioController::rebuildScene` → copie immuable du projet → worker `build_studio_scene` :
1. `stitch_generation::effective_sequence(project)` appelle `generate_sequence`, `apply_manual_overrides`, **`apply_predicted_pull`**, puis **`finish_sequence_report`** ;
2. `stitch_render::build_thread_segments` reconstruit les segments réellement cousus, selon les sauts / coupes / sources et l'ordre global ;
3. conversion des segments en sommets GPU (six par segment) et calcul des boîtes englobantes ;
4. synchronisation Qt Quick : reconstruction des batches à chaque nouveau pointeur de scène ; rendu réaliste reconstruisant ses runs/chunks par couleur.

Le moteur utilise déjà un `effectiveSequence` partagé entre scène et simulation : **ne pas introduire une seconde séquence indépendante**.

### Cache local déjà présent — et copie évitable corrigée

Il existe **déjà** un cache LRU des fragments indépendants, `libs/stitch_generation/src/chunk_cache.cpp` (HP-PERF-002, budget 64 Mio). `generate.cpp` recherche par identité, paramètres et géométrie exacte, et le préchauffage est parallèle. Ce cache préserve donc la génération locale entre deux changements d'un seul objet. **Inutile d'en créer un deuxième.**

Avant ce correctif, un hit faisait néanmoins `chunk = *hit` : il recopiait tout le vecteur de points dans un fragment mutable local, avant de recopier ses commandes dans la séquence finale, même quand la jointure n'en modifiait pas l'ordre.

**Optimisation implémentée dans cette PR :** le fragment du cache reste un `shared_ptr<const StitchSequence>` ; l'assemblage lit directement ses commandes. `orient_chunk` fabrique une copie uniquement lorsque la jointure automatique en a réellement besoin. La séquence effective finale et ses passes globales restent rigoureusement inchangées. Un nouveau test vérifie cache froid/chaud, jointure automatique, mutation d'un seul objet et égalité de `effective_sequence` avec le cache désactivé.

### Pourquoi une régénération seulement de l'objet changé serait dangereuse

- `emit_polyline` peut supprimer un Jump quand la fin précédente coïncide avec le premier point. Cette continuité dépend du contexte d'assemblage.
- Les changements de couleur, sauts, verrouillages, arrêts et coupes relient des objets voisins. Les finitions opèrent après la génération.
- Le **tirage prédit** utilise les segments cousus du projet, leur position et leur rang pour évaluer le champ de déformation. Un changement dans un objet peut modifier des points d'autres objets.
- Les imports DST (blocs de couleur) ont un chemin spécifique, non nécessairement indexable comme les objets éditables.
- L'ordre global de couture gouverne le picking (dernier fil cousu), la simulation et l'export exact. Un assemblage « approximativement identique » n'est pas suffisant.

**Décision :** ne pas créer de cache de **séquence effective finale par objet** sans une preuve que les dépendances et passes globales sont inchangées.

### Découpage pour les optimisations suivantes

**A. Génération locale : existant.** Conserver et mesurer le LRU actuel (`chunk_cache_stats`, hits, misses, octets). Le changement de cette PR élimine une copie sur chaque hit. Si nécessaire, étendre prudemment la couverture des types de points encore séquentiels. Le cœur conserve une seule sortie publique `effective_sequence`. Un champ d'invalidation « objets touchés » en complément de `ICommand::affectsEmbroidery` serait utile pour invalider des fragments dérivés du document ; le booléen actuel est trop grossier.

**B. Passes globales :** calcul entier tant que le mode `Predicted` ou les finitions impliquent plusieurs objets. Optimisation par fenêtre d'invalidation uniquement après comparaison stricte contre le pipeline de référence. Lors d'un changement de tissu, de finitions, d'ordre ou de visibilité, élargir automatiquement la fenêtre, voire tout invalider.

**C. Présentation :** sur une nouvelle séquence effective, comparer des plages de commandes adjacentes et leurs couleurs/sources ; réutiliser seulement les batches GPU rigoureusement identiques. La scène doit conserver l'ordre de couture, et le backend doit pouvoir mettre à jour une plage sans désynchroniser la simulation et les autres couches.

A existe déjà et la copie supprimée dans cette PR ne change aucun algorithme. B et C demandent une refonte des structures de cache et des preuves d'équivalence.

### Instrumentation ajoutée

La scène expose trois durées (en millisecondes) dans ses métadonnées, sans effet sur le DST ni la géométrie :
- `effectiveSequenceMs` : génération + retouches + compensation + finitions ;
- `threadSegmentsMs` : conversion séquence → segments cousus ;
- `verticesMs` : conversion segments → triangles et bornes.

Le banc natif `tests/bench/studio_bench.cpp` exporte ces valeurs en JSON sous `scene_effective_sequence_ms`, `scene_thread_segments_ms`, `scene_vertices_ms`, en plus de `scene_build_ms` et du rendu. Ces valeurs permettront de choisir la cible selon les mesures **Release** plutôt que de présumer que la création des triangles coûte plus que la génération des points.

### Critères impératifs avant un cache incrémental du cœur

1. Comparer **octet pour octet** les commandes de `effective_sequence`, DST et segments avec/sans cache, pour création, translation, suppression, modification de paramètres, overrides, undo/redo, réglages de tissu, finitions, ordre de couture, visibilité, imports.
2. Vérifier les sorties multi-pass, sous-couches, satin, spiral/radial, point droit, fondu, predicted pull et trous.
3. Tester 1/100/500/1000 objets, 10k/100k/1M segments, images et masques grands ; latences p50/p95/p99, uploads GPU, mémoire, consommation.
4. Éviter tout cache mutable partagé entre workers. Cache indexé par révision d'entrée, publication atomique immuable ; annulations et projets remplacés doivent rendre toute sortie en cours caduque.
5. Désactiver automatiquement le fast path s'il n'existe pas de preuve fiable d'invalidation. Les optimisations ne doivent pas modifier les sorties machine.

## UX

Pas de spinner ajouté pour cacher un calcul évitable. Un indicateur discret (phase `points`, `segments`, `rendu`) ne devient pertinent que si les mesures montrent une latence visible : traitement annulable et message explicite si son résultat a été rendu périmé.

## Validation

Les tests du refactoring vectoriel et du cache moteur (y compris jointure automatique) ont été ajoutés. La CI et les benchmarks matériels doivent confirmer le fonctionnement du GPU et du backend logiciel ; **aucun gain en ms ou FPS n'est revendiqué sans mesure sur machine**.
