# R02 — Suite des optimisations

Ordre proposé par gain mesurable sur coût et risque. Chaque lot suit le protocole R02 :
- baseline et après sur `profile_engine.ps1`, natif et monofil ;
- empreintes identiques (`compare_fingerprints.py`) ;
- test d'équivalence contre l'implémentation d'origine ;
- test un fil / huit fils.

| # | Lot | Poste visé (baseline tentabrode) | Approche | Risque |
|---|---|---|---|---|
| 1 ✔ | Aperçu CPU : équilibrage des bandes | 35 ms, indépendant du nombre de fils | bandes fines distribuées dynamiquement par `parallel_for` (respecte `OPENSTITCH_THREADS`) ; hypothèse différente de HP-PERF-012, dont la répartition des segments par bande a été mesurée sans gain à 8 bandes : ici c'est le déséquilibre de charge entre 8 bandes égales qui est visé | faible : chaque pixel reste écrit par une bande, dans l'ordre des brins |
| 2 | Pool de fils persistant pour `parallel_for` | ~1 ms par appel, ~12 appels par parcours | pool statique paresseux, même distribution par compteur, imbrication exécutée en ligne | moyen : arrêt propre, exceptions, réentrance |
| 3 | Reprendre HP-PERF-015 (lissage par bandes) | 39 ms, et k cartes `CV_32F` pleines en mémoire | livré par `57c754af` puis annulé dans `main` par `af61f094` (2026-10-31) ; la roadmap le donne encore pour livré. Comprendre la cause de l'annulation avant de le reprendre | moyen |
| 4 | Étiquetage unique de la segmentation | 53 ms | une passe de composantes connexes sur l'image d'étiquettes au lieu d'une par classe | moyen : numérotation des régions à préserver |
| 5 | Index 2D des arêtes du routage tatami | 72 ms en somme des tâches | grille parcourue le long du segment au lieu des bandes en y ([audit §11](../../performance-audit.md)) | faible à moyen |
| 6 | Invalidation fine après une édition locale | ~110 ms de régénération (audit §5) | finitions et tirage recalculés sur la fenêtre des objets modifiés et de leurs voisins de couture | élevé : dépendances d'ordre et de voisinage |
| 7 | Squelette auto-satin : isoler le coût réel | 112 ms en somme des tâches, 63 à un fil | profil par région, allocations et copies de la grille à bordure | à mesurer avant d'agir |

**Lot 1 livré (2026-11-17).** `render_threads` remplace 8 bandes égales sur threads dédiés par
`parallel_for` sur des bandes de 8 à 64 lignes (distribution dynamique, `OPENSTITCH_THREADS`
respecté). A/B entrelacé, étape aperçu, p50 : tentabrode 30,6 → 21,8 ms (−29 %), logo, texte et
marine inchangés (4,3 / 6,8 / 28 ms). Empreintes identiques sur tous les passages ; tests
raster existants (octets identiques au rendu de référence) verts.

## Piste GPU

Rouverte seulement si une condition de la [comparaison](backend-comparison.md#décision) est
remplie, mesure à l'appui. Le premier candidat serait alors l'aperçu réaliste haute résolution
hors Studio, résultat purement visuel. Le banc `openstitch-compute-prototype` permet de rejouer
la mesure de transfert sur toute nouvelle machine.
