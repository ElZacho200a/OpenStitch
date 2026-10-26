# Protocole de backend IA — `openstitch.ai.backend/1`

*PR-02. Remplace, pour les nouveaux backends, le protocole propre à SAM 2 (`sam-worker/`, v1) sans le modifier : le worker SAM 2 et le client du desktop restent inchangés.*

Un **backend** est un processus local, long-vivant, qui parle JSON Lines sur stdin/stdout. **stdout ne porte que des lignes du protocole** (toute autre sortie est une erreur côté client) ; les logs vont sur stderr. Un backend ne modifie jamais un projet : il rend des masques ; l'import passe par `semantic_import` / `semantic_replace` (donc `ICommand`, annulation).

## Cycle de vie

1. Le worker émet `hello` dès qu'il est prêt. Le client refuse (`PROTOCOL_MISMATCH`) toute version **majeure** différente de 1 : pas de dégradation silencieuse.
2. `capabilities`, `health`, puis `predict` / `refine` (une tâche active à la fois ; une seconde reçoit `BACKEND_BUSY`).
3. `cancel` est coopératif ; le worker répond `cancelled` pour la tâche visée (ou le `result` si elle avait déjà fini). Sans confirmation dans `cancel_grace`, le client arrête le processus.
4. `shutdown` → `bye`.

Chaque requête porte `id` (texte non vide) et `type` ; le client ajoute `"protocol": "openstitch.ai.backend/1"` (le worker refuse une autre version majeure). Les lignes émises par le client sont en ASCII pur (échappements `\u…`), indépendantes de la page de code de stdin.

## Messages du worker

| `type` | champs |
|---|---|
| `hello` | `protocol`, `backend_id`, `pid` |
| `capabilities` | `id`, `protocol`, `backend` = {`backend_id`, `backend_revision`, `display_name`, `model_id`, `model_hash` (SHA-256 ou `null`), `prompts` [`automatic`\|`box`\|`point`\|`text`\|`exemplar`], `devices`, `license_code`, `license_weights`, `experimental`} |
| `health` | `id`, `state` (`ready`\|`loading`\|`unavailable`\|`error`), `detail` |
| `progress` | `id`, `stage`, `fraction`? |
| `result` | `id`, `image_sha256`, `transform`, `masks`, `elapsed_ms`, `warnings` |
| `cancelled` | `id`, `target_id` |
| `unloaded`, `bye` | `id` |
| `error` | `id` (ou `null`), `code`, `message`, `details` |

`transform` = {`source_width`, `source_height`, `width`, `height`, `scale`, `crop` (`[x,y,w,h]` ou `null`)} : ce que le backend a fait de l'image en interne. **Les masques sont toujours rendus à la résolution de l'image source.**

`detections[]` (optionnel, planners comme Florence-2 : étiquette + boîte, sans masque) = {`id`, `label`, `bbox_xywh`, `prompt_index`?}.

`masks[]` = {`id`, `file` (PNG 8 bits sous `job_dir/masks/`, 255 = dans le masque), `bbox_xywh`, `area_px`, `confidence`? (rapportée par le modèle, jamais synthétisée), `prompt_index`?}.

## Requêtes du client

