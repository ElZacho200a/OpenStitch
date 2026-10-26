# Intégration IA dans l'auto-numérisation — architecture et inventaire

*État : PR-00 (audit, aucun code, aucune dépendance). Date de l'audit : 2026-10-26.*
*Document de veille source : `OpenStitch_veille_IA_autodigitize_2026-10-26.md` — **non fourni dans cette session** ; les arbitrages ci-dessous reposent sur l'inspection du dépôt et sur la vérification des licences à la source, pas sur ce document.*

## Principes

1. **L'IA est facultative.** Le chemin traditionnel (CIELAB + k-means, `segmentation`, `autodigitize`) reste complet et hors ligne.
2. **Aucun modèle n'est présumé meilleur.** Un backend entre dans le produit seulement s'il bat la base de référence sur le benchmark ([plan-benchmark.md](plan-benchmark.md)).
3. **Isolation par processus.** Tout modèle tourne dans un worker externe (JSON Lines stdin/stdout, logs sur stderr). Jamais de PyTorch lié au cœur C++ (invariant `CLAUDE.md`).
4. **Les points viennent du moteur.** L'IA ne produit que des masques, des relations et des intentions ; les points, aperçus et DST sont générés par `stitch_generation::effective_sequence`.
5. **Une carte sémantique qui omet un sujet principal est un échec bloquant**, quel que soit le score géométrique des masques restants (cas d'acceptation : voilier + phare + mer + écume + détails fins).

## Inventaire réel (vérifié dans le code)

| Brique | Emplacement | État |
|---|---|---|
| Worker SAM 2.1 | `sam-worker/` (`openstitch_sam_worker.py`, `protocol.py`, `sam_service.py`, `model_registry.py`, `mask_*.py`, `result_writer.py`) | Long-vivant, JSON Lines, `PROTOCOL_VERSION = 1`. Requêtes : `ping`, `load_model`, `segment_image`, `cancel`, `shutdown`. Réponses : `worker_starting/ready`, `pong`, `model_ready`, `progress`, `segment_result`, `cancelled`, `cancel_acknowledged`, `shutting_down`, `error`. |
| Catalogue de modèles | `sam-worker/model_registry.py` ↔ `libs/ai_segmentation/src/model_catalog.cpp` | 4 tailles SAM 2.1 (tiny/small/base_plus/large), doublon Python/C++ maintenu à la main. **Spécifique à SAM 2** (pas de notion de backend). |
| Profils de génération | `sam_service.py` (`main_shapes`, `balanced`, `detail`) | Mode « tout segmenter » (grille de points) uniquement ; pas d'invite par boîte/point/texte. |
| Bibliothèque C++ | `libs/ai_segmentation` (`color_refine`, `label_map`, `mask_result`, `topology_cleanup`, `error`, `model_catalog`) | Sans Qt, testée (`tests/unit/ai_segmentation`). Post-traitement de masques, **pas** de client de protocole. |
| Client de protocole | `apps/desktop/sam_worker_client.{hpp,cpp}` (Qt, `QProcess`) | Le seul client. **Lié à Qt et au desktop** : ni CLI ni MCP ne peuvent piloter un worker. Passe par WSL sous Windows (HP-AUTO-011 ouvert). |
| IHM | `ai_segmentation_dialog`, `ai_preferences*` | Réservé au desktop. |
| Opérations sémantiques | `libs/project_ops/src/op_semantic.cpp` : `semantic_import`, `semantic_replace` ; `semantic_validate` et `semantic_map` côté MCP/`po::` | Natives, transactionnelles, `dry_run`, identité par nom d'objet `sem:<id> <n>`. |
| MCP | `libs/mcp/src/tools.cpp` : `semantic_validate`, `semantic_import`, `semantic_replace`, `semantic_map`, `project_compare`, `project_satin_compare`… | Aucun outil de segmentation IA. |
| Ponts Python | `scripts/openstitch_semantic*.py` | Toujours utilisables ; l'import/remplacement sont désormais aussi natifs. |

### Tests existants liés

- Worker : `sam-worker/tests/` (pytest, `SamService` mocké, ne requiert ni torch ni sam2).
- C++ : `tests/unit/ai_segmentation/*`, `tests/unit/project_ops/test_semantic.cpp`, `tests/unit/mcp/test_tools_semantic.cpp`.
- Ponts : `tests/scripts/test_openstitch_semantic*.py`.
- CI : aucun test n'exige de poids ni de téléchargement.

## Constats de l'audit

| # | Constat | Conséquence |
|---|---|---|
| F1 | Le protocole est conçu pour SAM 2 (`load_model(model=tiny…)`, `segment_image`). Pas de `capabilities`, de hash de modèle, de révision de backend, ni de requête `refine`. | PR-02 : contrat générique versionné. |
| F2 | Le client est dans `apps/desktop` (Qt) : le CLI/MCP ne peuvent pas lancer de segmentation IA. | PR-02 : client sans Qt dans `libs/ai_segmentation`, le desktop l'enveloppe. |
| F3 | `docs/agent-semantic-format.md` affirmait « pas encore importable nativement » alors que `semantic_import`/`semantic_replace` existent. | Corrigé dans cette PR. |
| F4 | Les opérations sémantiques s'exécutent sur une **copie** du projet avec une `UndoStack` locale (`operations.cpp`) ; `apps/desktop` n'appelle aucune opération sémantique. L'annulation d'un `semantic_import` dans le bureau n'est donc **pas** câblée. | À traiter avant toute promesse « undo/redo » côté UI (PR-09 au plus tard). |
| F5 | Le manifeste `openstitch.semantic/1` n'a ni boîte, ni score, ni `backend`/`model_hash`, ni relations typées autres que `under/over`. | PR-03/05 : extension **rétrocompatible**. |
| F6 | Aucun corpus ni métrique de segmentation (instances présentes, trous, traits). `docs/validation/image-import-corpus.md` couvre l'import, pas la segmentation. | PR-01. |
| F7 | `HP-UX-040` (contrat IA : repli couleur, annulation tardive) a été retiré le 2026-10-18 (« l'IA n'est pas une priorité »). La présente mission le **rouvre explicitement** à la demande du mainteneur. | Journal roadmap §28 ; son contenu (repli, annulation) devient une exigence de PR-02. |
| F8 | Les poids sont téléchargés par l'utilisateur ; `HP-DIST-006` (IA installable) reste ouvert. | Hors périmètre PR-00..02 ; rien n'est embarqué. |

## Architecture cible

```
image -> backend(s) -> masques normalisés + scores -> graphe sémantique (famille/instance/composant)
 (worker isolé,         (PR-02, PR-03/04)                  | PR-05
  JSON Lines)                                              v
                                semantic_validate -> semantic_import / semantic_replace (ICommand)
                                                           |
                          moteur OpenStitch : effective_sequence -> aperçu / DST / project_check
```

Découpage : PR-00 doc/ADR · PR-01 benchmark · PR-02 contrat de backend · PR-03 scène + Florence-2 · PR-04 comparaison SAM2.1/HQ-SAM 2/MobileSAM · PR-05 graphe sémantique · PR-06 traits/trous · PR-07 intentions de points · PR-08 boucle d'itération · PR-09 IHM · PR-10 SAM 3 (licence validée d'abord) · PR-11+ données de couture.

Documents liés : [ADR-001](adr-001-backends-ia-modulaires.md), [licences code + poids](licences-modeles.md), [plan de benchmark](plan-benchmark.md).
