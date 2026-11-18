# Performances de la V2 : mesures réelles

> **Mise à jour : mesures Release ajoutées** (section « Release » ci-dessous). Les conclusions de la section Debug restent valables mais pessimistes.

Mesuré le 2026-11-05 avec `test_desktop_v2_core "[perf]"` (test caché `[.perf]`, source
`tests/unit/desktop_v2/test_perf_budget.cpp`). **Configuration Debug (MSVC, `msvc-debug`)** : les
chiffres Release, mesurés plus bas, sont nettement meilleurs.
Machine de développement Windows 11, un seul exemplaire du programme, rectangles de 8 × 8 mm remplis
(tatami) sur une grille, ajoutés par UN lot.

| Objets | Ajout du lot (ms) | Copie du projet (ms) | Séquence effective (ms) | Aperçu réaliste (ms) | Nœud p95 (ms) | Nœud max (ms) | +1 objet (ms) | Déplacer (ms) |
|---|---|---|---|---|---|---|---|---|
| 1 | 16,6 | 0,0 | 14,4 | 29,9 | 0,031 | 0,332 | 0,5 | 0,8 |
| 100 | 19,3 | 0,1 | 46,9 | 118,3 | 0,020 | 0,047 | 1,3 | 0,8 |
| 500 | 104,5 | 0,8 | 199,0 | 281,2 | 0,017 | 0,045 | 5,6 | 3,4 |
| 1 000 | 275,5 | 1,7 | 270,1 | 505,9 | 0,018 | 0,046 | 9,3 | 5,7 |

## Lecture par objectif

| Objectif de la mission | Résultat mesuré | Verdict |
|---|---|---|
| Retour visuel local au 95e centile < 50 ms | Aperçu d'un glisser de nœud : **0,02 ms** (p95), 0,33 ms au pire, quel que soit le nombre d'objets (le calcul ne dépend que de la forme glissée) | Atteint, avec une marge de plus de 1 000 ×. Contrôlé par un test (`CHECK(p95 < 50)`) |
| Aucun blocage perceptible > 100 ms sur un geste ordinaire | Sur le fil d'interface : ajouter un objet 9,3 ms, déplacer 5,7 ms, copier le projet 1,7 ms, **à 1 000 objets** | Atteint pour les gestes ordinaires |
| Pas de régénération complète des points à chaque mouvement de souris | Les aperçus de glisser (nœuds, rails, axe de fondu) sont de la géométrie pure ; la séquence n'est recalculée qu'au relâchement, en tâche de fond | Atteint par conception ; non mesuré par un compteur d'appels |
| Pas de modification accidentelle des sorties machine | Aucune optimisation d'interface ne touche `effective_sequence` ; les tests de déterminisme (Catch2) comparent les sorties | Atteint (tests) |

## Ce qui dépasse 100 ms et pourquoi c'est acceptable (ou non)

- **Ajout d'un lot de 500 à 1 000 objets : 105 à 276 ms sur le fil d'interface.** C'est une opération
  exceptionnelle (import, collage massif), pas un geste ordinaire, mais elle gèle brièvement
  l'interface. *Non corrigé* : à déplacer en tâche de fond si l'usage réel le justifie.
- **Séquence effective (jusqu'à 270 ms) et aperçu réaliste (jusqu'à 506 ms à 1 000 objets)** : ces
  calculs tournent déjà hors du fil d'interface (`TaskManager`), avec rejet des résultats périmés ;
  l'utilisateur voit l'ancien aperçu pendant le calcul au lieu d'une interface gelée.
- Les chiffres Debug surestiment le coût Release : voir la section Release en fin de document.

## Non mesuré à ce jour

- Sélection de nombreux objets (select-all à 1 000 objets) et rafales de réglages.
- Fluidité réelle du rendu du canevas (images par seconde) : aucune mesure sur écran.
- Consommation mémoire.

## Release (MSVC `msvc-release`, même banc, même machine)

| Objets | Ajout du lot (ms) | Copie du projet (ms) | Séquence effective (ms) | Aperçu réaliste (ms) | Nœud p95 (ms) | Nœud max (ms) | +1 objet (ms) | Déplacer (ms) |
|---|---|---|---|---|---|---|---|---|
| 1 | 0,8 | 0,0 | 0,3 | 9,8 | 0,001 | 0,018 | 0,1 | 0,0 |
| 100 | 0,9 | 0,0 | 2,8 | 23,3 | 0,002 | 0,007 | 0,2 | 0,1 |
| 500 | 4,3 | 0,1 | 6,5 | 49,7 | 0,001 | 0,003 | 0,7 | 0,4 |
| 1 000 | 8,9 | 0,2 | 12,1 | 97,8 | 0,003 | 0,006 | 1,8 | 0,9 |

