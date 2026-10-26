# Matrice de licences — code **et** poids

*Vérifié le 2026-10-26 sur les pages publiques officielles (dépôts GitHub / Hugging Face). Une ligne « non confirmé » **interdit l'intégration** tant qu'elle n'est pas levée en lisant le fichier `LICENSE` ou la carte du modèle. Rien de ceci n'est ajouté au dépôt, aux binaires ou à la CI par cette PR.*

La licence d'un **checkpoint** est distincte de celle du **code** : toujours les valider séparément.

| Composant | Code | Poids (checkpoints) | Statut | Remarque |
|---|---|---|---|---|
| SAM 2 / 2.1 (facebookresearch/sam2) | Apache-2.0 | Apache-2.0 | vérifié | Le dépôt liste aussi BSD-3-Clause (code adapté de `cc_torch`, `LICENSE_cctorch`) et SIL OFL 1.1 pour des polices du *demo* (non utilisées). |
| SAM 3 (facebookresearch/sam3) | **SAM License** (pas Apache-2.0) | SAM License ; accès Hugging Face sur demande avec authentification | conditions non lues | Lire le texte intégral et le faire valider par le mainteneur **avant PR-10**. Ne jamais embarquer les poids. |
| Florence-2 (microsoft) | MIT (carte du modèle) | MIT (carte du modèle) | vérifié | `trust_remote_code` exécute du code du dépôt du modèle : épingler une révision. |
| Grounding DINO (IDEA-Research) | Apache-2.0 | non confirmé | poids à vérifier | La page ne donne pas de licence des poids. |
| HQ-SAM / HQ-SAM 2 (SysCV) | Apache-2.0 | non confirmé (checkpoints HQ-SAM 2 en « beta ») | poids à vérifier | Lire le README `sam-hq2` et la source de téléchargement. |
| MobileSAM (ChaoningZhang) | Apache-2.0 | non confirmé | poids à vérifier | Idem. |

## Règles

1. Une dépendance de worker n'est jamais livrée avec OpenStitch : l'utilisateur l'installe (comme `torch`/`sam2` aujourd'hui, cf. `sam-worker/requirements.txt`). Le dépôt ne distribue ni code ni poids de modèle.
2. Aucune dépendance GPL/AGPL, même transitive d'un worker documenté.
3. Chaque backend ajouté consigne dans son manifeste : URL, révision (commit), SHA-256 du checkpoint, licence du code, licence des poids, date de vérification.
4. Ces constats viennent de la lecture de pages publiques ; ce n'est **pas un avis juridique**.
