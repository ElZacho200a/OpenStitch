# Audit de performance du pipeline

## Protocole

Le banc `openstitch-bench` exécute le même chemin que l'application :
chargement, segmentation, vectorisation, auto-numérisation, génération de
points, retouches, export DST et sauvegarde OSP. Il n'est pas enregistré dans
CTest afin que les variations de charge de la machine ne deviennent jamais un
critère de réussite.

Build et exécution de référence sous Windows, en Release et sans autre build
concurrent :

```powershell
cmake --build build/msvc --config Release --target openstitch-bench --parallel 1
build/msvc/tests/bench/Release/openstitch-bench.exe tests/fixtures/tentabrode.png --reps 1
```

La fixture est l'image utilisateur exacte `tests/fixtures/tentabrode.png`
(1117 × 1408), sans réduction. Les empreintes de segmentation et de séquence
sont rapportées pour détecter une optimisation qui modifierait le résultat.

## Goulot identifié

Une instrumentation temporaire de la version naïve de Zhang-Suen mesurait
94,6 s dans l'amincissement sur 100,7 s d'analyse Auto-Satin. Cette
implémentation rescannait toute la grille à chaque demi-passe. La version
optimisée maintient seulement la frontière susceptible d'être effacée et
utilise une table des 256 voisinages. L'effacement reste différé jusqu'à la
fin de chaque demi-passe : le résultat doit donc rester bit à bit identique.

Le test `test_skeleton_equivalence.cpp` compare la version optimisée à la
fonction naïve d'origine sur tout le corpus procédural, deux résolutions et
40 masques aléatoires. Il compare également une analyse recalculée à une
analyse servie par le cache de squelette.

## Cache de squelette

Pendant une planification, la même géométrie est analysée plusieurs fois par
le solveur local puis par la décomposition. `SkeletonCacheScope` mémorise la
rasterisation, le champ de distance, le squelette et le graphe brut sur le
thread courant. La clé contient toutes les positions des contours et les
paramètres de rasterisation ; l'élagage et le rapport de satinabilité restent
recalculés avec leurs propres paramètres. La charge utile du cache est bornée
à 64 Mio et libérée à la sortie du scope externe.

## Mesures vérifiées le 22 septembre 2026

Après optimisation, trois processus indépendants sur la fixture exacte ont
produit les mêmes 518 objets brodés, 99 812 commandes et l'empreinte de
séquence `f0d364959c09791a` :

| Exécution | `auto_digitize` | Pic mémoire |
|---|---:|---:|
| 1 | 19 574 ms | 227,7 Mio |
| 2 | 19 294 ms | 227,5 Mio |
| 3 | 17 716 ms | 227,3 Mio |

Ces chiffres incluent le budget déterministe de 32 évaluations d'oracle du
planificateur satin. Des essais contrôlés à 12 et 20 évaluations donnaient
respectivement 449 et 488 objets, pour 16,5–17,6 s : 32 conserve nettement
mieux la qualité sans réintroduire de décision fondée sur le temps mur.

La comparaison 94,6 s → 17,7–19,6 s indique le gain global attendu mais
ne doit pas être lue comme un micro-benchmark isolé : le chemin complet inclut
aussi le cache et les autres étapes d'auto-numérisation. La preuve de
correction repose sur l'équivalence bit à bit, pas sur le chronomètre.
