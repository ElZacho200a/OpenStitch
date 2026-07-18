# OpenStitch Studio — Étude de Phase 0 (cadrage)

> Nom temporaire, remplaçable (ADR-001). Logiciel libre de numérisation pour broderie machine — C++20, Windows d'abord, GPLv3 proposée.

Cette étude couvre les 17 points demandés pour la Phase 0. **Aucun code n'est écrit avant validation explicite.**

| Document | Contenu (points du cahier des charges §34) |
|---|---|
| [01-analyse-besoins-mvp.md](01-analyse-besoins-mvp.md) | 1. Analyse du problème · 2. Définition réaliste du MVP |
| [02-architecture.md](02-architecture.md) | 3. Architecture · 4. Diagramme des modules · 8. Structure du dépôt |
| [03-bibliotheques-licences.md](03-bibliotheques-licences.md) | 5. Bibliothèques open source · 6. Licences des dépendances · 7. Licence du projet |
| [04-modele-donnees-unites.md](04-modele-donnees-unites.md) | 9. Modèle de données · 10. Unités et coordonnées · 11. Points et commandes |
| [05-format-dst.md](05-format-dst.md) | 12. Stratégie d'encodage/décodage DST |
| [06-tests-qualite.md](06-tests-qualite.md) | 13. Stratégie de tests (+ qualité et CI) |
| [07-risques.md](07-risques.md) | 14. Risques techniques |
| [08-roadmap-adr.md](08-roadmap-adr.md) | 15. Roadmap · 16. ADR à créer · 17. Critères de démarrage de la Phase 1 |

## Décisions structurantes soumises à validation

1. **Licence Apache-2.0** pour tout le dépôt (choix validé ; la proposition initiale GPLv3 a été écartée au profit d'une licence permissive). Qt reste en LGPL liaison dynamique.
2. **Unité interne : micromètre en int32**, types forts, positions absolues, aucune coordonnée en `double` nu.
3. **Qt 6 Widgets** (LGPL, liaison dynamique) pour l'interface ; le cœur ne dépend jamais de Qt.
4. **vcpkg en mode manifeste** pour les dépendances.
5. **Dépendances MVP** : OpenCV (image/segmentation), Clipper2 (géométrie), nlohmann/json, minizip-ng, Catch2, spdlog, fmt, CLI11.
6. **Codec DST écrit en interne** (libembroidery utilisée seulement comme référence croisée de validation).
7. **Format projet `.osp`** : ZIP contenant JSON versionné + images originales + cache de points régénérable.
8. **MVP strict** = pipeline complet image → régions → vecteurs → point droit/triple + tatami simple → DST, avec undo/redo, ordre manuel et analyse minimale. **Satin exclu du MVP** (Phase 8, mais le modèle l'anticipe).
