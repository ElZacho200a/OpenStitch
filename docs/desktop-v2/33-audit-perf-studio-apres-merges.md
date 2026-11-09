# Audit de performance Studio après les merges de #303, #304, #305 et #306

Date : 9 novembre 2026. Base : `main` au commit `245300f84bc618f8d99f452d0a61557b37e2a736`.

## Vérification de l'existant

- **#305 est fusionnée dans `main`** : domaines d'invalidation scène/texture indépendants, gardes des contrôleurs Points/Fondu/Satin, scène partagée pour la simulation.
- **#306 est également intégrée**, via la branche de #305 : image vectorielle de base distincte du ROI de sélection, pas de copie du fragment LRU en cas de hit, mesures `scene_effective_sequence_ms`, `scene_thread_segments_ms`, `scene_vertices_ms`.
- **#303** (fermeture Ignorer) est sans rapport direct avec le chemin de rendu.
- **#304** permet de sélectionner/déplacer les nœuds depuis l'outil Sélection et fournit les poignées dans `ToolController::rebuild` / `GpuCanvas::toolOverlay`.
- **#307** n'était pas fusionnée au début du contrôle. Elle touche essentiellement `ViewPanel.qml` et l'ancien `ShellWindow` : ne pas incorporer son code à ce chantier.

Aucune duplication de PR #305/#306 : les fichiers ont été inspectés directement sur `main`.

## Nouvelle cascade identifiée : révision de points → raster vectoriel

Ancien chemin, même avec #305/#306 :

```
SetStitchParamsCommand / undo / redo
  -> DocumentSession::revision++
  -> lastChangeAffectsEmbroidery() == true
  -> StudioController::changed
  -> VectorSpaceController::refresh()
  -> VectorLayer::build(session.project()) // dessine TOUTES les formes
  -> GpuCanvas::setVectorLayer(nouveau snapshot)
  -> nouvelle texture vectorielle Qt Quick
```

Le recalcul est justifié **pour la séquence cousue**, mais pas pour les contours vectoriels inchangés. De plus, `computeStageStatus` était encore appelé à chaque sélection et à chaque publication de statut.

### Correctif de cette PR

- À chaque **révision du document**, calculer une petite signature visuelle ordonnée des objets vectoriels : id, visible, verrouillé, lien région, couleur et géométrie (empreinte sémantique + origine absolue, car l'empreinte sémantique seule est invariante par translation). Aucune recherche ni signature sur un simple changement de sélection.
- Refaire le fond vectoriel **seulement si la signature de la liste a changé**. Le ROI de sélection reste indépendant.
- Éviter `computeStageStatus` sur simple sélection ou fin de worker ; invalider le cache de statut sur révision ou `setSegmentationDirty`. Le comportement de l'état de l'étape reste identique.

**Compromis :** empreinte 64 bits calculée sur les nœuds à chaque révision (coût O(nombre de nœuds), sans copie permanente des chemins). Le risque théorique de collision existe ; il n'affecterait que le rendu de la couche vectorielle, pas les points générés ni le DST. Si une garantie déterministe absolue est exigée, remplacer l'empreinte par une copie exacte et comparaison structurale, en mesurant le surcoût mémoire.

## Deuxième coût visible depuis #304 : tous les nœuds hors champ

`ToolController::rebuild()` conserve les nœuds de l'objet pour l'édition (nécessaire au picking et aux gestes). En revanche, `GpuCanvas::toolOverlay()` exportait **toutes** les poignées sous forme de `QVariantMap` vers le Canvas QML, même hors du viewport après pan/zoom.

- Culling uniquement des marqueurs `point`/`selected`, avec marge de 8 px ;
- polylignes, contours et prévisualisations conservés même si leurs extrémités sont hors écran (ils peuvent traverser la vue) ;
- aucun changement dans `ToolController`, le hit testing ou les commandes.

## Scène et moteur : état vérifié, suite proposée

Le cache local LRU `chunk_cache.cpp` (64 Mio) et sa réutilisation sans copie sont intégrés. Le calcul final reste global pour les étapes dépendantes du contexte : jointures, finitions, compensation prédictive et ordre de couture. La structure immuable `StudioSceneSnapshot` contient déjà `effectiveSequence` partagée et trois durées de phases ; le GPU reconstruit toujours ses batches sur changement de snapshot de scène.

**Ne pas mettre en place un cache final par objet naïf** : il pourrait altérer les points d'autres objets via les finitions et le champ de tirage. Suivre les temps CPU par phase et la mémoire avant de découper le backend de scène en plages incrémentales.

## Tests / validation

- `test_studio_space_vector` : ajout d'une broderie, changement réel de longueur de point, undo/redo → même snapshot vectoriel et même nombre de rasterisations ; translation vectorielle → une rasterisation obligatoire.
- `test_studio_tools` : poignées visibles présentes ; après pan hors champ, aucune poignée QML, contours conservés.
- Tests existants de #305/#306 pour sélection, backend software, raster ROI et cache de séquence inchangés.
- **La compilation Linux/Windows et le rendu GPU réel doivent être vérifiés en CI et sur matériel**. Les durées / FPS ne sont pas inférés de la réduction des allocations.

Ce lot n'introduit aucun nouveau spinner ni changement de couture, de sortie OSP/DST ou de logique du moteur.
