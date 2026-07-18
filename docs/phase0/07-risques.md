# Phase 0 — Principaux risques techniques

Classés par criticité (impact × probabilité), avec mitigation.

## R1 — Robustesse géométrique (critique)

Polygones issus de vectorisation : auto-intersections, sommets quasi-confondus, trous touchant le bord, aires quasi nulles. Les booléens/offsets flottants naïfs explosent sur ces cas.
**Mitigation** : coordonnées entières (µm) partout ; Clipper2 (conçu pour l'entier) comme unique moteur booléen/offset ; étape systématique de « sanitization » (union à soi-même, suppression des arêtes < seuil) à l'entrée de tout générateur ; tests unitaires sur cas dégénérés dès la Phase 5.

## R2 — Qualité du satin (élevé, différé Phase 8)

Le satin à deux rails avec sections transversales cohérentes dans les virages est le point dur algorithmique du projet (appariement des rails, angles aigus, largeur variable).
**Mitigation** : hors MVP ; modèle d'objet conçu dès maintenant pour l'accueillir (rails + sections éditables) ; commencer par le satin défini manuellement (deux rails dessinés) avant toute génération automatique d'axe ; prototype jetable avec visualisation des sections avant intégration.

## R3 — Écart rendu simulé / broderie réelle (élevé, permanent)

Les compensations (tirage, densité) sont des heuristiques ; sans tests sur machine réelle, on peut produire des DST « corrects » mais mal brodables.
**Mitigation** : paramètres exposés et documentés plutôt que magiques ; module de compensation isolé et remplaçable ; moteur d'analyse (points courts, densité) comme filet de sécurité ; solliciter tôt des testeurs disposant de machines (l'atelier EPITA est un terrain d'essai naturel).

## R4 — Performance du canevas (moyen)

Des centaines de milliers de points + zoom fluide dans QGraphicsView naïf = ralentissements.
**Mitigation** : dès la conception du rendu (Phase 2/6) : niveaux de détail (polyligne simplifiée en zoom faible), items groupés par objet, caches de QPainterPath, bounding boxes ; si insuffisant, passage du canevas en QOpenGLWidget — l'abstraction de rendu (§13) rend ce changement local.

## R5 — Undo/redo incomplet ou incohérent (moyen)

Un undo partiel corrompt la confiance dans l'éditeur ; l'ajouter tardivement force une refonte.
**Mitigation** : Command pattern obligatoire dès la **première** mutation du document (Phase 3) ; règle de revue : aucune mutation du document hors d'une commande ; test d'intégration « undo total = état initial ».

## R6 — Fidélité DST sur machines réelles (moyen)

Les conventions non écrites (jumps pour trim, en-têtes laxistes) varient selon les machines.
**Mitigation** : options d'export configurables (nb de jumps par trim), validation croisée avec libembroidery et visualiseurs tiers, retour terrain des testeurs.

## R7 — Dérive de périmètre (moyen, humain)

Le cahier des charges décrit des années de travail ; tenter tout en parallèle = rien de fini.
**Mitigation** : roadmap par phases avec critères de sortie (08-roadmap.md), MVP strict §31, revue de périmètre à chaque fin de phase.

## R8 — Dépendances Windows/vcpkg (faible)

Temps de build OpenCV/Qt via vcpkg long au premier build ; versions qui bougent.
**Mitigation** : baseline vcpkg verrouillée, cache binaire GitHub Actions, Qt installable aussi via l'installeur officiel (documenté), features OpenCV réduites au minimum.

## R9 — Contributions externes et licence (faible)

Confusion LGPL/GPL, code copié d'ailleurs par un contributeur.
**Mitigation** : THIRD_PARTY_LICENSES tenu à jour, CONTRIBUTING.md explicite (« pas de code copié de logiciels propriétaires ni de sources incompatibles GPL »), revue systématique.