Lecture : en Release, **tout** passe sous 10 ms sur le fil d'interface à 1 000 objets, y compris l'ajout d'un
lot de 1 000 objets (8,9 ms) que le Debug montrait à 276 ms ; l'aperçu réaliste (98 ms) et la séquence
effective (12 ms) restent en tâche de fond. Le point « lot de 1 000 objets > 100 ms » de la section Debug
**ne se confirme pas en Release** et n'est plus un problème. Toujours non mesuré : fluidité d'affichage
(images par seconde), sélection massive, mémoire.


## Coût d'une édition de forme et du pinceau (Release, lot 2, 2026-11-18)

Banc : `test_desktop_v2_core "[editcost]"` (étiquette cachée `[.perf]`). Un trait de 60 touches pousse **une**
forme au pinceau ; on mesure ensuite ce que paient le fil d'interface et les consommateurs. Les chiffres varient
d'une exécution à l'autre sur cette machine (jusqu'à ×1,6 sur les plus longs) : lire les ordres de grandeur.

Document de N rectangles de 8 × 8 mm, une forme poussée (ms) :

| Objets | Copie (appui) | Application | Séquence (cache tiède) | Séquence (à froid) | Scène Studio | Supprimer une forme | Annuler |
|---|---|---|---|---|---|---|---|
| 1 | 0,00 | 0,00 | 0,1 | 0,0 | 0,0 | < 0,01 | < 0,01 |
| 100 | 0,02 | 0,00 | 1,5 | 1,7 | 2,2 | < 0,01 | < 0,01 |
| 500 | 0,1 | 0,00 | 4,5 | 5,5 | 9 | 0,01 | < 0,01 |
| 1 000 | 0,2 | 0,00 | 7,0-7,8 | 10 | 16-17 | 0,02-0,04 | < 0,01 |

Une seule grande forme (polygone de ~120 mm, ±5 % de variation de rayon) à nombre de sommets croissant (ms) :

| Sommets | 60 mises à jour, rejeu complet | 60 mises à jour, reprise | Fin de geste (validation) | Séquence | Aplatissement de l'aperçu |
|---|---|---|---|---|---|
| 8 | 1,1-1,6 | 0,1 | 0,01 | 1,3 | 0,003 |
| 100 | 3,0 | 0,1 | 0,02 | 1,4 | 0,003 |
| 500 | 17-19 | 0,7-1,0 | 0,27 | 2,1 | 0,017 |
| 2 000 | 73-118 | 3,4-6,2 | 3,7-4,0 | 3,5 | 0,05 |

Lecture :

- **Le calcul du pinceau n'était pas le goulot** pour des formes ordinaires : même à 2 000 sommets, une mise à
  jour rejouée de bout en bout coûte 1-2 ms (73-118 ms pour les 60 cumulées). La reprise du geste (PR pinceau
  réactif) divise ce coût par ~20, mais il est déjà sous le seuil de perception ; son gain réel est surtout pour
  les formes très denses et les longs traits.
- **Sur le fil d'interface, tout est négligeable** : copie d'instantané ≤ 0,2 ms, application de la commande
  ≤ 0,03 ms, suppression de forme ≤ 0,04 ms, annulation < 0,01 ms, aplatissement de l'aperçu ≤ 0,05 ms.
- **Après une édition, la séquence et la scène se refont en entier** : à 1 000 objets, ~7 ms de séquence même
  avec le cache tiède (10 ms à froid : le cache par objet n'économise qu'environ 25 %) et ~16 ms de scène, soit
  ~24 ms hors fil d'interface avant que le canevas reflète le changement. Le coût croît avec le nombre
  d'objets du document, pas avec la taille de l'édition. À 100 objets : ~4 ms. C'est la seule piste d'invalidation
  ciblée (lot 3) qui a un gain mesurable, et il n'est sensible qu'au-delà de plusieurs centaines d'objets.
- **Non mesuré** : le trajet complet fil de travail → signal → reconstruction de la superposition → rendu du
  canevas, c'est-à-dire la latence ressentie entre un mouvement de souris et l'image. C'est elle qui décide si
  la sensation de lenteur du pinceau vient du calcul (non, d'après ce qui précède) ou de l'affichage et de la
  boucle d'événements ; à mesurer avec `openstitch-studio-bench` (fenêtre native) avant d'écrire l'aperçu léger
  (lot 4).
