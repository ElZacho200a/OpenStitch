# Phase 0 — Roadmap détaillée, ADR et critères d'entrée en Phase 1

## 1. Roadmap

Chaque phase se termine par : build vert, tests verts, démo reproductible, doc mise à jour, zéro régression. Les numéros suivent le cahier des charges (§30).

| Phase | Contenu | Livrable démontrable | Sortie quand |
|---|---|---|---|
| **0. Cadrage** | Cette étude | docs/phase0 | Validation utilisateur |
| **1. Socle** | Dépôt Git, CMake + presets, vcpkg (fmt, spdlog, Catch2, CLI11, OpenCV, Qt), lib `core` (unités, ids, Result), CI (3 jobs), CLI `info image.png`, fenêtre Qt affichant une image | `openstitch-cli info` + fenêtre avec image | Build reproductible Windows documenté ; tests verts ; CI verte ; zéro logique dans les widgets |
| **2. Canevas physique** | mm partout, pixels→µm à l'import, zoom/pan, règles, grille, cadre affiché | Image affichée à taille physique réglable | Une image 100 px importée à 50 mm mesure 50 mm sur les règles à tout zoom |
| **3. Prétraitement** | recadrage, resize, contraste/luminosité, gris, débruitage, alpha, quantification simple, aperçu non destructif, **infrastructure undo/redo + tâches asynchrones** | Pipeline de retouche avec annulation | Original jamais modifié ; pile de transformations sauvegardée ; undo complet |
| **4. Segmentation** | k-means/median cut en Lab, régions connexes (RegionId stables), carte des régions, sélection au clic, fusion/suppression/recoloration, nettoyage petites zones | Image → régions éditables | Logo 4 couleurs → 4+ régions manipulables |
| **5. Vectorisation** | contours OpenCV, hiérarchie trous, Douglas-Peucker, PathSet, sanitization Clipper2, édition de nœuds basique | Région → forme vectorielle propre éditable | Anneau segmenté → PathSet extérieur+trou, nœuds déplaçables |
| **6. Premiers points** | lib `stitch`, générateur point droit/triple, objets de contour, affichage points, stats | Chemin → points visibles + statistiques | Subdivision/fusion correctes en tests ; stats exactes |
| **7. DST minimal** | codec DST maison, aller-retour, `stats`/`dst2svg` en CLI, export/import GUI | DST exporté, relu, affiché | Golden verts ; aller-retour ±50 µm ; fichier lu par visualiseur tiers (test manuel) |
| **8. Satin** | colonne à rails, sections, densité, zigzag/centrale en sous-couche, compensation simple, édition sections, avertissement largeur | Colonne droite + courbe cohérentes | Aperçu sections ; warnings satin large |
| **9. Tatami complet** | trous, alternance, connexions bord, angle/densité/offset, sous-couche, compensation contour | Forme à trou remplie proprement | Golden tatami ; pénétrations décalées vérifiées |
| **10. Document complet** | calques, groupes, palette de fils, ordre manuel complet, format `.osp` (ZIP), autosave/récupération | Projet complet sauvegardé/rechargé | Save→load identité ; récupération après kill testée |
| **11. Simulation + analyse** | lecture animée, curseur de point, moteur de règles complet, carte de densité | Simulation + panneau d'analyse | Toutes les règles §15 MVP implémentées et testées |
| **12. Optimisation ordre** | graphe de dépendances, tri par couleur/proximité, coûts, verrous respectés | Comparaison manuel vs auto | Jamais de modification d'un ordre verrouillé |
| **13. Autonumérisation** | suggestions satin/tatami, directions, sous-couches | Assistant produisant des objets éditables | — |

**Jalon MVP (§31)** = fin de Phase 7 + les éléments document/undo des phases 3 et 10 ramenés au minimum (le MVP strict intercale : sauvegarde projet simplifiée et ordre manuel dès Phase 6–7).

## 2. Décisions ADR à créer (docs/adr/, format MADR court)

| ADR | Décision | Statut proposé |
|---|---|---|
| 001 | Nom temporaire « OpenStitch Studio », remplaçable (aucune occurrence en dur hors constante unique) | Proposé |
| 002 | Licence Apache-2.0 (permissive, clause brevets), « inbound = outbound », pas de CLA — validé le 2026-07-18 | **Accepté** |
| 003 | Unité interne = micromètre int32, types forts, positions absolues, Y vers le haut | Proposé |
| 004 | vcpkg manifeste + baseline verrouillée | Proposé |
| 005 | Clipper2 encapsulé dans `libs/geometry` (aucun type Clipper hors de la lib) | Proposé |
| 006 | OpenCV limité à core/imgproc/imgcodecs, encapsulé dans `libs/image` + `segmentation` | Proposé |
| 007 | Qt 6 Widgets LGPL en liaison dynamique ; cœur sans Qt ; QGraphicsView pour le canevas | Proposé |
| 008 | Codec DST interne (pas de dépendance libembroidery), deltas dérivés des positions quantifiées | Proposé |
| 009 | Format projet `.osp` = ZIP(JSON versionné + originaux + cache points) | Proposé |
| 010 | Command pattern obligatoire pour toute mutation du document dès la première mutation | Proposé |
| 011 | Erreurs par `Result`/`std::expected` aux frontières de lib ; exceptions confinées | Proposé |
| 012 | Catch2 v3 + CTest ; golden avec DST octet-à-octet + SVG diagnostic | Proposé |
| 013 | CI : MSVC Debug/Release + build Linux du cœur (garde-fou portabilité) + lint | Proposé |
| 014 | États de génération Clean/Dirty/ManuallyEdited ; jamais d'écrasement silencieux de retouches | **Accepté** — implémenté Lot 8.0/8.1 (états dérivés d'une empreinte, jamais stockés ; sortie de `Dirty` par abandon explicite uniquement en MVP, cf. `docs/lot8-manual-editing-design.md`) |

## 3. Critères précis pour démarrer la Phase 1

La Phase 1 démarre quand :

1. L'utilisateur (toi) a **validé explicitement** : la licence GPLv3 (ADR-002), l'unité micromètre (ADR-003), le choix Qt 6 Widgets (ADR-007), vcpkg (ADR-004), le périmètre MVP (01-analyse-besoins-mvp.md §2) et la structure de dépôt (02-architecture.md §2).
2. Les prérequis machine sont confirmés : Visual Studio 2022 (MSVC v143) ou Build Tools, CMake ≥ 3.27, Git, ~15 Go pour vcpkg/Qt.
3. Le nom du dépôt et son hébergement (GitHub, org ou compte perso) sont choisis.

Contenu exact de la Phase 1 (pour mémoire, aucune ligne de code avant validation) :
dépôt initialisé + LICENSE + README ; CMake racine + presets ; vcpkg.json ; `libs/core` (Micrometers, Millimeters, ids, Result, logging spdlog) avec tests ; `apps/cli` (`info` sur une image via `libs/image` minimal) ; `apps/desktop` (fenêtre Qt, ouverture et affichage d'un PNG, aucune logique métier) ; CI 3 jobs ; docs/build-windows.md.
