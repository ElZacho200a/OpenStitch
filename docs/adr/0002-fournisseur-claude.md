# ADR 0002 — Fournisseur Claude du compagnon : API Messages, client d'outils en processus

Statut : **acceptée** (issue #177, épopée #172). Complète l'[ADR 0001](0001-compagnon-ia-session-live.md).

## Contexte

Le compagnon doit permettre à Claude d'utiliser les outils OpenStitch sur le projet ouvert, avec une
architecture ouverte à d'autres fournisseurs, sous Windows packagé, sans exposer de serveur.

## Options examinées

| Option | Pour | Contre |
|---|---|---|
| **A. API Messages d'Anthropic + boucle d'outils dans l'application** | aucune dépendance native supplémentaire (Qt Network, déjà livré avec le desktop) ; contrôle total de la boucle, des permissions et de l'annulation ; testable avec un faux fournisseur ; pas de processus tiers à empaqueter | boucle d'agent à maintenir (≈ 300 lignes, `libs/agent`), pas de streaming dans le MVP |
| B. Agent SDK (Python/TypeScript) en sidecar | boucle toute faite | runtime Node/Python à embarquer et à mettre à jour, surface d'attaque (exécution d'outils intégrés : shell, fichiers) contraire à l'exigence « périmètre OpenStitch seul », licences et poids du packaging |
| C. Connecteur « remote MCP » de l'API | aucun code d'outils côté client | **exige un serveur HTTP public** : contraire à l'ADR 0001 (aucune exposition réseau) |

## Décision

**Option A.** `libs/agent` (sans Qt, sans réseau) porte l'interface `IAgentProvider`, le fournisseur
`AnthropicProvider` (API Messages `2023-06-01`, utilisation d'outils, images en entrée de résultats),
la boucle `AgentRunner`, `ToolPolicy` (liste d'autorisation + refus) et `ISecretStore`. Le transport
HTTPS (`QtHttpClient`), le stockage de secrets (Gestionnaire d'identifiants Windows) et l'accès aux
outils (`LiveToolHost` → `mcp::LiveService`, appelé **en processus**, sans client MCP stdio) sont
fournis par `apps/desktop`. Les outils sont exactement ceux du mode live (ADR 0001 §6) : l'approbation
des modifications reste celle du dock.

- **Dépendances et licences** : aucune nouvelle bibliothèque tierce (Qt Network est déjà sous la
  licence Qt du desktop ; nlohmann/json déjà utilisé). Aucun SDK Anthropic.
- **Secrets** : clé API saisie volontairement, stockée uniquement dans le Gestionnaire d'identifiants
  Windows (cible `OpenStitch/anthropic-api-key`) ; hors Windows le stockage est indisponible et le
  fournisseur reste inactif (la clé n'est jamais écrite sur disque). Jamais dans `QSettings`, journaux,
  messages d'erreur (`redact`) ni `.osp`. Pas d'abonnement Claude grand public réutilisable.
- **Périmètre** : l'agent ne reçoit que les outils autorisés (lecture + `project_apply` +
  `live_status`) ; tout autre outil demandé est refusé sans exécution (`bash`, `project_open/save/
  export/new/digitize` explicitement dans la liste de refus). Aucun accès disque, aucun shell.
- **Robustesse** : annulation (jeton partagé, requête HTTP avortée en < 100 ms), délai par requête
  (120 s), reprises bornées (≤ 5, recul exponentiel, `Retry-After` plafonné à 30 s) pour réseau,
  délai, 429 et 5xx ; pas de reprise sur 401/403 ni 4xx ; 12 allers-retours d'outils au plus par
  demande ; budget de jetons optionnel ; historique toujours valide (chaque `tool_use` reçoit son
  `tool_result`, même interrompu).
- **Données** : le dock et le dialogue Aide ▸ Compagnon IA… affichent le fournisseur, l'usage en
  jetons et indiquent que messages, résultats d'outils et aperçus partent vers `api.anthropic.com`.
  Le fournisseur est désactivé par défaut ; « Désactiver entièrement » coupe aussi le pont local.

## Conséquences

- Un second fournisseur = une classe `IAgentProvider` ; rien d'autre ne change.
- Le streaming de la réponse est reporté (interface prête : `ProviderCapabilities::streaming`).
- Le « sidecar » du ticket de packaging (#179) disparaît : rien de plus à embarquer que le desktop.
