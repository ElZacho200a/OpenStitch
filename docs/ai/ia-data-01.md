# IA-DATA-01 : corpus synthétique mesuré par le moteur (L0 et L1)

> **État : prototype de données.** Aucun modèle entraîné, aucun label de préférence,
> aucune donnée tierce ingérée, aucune prétention de qualité textile. Fiche Notion :
> IA-DATA-01 (Roadmap — Chantiers). Complète IA-AUTO-01 et son ranker
> (`scripts/auto_digitize_ranker.py`, [ranker-phase2.md](ranker-phase2.md)).

## Pourquoi ce lot

`ranker-phase2.md` le dit : le ranker attend des métriques **réellement mesurées sur
des candidats produits par le moteur C++**, et ce producteur n'existait pas. Ce lot le
fournit pour des formes synthétiques, sans annotation humaine :

```
formes synthétiques (graine) ──► openstitch-cli project apply   (1 lot = forme + technique)
                                 openstitch-cli project check   --json  (séquence effective)
                                 openstitch-cli project density --json
                               ──► candidates.jsonl + manifest.json (SHA-256)
```

Toutes les mesures viennent de `stitch_analysis` via la CLI ; le script n'estime rien.

## L0 : audit

### Sources externes

Le registre est dans [ia-data-sources.csv](ia-data-sources.csv). Décisions (revue de
cadrage du 2026-11-13, reprise ici) :

