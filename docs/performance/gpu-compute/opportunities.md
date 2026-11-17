# R02 — Matrice des opportunités

Pour chaque poste mesuré dans la [baseline](baseline.md), trois approches sont comparées :
(1) optimisation CPU de l'algorithme existant, (2) réécriture algorithmique, (3) GPU ou
hybride. Classement GPU : ●●● très prometteur, ●● potentiellement intéressant, ● peu pertinent,
✕ contre-productif. Coûts : tentabrode sauf mention, médianes chaudes, réglage natif.

| Poste | Coût baseline | (1) CPU existant | (2) Réécriture | (3) GPU | Décision R02 |
|---|---:|---|---|---|---|
| Simulation du tirage (marine) | 95 ms, 53 % du parcours | boucle indépendante par point, à paralléliser | index CSR, grandeurs par segment précalculées, rejet exact avant `exp` | ✕ FP64 `exp`/`tanh` absents ou variables selon le pilote | **fait** : 95 → 14 ms, sortie identique |
| Finitions, grille des ponts cachés | 68 ms (52 en construction de grille) | grille par bandes de rangées en parallèle | intervalle par rangée au lieu de la boîte englobante ; carré de la distance hors zone ambiguë | ● rastérisation de capsules possible, mais résultat consommé aussitôt par la recherche de chemin sur CPU | **fait** : 68 → 14 ms, grille identique |
| Ponts cachés, recherche de chemin | 5 ms | plans en parallèle (fonction pure) | — | ✕ A* irrégulier | **fait** (gain ≤ 5 ms) |
| Tirage prédit (production) | 0 sur le corpus, jusqu'à 6 passes | parallèle par point | même `PullField` | ✕ | **fait** (bénéficie de la réécriture) |
| Squelette auto-satin | 112 ms en somme des tâches (63 à 1 fil) | déjà parallèle par région | bord actif déjà en place ; restent les copies et les allocations | ● aller-retour par masque mesuré perdant (R01) | non traité : la somme des tâches gonfle avec la contention, le gain réel reste à isoler |
| Distance (auto-satin) | 50 ms en somme (20 à 1 fil) | déjà parallèle | Meijster/Felzenszwalb exact, mais OpenCV `DIST_L2,3` impose son chanfrein | ✕ **mesuré** : 4,8× plus lent sur GPU de bureau | rejeté (R01, R02) |
| Lissage des masques de classe | 39 ms | parallèle par classe | vote par bandes de lignes sans k cartes `CV_32F` pleines (HP-PERF-015, livré puis annulé dans `main` par `af61f094`) | ●● filtre régulier, mais k transferts par image | **candidat** (CPU d'abord) |
| Segmentation (affectation, composantes) | 53 ms | parallèle par ligne et par classe | étiquetage unique en une passe au lieu d'un `connectedComponents` par classe (O(k·N) → O(N)) | ●● CCL GPU exacte connue (Playne-Hawick, Allegretti), mais transfert du résultat requis par la suite CPU | **candidat** (CPU d'abord) |
| Aperçu CPU | 35 ms, indépendant du nombre de fils | bandes dynamiques plus fines via `parallel_for` | segments répartis par bande au lieu d'un parcours complet par bande | ●●● pour Studio (déjà sur GPU) ; ● pour le CLI | **candidat** : CPU pour le CLI |
| Décodage PNG | 19 ms | — | décodage par bandes (non standard pour PNG) | ✕ | non traité |
| Préfiltre médian | 10,7 ms (59 à 1 fil) | déjà par bandes | `cv::medianBlur` 8 bits déjà en temps constant pour les grands noyaux | ● | non traité |
| Tatami et liaisons | 72 ms en somme | déjà parallèle par unité | index 2D des arêtes à la place des bandes en y ([audit §11](../../performance-audit.md)) | ✕ résultat R01 : CPU indexé meilleur | **candidat** (CPU) |
| Création des fils à chaque appel | ~1 ms × ~12 appels | — | pool persistant déterministe | sans objet | **candidat** (architecture) |
| Régénération après une édition locale | ~110 ms (audit §5) | cache de fragments par objet | invalidation fine des finitions et du tirage par fenêtre | sans objet | **candidat** (architecture) |

## Lecture

Les deux premiers postes du parcours étaient des problèmes d'**algorithme** : rastérisation
quadratique, réévaluations, absence de rejet, et non de débit de calcul. Le parallélisme n'a
été appliqué qu'après la réécriture, quand le travail restant était à la fois indépendant et
assez gros. Le GPU ne figure dans aucune décision d'implémentation : aucun poste n'est à la
fois gros, régulier, résident et compatible avec l'exactitude exigée.
