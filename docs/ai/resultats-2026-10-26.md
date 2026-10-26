# Premiers résultats mesurés — 2026-10-26

*Une seule image synthétique (`marine-synth-01`, aplats, 320×240 px = 160×120 mm), une exécution par configuration, GPU RTX 5060 Ti. Ces chiffres valident la chaîne et donnent un ordre de grandeur ; ils **ne classent pas** les modèles sur de vraies illustrations (corpus de 40–60 images encore à constituer).*

## Ce qui a été exécuté

| Élément | Version / poids | SHA-256 (poids) | Licence code / poids |
|---|---|---|---|
| SAM 2.1 small (automatique, profil `balanced`) | `sam2` 1.1.0, `sam2.1_hiera_small.pt` | `6d1aa6f3…dc4d38` | Apache-2.0 / Apache-2.0 |
| SAM 2.1 small guidé par boîtes | idem | idem | idem |
| HQ-SAM 2 large guidé par boîtes (autre environnement : `sam-hq2` installé, `setup.py` lu avant exécution, extension CUDA non compilée) | `sam2.1_hq_hiera_large.pt` | `03b8be4b…cdb05` | Apache-2.0 / Apache-2.0 (README du dépôt) |
| MobileSAM `vit_t` guidé par boîtes | `mobile_sam.pt` | non consigné | Apache-2.0 (dépôt) / non précisée |
| Florence-2-base-ft (planner) | format natif `transformers` 5.20 (aucun code distant exécuté), dépôt `florence-community/Florence-2-base-ft` | `ab06dea6…f87d42` | MIT / MIT (carte du modèle) |
| Baseline CIELAB | `openstitch-cli project digitize --max-colors 12 --skip-background no` | — | — |

Environnement : Python 3.10, PyTorch 2.11 + CUDA 12.8, hors dépôt (`C:\Users\zache\openstitch-ai\`).

## Instance de sujet entier (IoU de la meilleure région unique)

| configuration | voilier | phare | mer | objets protégés présents (composants) | régions |
|---|---|---|---|---|---|
| CIELAB (baseline) | 0,59 | 0,25 | 0,37 | 6/9 | 19 |
| SAM 2.1 small, automatique | 0,36 | 0,26 | 0,97 | 6/9 | 17 |
| **Florence-2 → SAM 2.1 small (boîtes)** | **0,96** | **1,00** | 0,97 | 1/9 | 3 |
| **Florence-2 → MobileSAM (boîtes)** | 0,84 | 1,00 | 0,98 | 1/9 | 3 |
| **Florence-2 → HQ-SAM 2 large (boîtes)** | 0,93 | 0,99 | 0,98 | n/m | 3 |

Lecture :

- Sans planner, ni la baseline ni SAM 2 automatique ne produisent **une instance** « voilier » ou « phare » (IoU ≤ 0,59) : ils les découpent en morceaux. Avec le plan de scène (« a sailboat. a lighthouse. the sea. »), Florence-2 localise les trois sujets (boîtes ≈ vérité terrain) et SAM 2.1 / MobileSAM rendent des masques entiers (≥ 0,84).
- SAM 2.1 small est meilleur que MobileSAM sur le voilier (0,96 contre 0,84) ; équivalents sur le phare et la mer. HQ-SAM 2 large (0,93 / 0,99 / 0,98) ne fait **pas mieux** que SAM 2.1 small sur ces aplats nets (voilier 0,93 contre 0,96, mer 0,98 contre 0,97) : son intérêt annoncé (frontières, structures fines) n'est pas visible sur cette image et reste à tester sur des cordages réels. Un seul échantillon, modèles de tailles différentes : écarts non concluants.
- **Aucun pipeline n'est suffisant** : la colonne « objets protégés présents » montre que les pipelines à instances perdent les composants (coque, voiles, mât, cordages, bandes de la tour) ; la baseline et SAM 2 automatique perdent le mât, les cordages ou une voile. La porte « objet requis absent » échoue pour **toutes** les configurations réelles — seul le témoin `oracle` la passe. Il faut donc le graphe sémantique (PR-05) pour recomposer les composants à partir des régions de couleur sous chaque instance, et un chemin dédié aux traits fins (PR-06).
- Le ciel n'est pas dans le plan de scène : il n'est pas segmenté par les pipelines à planner (comportement attendu, pas un défaut).

## Non exécuté, et pourquoi

- **SAM 3** : poids non téléchargés (licence « SAM License » à accepter et authentification Hugging Face).
- **Grounding DINO** : non évalué (Florence-2 suffisait pour le plan de scène).
- Temps d'inférence et mémoire GPU non relevés dans cette série.
- HQ-SAM 2 : colonne « composants protégés » non mesurée (`n/m`) — exécution limitée aux familles.

## Reproduire

    python scripts/openstitch_ai_bench.py --out out --controls --cli <openstitch-cli> \
      --sam-models <dossier> --sam-model small --sam-python <python avec torch+sam2> \
      --backends cielab,sam2,florence+sam2,florence+mobilesam --pipelines pipelines.json

`pipelines.json` : `{"florence+sam2": {"planner": [python, ai-worker/openstitch_ai_worker.py, --backend, florence2, --model-dir, <dossier natif>], "segmenter": [python, ai-worker/openstitch_ai_worker.py, --backend, sam2-box, --checkpoint, <pt>, --config, configs/sam2.1/sam2.1_hiera_s.yaml]}}`.