| Source | Décision | Raison |
|---|---|---|
| Générateur synthétique de ce dépôt | **approved** | géométrie produite par le script, aucun actif tiers |
| jiang-s5 image-to-embroidery | **rejected** (pour l'instant) | licence non OSI, usage commercial et redistribution restreints ; référence méthodologique seulement |
| Ink/Stitch | **rejected** pour le code (GPL, interdit par `GUARDRAILS.md`) ; exemples **unknown** | licence par fichier non établie |
| pyembroidery | **research_only** | bibliothèque MIT, pas un dataset ; pas de dépendance ajoutée dans ce lot |
| CodeNStitch (Zenodo 7642304) | **unknown** | l'archive publique est un XLSX de métadonnées, pas 217 DST |
| Quick, Draw! | **research_only** | CC BY 4.0 (attribution) ; formes de départ possibles en L2, pas des labels de broderie |
| MSEmbGAN | **unknown** | disponibilité et droits ML non établis ; images rendues ≠ commandes |
| Hatch / Wilcom | **rejected** | logiciel propriétaire : comparaison manuelle ponctuelle seulement, pas de scraping ni d'entraînement sur ses sorties |

Aucune de ces sources n'est téléchargée par le script. Une nouvelle source exige une
ligne `approved` dans le registre avec preuve (`evidence_url`) avant toute ingestion.

### Actifs déjà présents dans le dépôt

| Actif | Nombre | Statut |
|---|---|---|
| `sample/Turner.dst`, `sample/*.png` | 2 | **pending** : auteur et licence non documentés, hors corpus |
| `tests/fixtures/*.png` | 22 | **pending** : fixtures de test, provenance à vérifier fichier par fichier |
| `tests/golden/*.svg` | 9 | sorties du moteur (dorés de non-régression), pas des données d'entraînement |

### Points d'injection et périmètre

- Le ranker IA-AUTO-01 (`openstitch.ai.ranker-sample/1`) exige neuf features, dont
  `detail_retention_ratio` et `angle_coherence_ratio` que le moteur **ne mesure pas
  aujourd'hui**. Ce lot ne les invente pas : la conversion vers le format du ranker
  attend une mesure moteur (voir « Limites »).
- `libs/autodigitize` n'est pas modifié. L'intégration (feature flag, repli C++) relève
  de L4, après preuve sur holdout.

### Budget

100 formes × 9 candidats = 2 700 appels CLI, **17 s** sur le PC de développement
(Release, `--jobs 8`), environ 1 Mo de JSONL. 10 000 formes ≈ 30 min et 100 Mo :
rien n'est versionné dans Git, le corpus se régénère depuis la graine.

## L1 : générateur et corpus

```bash
# Formes seules, sans moteur
python scripts/ia_data_corpus.py generate out/shapes-42 --seed 42 --count 100
# Formes + candidats mesurés
python scripts/ia_data_corpus.py build out/corpus-42 --seed 42 --count 100 \
    --cli build/msvc/apps/cli/Release/openstitch-cli.exe --engine-rev "$(git rev-parse HEAD)"
# Schémas + sommes de contrôle
python scripts/ia_data_corpus.py validate out/corpus-42
# Tests (le test d'intégration tourne si OPENSTITCH_CLI est défini)
python -m unittest discover -s tests/scripts -p 'test_ia_data_corpus.py' -v
```

`build` refuse d'écraser un corpus existant.

### Formes (`openstitch.ia-data.shape/1`)

Sept familles : `ellipse`, `rect`, `band`, `star`, `blob` (rayon à harmoniques),
`l_shape`, `u_shape`. Rotation quelconque, centrage dans le cadre, coordonnées en mm
sur une grille de 0,01 mm (Y vers le haut, sens trigonométrique). Environ 8 % des
formes sont réduites à 1,5–4 mm. Drapeaux : `tiny`, `thin` (bande < 2 mm),
`very_concave` (convexité < 0,6).

Chaque forme a son propre générateur aléatoire (`seed`, `index`) : la forme *i* ne
dépend pas du nombre demandé. `shape_id` est le SHA-256 de la géométrie canonique ;
`split_group = shape_id` ; `split` vaut `holdout` pour 20 % des groupes (hachage
stable), comme le découpage par `motif_group` du ranker.

Caractéristiques géométriques **antérieures au choix** : aire, périmètre, compacité,
convexité, boîte, axe principal, nombre de sommets.

### Candidats (`openstitch.ia-data.candidate-set/1`)

Catalogue fixe, dans cet ordre : `tatami_a0`, `tatami_a45`, `tatami_a90`,
`tatami_axis` (angle = axe principal), `running`, `directional`
(`directional_guides`), `spiral` (`spiral_fill`), `contour` (`contour_fill`),
`auto_satin`. Les noms de technique sont ceux du ranker.

| Statut | Sens |
|---|---|
| `measured` | lot accepté, points visibles, mesures complètes |
| `refused` | opération refusée par le moteur (code d'erreur conservé), comme dans Studio |
| `empty` | lot accepté mais aucun point visible cousu : **négatif réel** |
| `failed` | sortie CLI inattendue (JSON invalide, champ absent) : défaut d'outillage |

Mesures (`metrics`, toutes ≥ 0) : `covered_ratio`, `overflow_ratio`
(`null` si le moteur ne mesure pas la surface, par exemple un `running`),
`coverage_measured`, `stitches`, `jumps`, `trims`, `color_changes`, `thread_m`,
`short_stitches`, `long_moves_without_trim`, `travel_mm`, `quality_errors`,
`quality_warnings`, `layers_mean`, `layers_p99`, `layers_max`,
`area_over_{2,3,4}_layers_mm2`. Un champ manquant dans un rapport moteur est une
erreur, jamais un zéro.

`label` vaut toujours `null` en L1 : le validateur refuse toute étiquette.

### Manifeste (`openstitch.ia-data.manifest/1`)

Graine, version du générateur, catalogue, version de la CLI et révision du moteur,
SHA-256 / taille / nombre de lignes de chaque fichier, couverture (familles,
drapeaux, split, statuts par candidat). Pas d'horodatage : deux constructions
identiques donnent des fichiers identiques octet pour octet (vérifié avec 3 et 8
fils de calcul).

### Premier corpus (graine 42, 100 formes, moteur `0.1.0`)

| Candidat | measured | refused | empty |
|---|---:|---:|---:|
| tatami_a0, tatami_a45, spiral | 99 | 0 | 1 |
| tatami_a90, tatami_axis, running, contour | 100 | 0 | 0 |
| directional | 95 | 5 | 0 |
| auto_satin | 99 | 1 | 0 |

Observations moteur, à confirmer avant d'en faire des bugs :
`directional_guides` refuse les formes minuscules et aussi une ellipse de
12 × 13,5 mm (axe médian dégénéré d'une forme presque ronde) ; une bande de
1,6 × 0,9 mm ne produit aucun point visible en tatami et en spirale.

## Limites et suite

1. **Pas de label.** Choisir un « meilleur » candidat demande une politique de score
   proxy documentée (contraintes dures + coûts) et testée contre ses effets pervers,
   ou un avis humain. C'est L3 ; apprendre les préférences par défaut du moteur
   reviendrait à apprendre ses biais.
2. **Features du ranker manquantes.** `detail_retention_ratio` et
   `angle_coherence_ratio` doivent d'abord être mesurées par le moteur (par exemple
   via `project fidelity` sur une image rendue de la forme, et l'histogramme des
   directions de `check`).
3. **Une seule région par scène**, sans trou, couleur unique. Scènes multi-régions,
   trous, contacts de couleurs et ordre de couture : lot suivant.
4. **Formes ≠ images d'utilisateurs.** Écart de domaine assumé ; aucune conclusion de
   généralisation.
5. L2 (lecture DST/PES, labels faibles) n'est pas commencé.