| `type` | champs |
|---|---|
| `capabilities`, `health`, `unload`, `shutdown` | — |
| `predict` | `job_dir` (contient l'image), `image_file` (défaut `input.png`), `prompts` [{`kind`, `box_xywh`?, `text`?}], `options` (propres au backend : profil, `max_resolution`…) |
| `refine` | réservé (PR-04) ; un backend qui ne sait pas répond `UNSUPPORTED_PROMPT` |
| `cancel` | `target_id` |

Une invite non listée dans `capabilities.prompts` est refusée en `UNSUPPORTED_PROMPT` avant tout calcul.

## Codes d'erreur

Réutilisent `libs/ai_segmentation/src/error.cpp` : `PROTOCOL_MISMATCH`, `INVALID_REQUEST`, `UNSUPPORTED_PROMPT`, `BACKEND_BUSY`, `IMAGE_LOAD_FAILED`, `INFERENCE_FAILED`, `CUDA_OUT_OF_MEMORY`, `SAM2_NOT_INSTALLED`, `CHECKPOINT_NOT_FOUND`, `MODEL_NOT_INSTALLED`, etc. Côté client s'ajoutent `BACKEND_TIMEOUT`, `BACKEND_UNAVAILABLE` et `WORKER_CRASHED`. Un code inconnu n'est jamais ignoré : il devient `INVALID_WORKER_RESPONSE` avec le code d'origine en détail.

## Backends fournis par `ai-worker/`

| `--backend` | invites | notes |
|---|---|---|
| `sam2` | `automatic` | adaptateur de `sam-worker/` (SAM 2.1) |
| `sam2-box` / `hqsam2-box` | `box` | `SAM2ImagePredictor` ; `--checkpoint` + `--config` explicites ; HQ-SAM 2 demande son propre paquet `sam2` (autre environnement) |
| `mobilesam` | `box` | `vit_t` |
| `florence2` | `automatic` (`<OD>`), `text` (ancrage de phrases) | classe native `transformers`, poids safetensors ; **aucun `trust_remote_code`** |
| `fake-grid`, `fake-flaky` | — | tests |

Contraintes d'implémentation : le worker lit stdin par `os.read` et appelle `Backend.preload()` dans le thread principal avant la boucle de service — sous Windows, `import torch` lancé depuis le thread d'une tâche ne rend jamais la main tant que le thread principal attend sur stdin (constaté).

## Implémentations

| Rôle | Emplacement |
|---|---|
| Worker générique, adaptateur SAM 2.1, deux faux backends | `ai-worker/` (`openstitch_ai_worker.py --backend sam2 \| fake-grid \| fake-flaky`) |
| Analyse/sérialisation, session, transport processus, registre | `libs/ai_segmentation` (`backend_protocol`, `backend_session`, `process_transport`, `backend_registry`) — sans Qt |
| Diagnostic | `openstitch-cli ai backends [--config F] [--json]`, outil MCP `ai_backends` |

L'adaptateur `sam2` réutilise `SamService`, `mask_cleaner` et `mask_deduplicator` de `sam-worker/` sans les modifier. Il ne gère que l'invite `automatic` (comme aujourd'hui) ; les masques sont redimensionnés à la résolution de la source si `max_resolution` a réduit l'image. `numpy`, `Pillow`, `torch` et `sam2` sont importés paresseusement : sans eux le worker démarre et signale `health.state = "unavailable"`.

## Déclarer des backends

Fichier JSON désigné par `--config` ou la variable `OPENSTITCH_AI_BACKENDS` :

```json
{
  "schema": "openstitch.ai.backends/1",
  "preferred": "sam2",
  "backends": [
    {"id": "sam2", "display_name": "SAM 2.1 small", "command": "python",
     "args": ["ai-worker/openstitch_ai_worker.py", "--backend", "sam2",
              "--models-dir", "D:/sam2-models", "--model", "small"],
     "cwd": "."}
  ]
}
```

OpenStitch ne télécharge ni n'installe rien. **Sécurité :** la commande lancée ne vient que de ce fichier, maîtrisé par l'utilisateur ; ni le CLI, ni le MCP, ni un agent ne peuvent en choisir une autre. Les arguments sont passés sans shell. Un `cwd` relatif est résolu depuis le dossier du fichier.

## Repli

`discover_backends` sonde chaque backend (états `available`, `unavailable`, `incompatible`, `failed`, `disabled`) sans qu'un échec n'empêche les suivants. `resolve_backend` choisit le backend préféré s'il est disponible **et** sait traiter l'invite demandée ; sinon `classical_fallback = true` avec la raison : la segmentation CIELAB reste toujours possible, aucun backend n'est requis.

## Limites de cette PR

- Le desktop utilise toujours son client Qt historique (protocole SAM v1) : la migration vers ce contrat, l'invite `box`/`point` de SAM 2 et `refine` sont prévues (PR-04, PR-09).
- Validé sur Windows (processus réels contre le worker générique et ses backends factices) ; la branche POSIX du transport est compilée et exercée par la CI Linux, non exécutée sur la machine de développement.
- Aucun poids de modèle n'a été utilisé : l'adaptateur SAM 2 n'a pas été exécuté avec de vrais poids.
