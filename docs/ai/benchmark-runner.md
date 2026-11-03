# Benchmark de segmentation — mode d'emploi (PR-01)

Compare, hors ligne et avec la seule bibliothèque standard Python, la **baseline CIELAB actuelle** et le **worker SAM 2.1 existant** sur un corpus épinglé. Aucun modèle n'est requis : un backend indisponible est `skipped` avec sa raison, jamais remplacé par une estimation.

    python scripts/openstitch_ai_bench.py --out bench-out --cli <openstitch-cli>
    python scripts/openstitch_ai_bench.py --out bench-out --cli <openstitch-cli> --sam-models <dossier> --sam-model small
    python scripts/openstitch_ai_bench.py --out bench-out --controls     # + témoins de la métrique

Variables : `OPENSTITCH_CLI`, `OPENSTITCH_SAM_MODELS`, `OPENSTITCH_SAM_PYTHON` (interpréteur ayant torch + sam2 + numpy + Pillow).

## Entrées (épinglées)

- Manifeste `openstitch.ai.benchmark-manifest/1` (`tests/fixtures/ai-benchmark/manifest.json`) : par cas, `source_hash`, dimensions px et **mm**, type d'image, droits, familles de sujets principaux ; par objet, famille, genre (`area`/`line`), couleur, `required`/`protected`, masque or (empreinte SHA-256), boîte, ancre, composantes, trous, relations `under/over`.
- Le runner refuse (code 2) un fichier dont l'empreinte diffère ou un chemin qui sort du corpus.
- Fixture fournie : `marine-synth-01` (voilier + phare + mer + écume + cordages, CC0, générée par `scripts/ai_bench/fixtures.py`). Couleurs partagées volontairement entre objets distincts (voiles, mât/cordages, écume/tour).

## Backends

| backend | rôle | skip si |
|---|---|---|
| `cielab` | `openstitch-cli project digitize --debug-dir` ; régions = composantes 4-connexes de même couleur de `regions.png` ; `project check --json` | CLI introuvable |
| `sam2` | pilote `sam-worker/openstitch_sam_worker.py` en JSON Lines v1 (inchangé), profil `balanced` | poids/checkpoint ou environnement absents, `SAM2_NOT_INSTALLED`, etc. |
| `oracle`, `lighthouse_only` | **témoins** de la métrique (jamais comparés à un modèle) | — |

`cielab` fixe `--max-colors 12 --skip-background no` ; les autres options sont les défauts de la version du CLI (enregistrée dans le rapport).

## Métriques (par objet, puis agrégées)

- **Présence** : l'union des régions majoritairement (≥ 50 %) contenues dans l'objet atteint IoU ≥ 0,5. Sont rapportés aussi l'IoU de la meilleure région seule et la **fragmentation** (nombre de régions regroupées).
- **Frontière / trait** : F-mesure avec tolérance en mm (`--tol-mm`, 0,5 mm par défaut) : contours pour une aire, pixels pour un trait fin (mât, cordages).
- **Trous** : conservation des trous protégés (fenêtre de la tour). **Composantes** attendues vs prédites. **Couverture** de l'image.
- **Porte sémantique bloquante** : tout objet `required` absent = échec, quels que soient les autres scores. Le témoin `lighthouse_only` (phare parfait, voilier absent) doit échouer.
- **Moteur** : résumé de `project check` (erreurs/avertissements, points, sauts, coupes) pour `cielab` ; `null` + raison sinon.

## Sorties

`report.json` (`openstitch.ai.benchmark-report/1`, reproductible octet pour octet : pas d'horodatage), `summary.md`, `compare_<cas>_<backend>.png` (source | masques or | segmentation du backend). Versions épinglées : hash du manifeste, version du runner, révision git, version du CLI, SHA-256 du checkpoint et version du protocole du worker.

## Limites

- Fixtures synthétiques : elles valident la métrique et la chaîne, **elles ne classent pas** les modèles sur de vraies illustrations (corpus de 40–60 images à constituer).
- Les points, sauts et coupes ne sont comparables qu'entre backends passant par le même moteur ; seul `cielab` passe aujourd'hui par `project digitize`.
- Dans cette PR, SAM 2 n'a été exécuté que contre un faux worker (poids absents sur la machine de développement) : aucun résultat SAM 2 réel n'est annoncé.
- La métrique de présence ne mesure pas la brodabilité.
